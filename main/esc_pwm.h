#ifndef ESC_PWM_H
#define ESC_PWM_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#define ESC_PWM_MIN_FREQ_HZ 50
#define ESC_PWM_MAX_FREQ_HZ 1000
#define ESC_PWM_DEFAULT_FREQ_HZ 50
#define ESC_PWM_DEFAULT_DUTY_TENTHS 50
#define ESC_PWM_DEFAULT_GPIO GPIO_NUM_3

typedef enum {
    ESC_PROTOCOL_PWM = 0,
    ESC_PROTOCOL_DSHOT150,
    ESC_PROTOCOL_DSHOT300,
    ESC_PROTOCOL_DSHOT600,
    ESC_PROTOCOL_DSHOT1200,
} esc_protocol_t;

typedef struct {
    esc_protocol_t protocol;
    gpio_num_t gpio_num;
    uint32_t frequency_hz;
    uint16_t duty_tenths;
} esc_pwm_config_t;

esp_err_t esc_pwm_init(void);
esp_err_t esc_pwm_set_config(esc_protocol_t protocol, gpio_num_t gpio_num,
                             uint32_t frequency_hz, uint16_t duty_tenths,
                             bool save);
esp_err_t esc_pwm_set_enabled(bool enabled);
esp_err_t esc_pwm_stop(void);

const esc_pwm_config_t *esc_pwm_get_config(void);
bool esc_pwm_is_enabled(void);
uint32_t esc_pwm_get_pulse_width_us(void);
uint16_t esc_pwm_get_dshot_throttle(void);
bool esc_pwm_gpio_is_supported(gpio_num_t gpio_num);
const char *esc_pwm_protocol_to_string(esc_protocol_t protocol);
bool esc_pwm_protocol_from_string(const char *value, esc_protocol_t *protocol);

#endif
