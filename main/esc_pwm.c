#include "esc_pwm.h"

#include <inttypes.h>
#include <string.h>

#include "driver/ledc.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"

#define ESC_NVS_NAMESPACE "esc_config"
#define ESC_NVS_KEY_PROTOCOL "proto"
#define ESC_NVS_KEY_GPIO "gpio"
#define ESC_NVS_KEY_FREQ "freq"
#define ESC_NVS_KEY_DUTY "duty"

#define ESC_PWM_SPEED_MODE LEDC_LOW_SPEED_MODE
#define ESC_PWM_TIMER LEDC_TIMER_0
#define ESC_PWM_CHANNEL LEDC_CHANNEL_0
#define ESC_PWM_RESOLUTION LEDC_TIMER_14_BIT
#define ESC_PWM_MAX_DUTY ((1U << 14) - 1U)

#define ESC_DSHOT_RESOLUTION_HZ 40000000U
#define ESC_DSHOT_POST_DELAY_US 50U
#define ESC_DSHOT_SYMBOLS 17U
#define ESC_DSHOT_MIN_THROTTLE 48U
#define ESC_DSHOT_MAX_THROTTLE 2047U

static const char *TAG = "esc_pwm";

static esc_pwm_config_t s_config = {
    .protocol = ESC_PROTOCOL_PWM,
    .gpio_num = ESC_PWM_DEFAULT_GPIO,
    .frequency_hz = ESC_PWM_DEFAULT_FREQ_HZ,
    .duty_tenths = ESC_PWM_DEFAULT_DUTY_TENTHS,
};
static bool s_enabled = false;
static bool s_initialized = false;
static rmt_channel_handle_t s_dshot_channel = NULL;
static rmt_encoder_handle_t s_dshot_encoder = NULL;
static bool s_dshot_channel_enabled = false;
static rmt_symbol_word_t s_dshot_symbols[ESC_DSHOT_SYMBOLS];

bool esc_pwm_gpio_is_supported(gpio_num_t gpio_num)
{
    switch (gpio_num) {
    case GPIO_NUM_0:
    case GPIO_NUM_1:
    case GPIO_NUM_2:
    case GPIO_NUM_3:
    case GPIO_NUM_4:
    case GPIO_NUM_5:
    case GPIO_NUM_6:
    case GPIO_NUM_7:
    case GPIO_NUM_10:
    case GPIO_NUM_18:
    case GPIO_NUM_19:
    case GPIO_NUM_20:
    case GPIO_NUM_21:
        return true;
    default:
        return false;
    }
}

static uint32_t duty_tenths_to_ledc(uint16_t duty_tenths)
{
    return ((uint32_t)duty_tenths * ESC_PWM_MAX_DUTY) / 1000U;
}

const char *esc_pwm_protocol_to_string(esc_protocol_t protocol)
{
    switch (protocol) {
    case ESC_PROTOCOL_DSHOT150:
        return "dshot150";
    case ESC_PROTOCOL_DSHOT300:
        return "dshot300";
    case ESC_PROTOCOL_DSHOT600:
        return "dshot600";
    case ESC_PROTOCOL_DSHOT1200:
        return "dshot1200";
    case ESC_PROTOCOL_PWM:
    default:
        return "pwm";
    }
}

bool esc_pwm_protocol_from_string(const char *value, esc_protocol_t *protocol)
{
    if (!value || !protocol) {
        return false;
    }
    if (strcmp(value, "pwm") == 0) {
        *protocol = ESC_PROTOCOL_PWM;
    } else if (strcmp(value, "dshot150") == 0) {
        *protocol = ESC_PROTOCOL_DSHOT150;
    } else if (strcmp(value, "dshot300") == 0) {
        *protocol = ESC_PROTOCOL_DSHOT300;
    } else if (strcmp(value, "dshot600") == 0) {
        *protocol = ESC_PROTOCOL_DSHOT600;
    } else if (strcmp(value, "dshot1200") == 0) {
        *protocol = ESC_PROTOCOL_DSHOT1200;
    } else {
        return false;
    }
    return true;
}

static bool protocol_is_dshot(esc_protocol_t protocol)
{
    return protocol >= ESC_PROTOCOL_DSHOT150 && protocol <= ESC_PROTOCOL_DSHOT1200;
}

static uint32_t protocol_to_dshot_baud(esc_protocol_t protocol)
{
    switch (protocol) {
    case ESC_PROTOCOL_DSHOT150:
        return 150000U;
    case ESC_PROTOCOL_DSHOT300:
        return 300000U;
    case ESC_PROTOCOL_DSHOT600:
        return 600000U;
    case ESC_PROTOCOL_DSHOT1200:
        return 1200000U;
    default:
        return 0;
    }
}

uint16_t esc_pwm_get_dshot_throttle(void)
{
    if (s_config.duty_tenths == 0) {
        return 0;
    }
    return ESC_DSHOT_MIN_THROTTLE +
           (((uint32_t)s_config.duty_tenths * (ESC_DSHOT_MAX_THROTTLE - ESC_DSHOT_MIN_THROTTLE)) / 1000U);
}

static esp_err_t save_config(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ESC_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(handle, ESC_NVS_KEY_PROTOCOL, (uint8_t)s_config.protocol);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, ESC_NVS_KEY_GPIO, (uint8_t)s_config.gpio_num);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(handle, ESC_NVS_KEY_FREQ, s_config.frequency_hz);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(handle, ESC_NVS_KEY_DUTY, s_config.duty_tenths);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

static void load_config(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ESC_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved ESC config, using defaults");
        return;
    }

    uint8_t gpio = (uint8_t)s_config.gpio_num;
    uint8_t protocol = (uint8_t)s_config.protocol;
    uint32_t frequency_hz = s_config.frequency_hz;
    uint16_t duty_tenths = s_config.duty_tenths;

    nvs_get_u8(handle, ESC_NVS_KEY_PROTOCOL, &protocol);
    nvs_get_u8(handle, ESC_NVS_KEY_GPIO, &gpio);
    nvs_get_u32(handle, ESC_NVS_KEY_FREQ, &frequency_hz);
    nvs_get_u16(handle, ESC_NVS_KEY_DUTY, &duty_tenths);
    nvs_close(handle);

    if (protocol <= ESC_PROTOCOL_DSHOT1200 &&
        esc_pwm_gpio_is_supported((gpio_num_t)gpio) &&
        frequency_hz >= ESC_PWM_MIN_FREQ_HZ &&
        frequency_hz <= ESC_PWM_MAX_FREQ_HZ &&
        duty_tenths <= 1000) {
        s_config.protocol = (esc_protocol_t)protocol;
        s_config.gpio_num = (gpio_num_t)gpio;
        s_config.frequency_hz = frequency_hz;
        s_config.duty_tenths = duty_tenths;
        ESP_LOGI(TAG, "Loaded ESC config: protocol=%s, GPIO=%d, freq=%" PRIu32 "Hz, duty=%u.%u%%",
                 esc_pwm_protocol_to_string(s_config.protocol), s_config.gpio_num, s_config.frequency_hz,
                 s_config.duty_tenths / 10, s_config.duty_tenths % 10);
    } else {
        ESP_LOGW(TAG, "Saved ESC config invalid, using defaults");
    }
}

static esp_err_t stop_ledc_output(void)
{
    return ledc_stop(ESC_PWM_SPEED_MODE, ESC_PWM_CHANNEL, 0);
}

static void deinit_dshot(void)
{
    if (s_dshot_channel) {
        if (s_dshot_channel_enabled) {
            esp_err_t err = rmt_disable(s_dshot_channel);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to disable DShot RMT channel: %s", esp_err_to_name(err));
            }
            s_dshot_channel_enabled = false;
        }
        esp_err_t err = rmt_del_channel(s_dshot_channel);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to delete DShot RMT channel: %s", esp_err_to_name(err));
        }
        s_dshot_channel = NULL;
    }
    if (s_dshot_encoder) {
        esp_err_t err = rmt_del_encoder(s_dshot_encoder);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to delete DShot RMT encoder: %s", esp_err_to_name(err));
        }
        s_dshot_encoder = NULL;
    }
}

static void build_dshot_symbols(uint16_t throttle, uint32_t baud_rate)
{
    uint16_t packet = (uint16_t)(throttle << 1);
    uint16_t checksum_data = packet;
    uint16_t checksum = 0;
    for (int i = 0; i < 3; i++) {
        checksum ^= checksum_data;
        checksum_data >>= 4;
    }
    packet = (uint16_t)((packet << 4) | (checksum & 0x0F));

    uint32_t period_ticks = ESC_DSHOT_RESOLUTION_HZ / baud_rate;
    uint32_t t1h_ticks = (period_ticks * 75U) / 100U;
    uint32_t t0h_ticks = (period_ticks * 38U) / 100U;
    uint32_t delay_ticks = (ESC_DSHOT_RESOLUTION_HZ / 1000000U) * ESC_DSHOT_POST_DELAY_US;

    for (int bit = 0; bit < 16; bit++) {
        bool is_one = (packet & (1U << (15 - bit))) != 0;
        uint32_t high_ticks = is_one ? t1h_ticks : t0h_ticks;
        s_dshot_symbols[bit] = (rmt_symbol_word_t) {
            .level0 = 1,
            .duration0 = high_ticks,
            .level1 = 0,
            .duration1 = period_ticks - high_ticks,
        };
    }
    s_dshot_symbols[16] = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = delay_ticks / 2,
        .level1 = 0,
        .duration1 = delay_ticks - (delay_ticks / 2),
    };
}

static esp_err_t apply_dshot_config(bool force_recreate)
{
    uint32_t baud_rate = protocol_to_dshot_baud(s_config.protocol);
    if (baud_rate == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(stop_ledc_output(), TAG, "Failed to stop LEDC before DShot");
    if (force_recreate) {
        deinit_dshot();
    }

    if (!s_dshot_channel) {
        rmt_tx_channel_config_t channel_config = {
            .gpio_num = s_config.gpio_num,
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = ESC_DSHOT_RESOLUTION_HZ,
            .mem_block_symbols = 64,
            .trans_queue_depth = 2,
            .flags.init_level = 0,
        };
        ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&channel_config, &s_dshot_channel),
                            TAG, "Failed to create DShot RMT channel");

        rmt_copy_encoder_config_t encoder_config = {};
        ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&encoder_config, &s_dshot_encoder),
                            TAG, "Failed to create DShot RMT encoder");
    }
    return ESP_OK;
}

static esp_err_t apply_dshot_output(void)
{
    uint32_t baud_rate = protocol_to_dshot_baud(s_config.protocol);
    uint16_t throttle = s_enabled ? esc_pwm_get_dshot_throttle() : 0;
    build_dshot_symbols(throttle, baud_rate);

    if (s_dshot_channel_enabled) {
        ESP_RETURN_ON_ERROR(rmt_disable(s_dshot_channel), TAG, "Failed to stop previous DShot frame loop");
        s_dshot_channel_enabled = false;
    }
    ESP_RETURN_ON_ERROR(rmt_enable(s_dshot_channel), TAG, "Failed to enable DShot RMT channel");
    s_dshot_channel_enabled = true;

    rmt_transmit_config_t tx_config = {
        .loop_count = -1,
        .flags.eot_level = 0,
    };
    return rmt_transmit(s_dshot_channel, s_dshot_encoder, s_dshot_symbols,
                        sizeof(s_dshot_symbols), &tx_config);
}

static esp_err_t apply_ledc_config(void)
{
    deinit_dshot();

    ledc_timer_config_t timer_conf = {
        .speed_mode = ESC_PWM_SPEED_MODE,
        .timer_num = ESC_PWM_TIMER,
        .freq_hz = s_config.frequency_hz,
        .duty_resolution = ESC_PWM_RESOLUTION,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_conf), TAG, "Failed to configure LEDC timer");

    ledc_channel_config_t channel_conf = {
        .speed_mode = ESC_PWM_SPEED_MODE,
        .channel = ESC_PWM_CHANNEL,
        .timer_sel = ESC_PWM_TIMER,
        .gpio_num = s_config.gpio_num,
        .duty = s_enabled ? duty_tenths_to_ledc(s_config.duty_tenths) : 0,
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_conf), TAG, "Failed to configure LEDC channel");
    return ESP_OK;
}

static esp_err_t apply_output_duty(void)
{
    if (protocol_is_dshot(s_config.protocol)) {
        return apply_dshot_output();
    }

    uint32_t duty = s_enabled ? duty_tenths_to_ledc(s_config.duty_tenths) : 0;
    ESP_RETURN_ON_ERROR(ledc_set_duty(ESC_PWM_SPEED_MODE, ESC_PWM_CHANNEL, duty),
                        TAG, "Failed to set LEDC duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(ESC_PWM_SPEED_MODE, ESC_PWM_CHANNEL),
                        TAG, "Failed to update LEDC duty");
    return ESP_OK;
}

esp_err_t esc_pwm_init(void)
{
    load_config();
    s_enabled = false;

    esp_err_t err = apply_ledc_config();
    if (protocol_is_dshot(s_config.protocol)) {
        err = apply_dshot_config(true);
        if (err == ESP_OK) {
            err = apply_dshot_output();
        }
    }
    if (err == ESP_OK) {
        s_initialized = true;
        ESP_LOGI(TAG, "ESC output initialized locked: protocol=%s, GPIO=%d",
                 esc_pwm_protocol_to_string(s_config.protocol), s_config.gpio_num);
    }
    return err;
}

esp_err_t esc_pwm_set_config(esc_protocol_t protocol, gpio_num_t gpio_num,
                             uint32_t frequency_hz, uint16_t duty_tenths,
                             bool save)
{
    if (protocol > ESC_PROTOCOL_DSHOT1200) {
        ESP_LOGE(TAG, "Unsupported ESC protocol: %d", protocol);
        return ESP_ERR_INVALID_ARG;
    }
    if (!esc_pwm_gpio_is_supported(gpio_num)) {
        ESP_LOGE(TAG, "Unsupported ESC output GPIO: %d", gpio_num);
        return ESP_ERR_INVALID_ARG;
    }
    if (frequency_hz < ESC_PWM_MIN_FREQ_HZ || frequency_hz > ESC_PWM_MAX_FREQ_HZ ||
        duty_tenths > 1000) {
        ESP_LOGE(TAG, "Invalid PWM config: freq=%" PRIu32 ", duty_tenths=%u",
                 frequency_hz, duty_tenths);
        return ESP_ERR_INVALID_ARG;
    }

    esc_protocol_t old_protocol = s_config.protocol;
    gpio_num_t old_gpio = s_config.gpio_num;
    s_config.protocol = protocol;
    s_config.gpio_num = gpio_num;
    s_config.frequency_hz = frequency_hz;
    s_config.duty_tenths = duty_tenths;

    bool output_changed = old_protocol != protocol || old_gpio != gpio_num;
    esp_err_t err = protocol_is_dshot(protocol) ? apply_dshot_config(output_changed) : apply_ledc_config();
    if (err != ESP_OK) {
        return err;
    }
    if (old_gpio != gpio_num) {
        gpio_reset_pin(old_gpio);
    }
    if (save) {
        ESP_RETURN_ON_ERROR(save_config(), TAG, "Failed to save ESC config");
    }
    ESP_RETURN_ON_ERROR(apply_output_duty(), TAG, "Failed to update ESC output");

    ESP_LOGI(TAG, "ESC config set: protocol=%s, GPIO=%d, freq=%" PRIu32 "Hz, duty=%u.%u%%, enabled=%d",
             esc_pwm_protocol_to_string(s_config.protocol), s_config.gpio_num, s_config.frequency_hz,
             s_config.duty_tenths / 10, s_config.duty_tenths % 10, s_enabled);
    return ESP_OK;
}

esp_err_t esc_pwm_set_enabled(bool enabled)
{
    s_enabled = enabled;
    esp_err_t err = s_initialized ? apply_output_duty() : ESP_OK;
    ESP_LOGI(TAG, "PWM output %s", s_enabled ? "enabled" : "locked");
    return err;
}

esp_err_t esc_pwm_stop(void)
{
    s_enabled = false;
    return apply_output_duty();
}

const esc_pwm_config_t *esc_pwm_get_config(void)
{
    return &s_config;
}

bool esc_pwm_is_enabled(void)
{
    return s_enabled;
}

uint32_t esc_pwm_get_pulse_width_us(void)
{
    if (s_config.frequency_hz == 0) {
        return 0;
    }
    return (1000000U * (uint32_t)s_config.duty_tenths) /
           (1000U * s_config.frequency_hz);
}
