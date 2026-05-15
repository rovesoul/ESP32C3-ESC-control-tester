#include "esc_pwm.h"

#include <inttypes.h>

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"

#define ESC_NVS_NAMESPACE "esc_config"
#define ESC_NVS_KEY_GPIO "gpio"
#define ESC_NVS_KEY_FREQ "freq"
#define ESC_NVS_KEY_DUTY "duty"

#define ESC_PWM_SPEED_MODE LEDC_LOW_SPEED_MODE
#define ESC_PWM_TIMER LEDC_TIMER_0
#define ESC_PWM_CHANNEL LEDC_CHANNEL_0
#define ESC_PWM_RESOLUTION LEDC_TIMER_14_BIT
#define ESC_PWM_MAX_DUTY ((1U << 14) - 1U)

static const char *TAG = "esc_pwm";

static esc_pwm_config_t s_config = {
    .gpio_num = ESC_PWM_DEFAULT_GPIO,
    .frequency_hz = ESC_PWM_DEFAULT_FREQ_HZ,
    .duty_tenths = ESC_PWM_DEFAULT_DUTY_TENTHS,
};
static bool s_enabled = false;
static bool s_initialized = false;

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

static esp_err_t save_config(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ESC_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(handle, ESC_NVS_KEY_GPIO, (uint8_t)s_config.gpio_num);
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
    uint32_t frequency_hz = s_config.frequency_hz;
    uint16_t duty_tenths = s_config.duty_tenths;

    nvs_get_u8(handle, ESC_NVS_KEY_GPIO, &gpio);
    nvs_get_u32(handle, ESC_NVS_KEY_FREQ, &frequency_hz);
    nvs_get_u16(handle, ESC_NVS_KEY_DUTY, &duty_tenths);
    nvs_close(handle);

    if (esc_pwm_gpio_is_supported((gpio_num_t)gpio) &&
        frequency_hz >= ESC_PWM_MIN_FREQ_HZ &&
        frequency_hz <= ESC_PWM_MAX_FREQ_HZ &&
        duty_tenths <= 1000) {
        s_config.gpio_num = (gpio_num_t)gpio;
        s_config.frequency_hz = frequency_hz;
        s_config.duty_tenths = duty_tenths;
        ESP_LOGI(TAG, "Loaded ESC config: GPIO=%d, freq=%" PRIu32 "Hz, duty=%u.%u%%",
                 s_config.gpio_num, s_config.frequency_hz,
                 s_config.duty_tenths / 10, s_config.duty_tenths % 10);
    } else {
        ESP_LOGW(TAG, "Saved ESC config invalid, using defaults");
    }
}

static esp_err_t apply_ledc_config(void)
{
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
    if (err == ESP_OK) {
        s_initialized = true;
        ESP_LOGI(TAG, "PWM initialized locked on GPIO=%d", s_config.gpio_num);
    }
    return err;
}

esp_err_t esc_pwm_set_config(gpio_num_t gpio_num, uint32_t frequency_hz,
                             uint16_t duty_tenths, bool save)
{
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

    gpio_num_t old_gpio = s_config.gpio_num;
    s_config.gpio_num = gpio_num;
    s_config.frequency_hz = frequency_hz;
    s_config.duty_tenths = duty_tenths;

    esp_err_t err = apply_ledc_config();
    if (err != ESP_OK) {
        return err;
    }
    if (old_gpio != gpio_num) {
        gpio_reset_pin(old_gpio);
    }
    if (save) {
        ESP_RETURN_ON_ERROR(save_config(), TAG, "Failed to save ESC config");
    }

    ESP_LOGI(TAG, "PWM config set: GPIO=%d, freq=%" PRIu32 "Hz, duty=%u.%u%%, enabled=%d",
             s_config.gpio_num, s_config.frequency_hz,
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
