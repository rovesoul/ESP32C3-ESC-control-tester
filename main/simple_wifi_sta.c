/*
 * 简化的 WiFi STA 初始化 - 只处理配网和连接
 */
#include "simple_wifi_sta.h"
#include "wifi_provisioning.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

static const char *TAG = "wifi";

// WiFi IP地址存储
static char wifi_ip_address[16] = "No IP";
static bool wifi_connected = false;

// 呼吸灯 GPIO
#define BREATHING_LED_GPIO  GPIO_NUM_8

static void breathing_led_task(void *arg)
{
    ESP_LOGI(TAG, "Breathing LED task started on GPIO8");

    // LEDC 配置
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_1,
        .freq_hz = 1000,  // 1kHz PWM
        .duty_resolution = LEDC_TIMER_8_BIT,
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t ch_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_1,
        .gpio_num = BREATHING_LED_GPIO,
        .duty = 0,
    };
    ledc_channel_config(&ch_conf);

    // 呼吸灯效果
    while (1) {
        // 渐亮 (0 -> 255)
        for (int duty = 0; duty <= 255; duty++) {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        // 渐暗 (255 -> 0)
        for (int duty = 255; duty >= 0; duty--) {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Connected to AP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            ESP_LOGI(TAG, "connect failed, retry now");
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            esp_ip4addr_ntoa(&event->ip_info.ip, wifi_ip_address, sizeof(wifi_ip_address));
            ESP_LOGI(TAG, "got ip: %s", wifi_ip_address);

            // WiFi 连接成功后，启动呼吸灯
            if (!wifi_connected) {
                wifi_connected = true;
                xTaskCreate(breathing_led_task, "breathing_led", 2048, NULL, 1, NULL);
            }
        }
    }
}

esp_err_t wifi_sta_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // 配网检测
    if (!wifi_provisioning_has_config()) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "No WiFi config found, entering provisioning mode...");
        ESP_LOGI(TAG, "========================================");

        esp_err_t err = wifi_provisioning_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Provisioning failed: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "Provisioning success! Device will restart...");
        esp_restart();
    }

    ESP_LOGI(TAG, "WiFi config found, connecting...");

    // 加载 WiFi 配置
    wifi_config_t wifi_config = {0};
    char saved_ssid[64] = {0};
    char saved_password[64] = {0};

    if (wifi_provisioning_load_config(saved_ssid, saved_password,
                                       sizeof(saved_ssid), sizeof(saved_password)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load WiFi config!");
        return ESP_FAIL;
    }

    strncpy((char *)wifi_config.sta.ssid, saved_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, saved_password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "Connecting to SSID: %s", saved_ssid);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_sta_init finished");

    // 启动按键检测（长按 5 秒清除配置重新配网）
    xTaskCreate((void (*)(void *))wifi_provisioning_check_button,
                "button_check", 2048, NULL, 1, NULL);

    return ESP_OK;
}

const char* get_wifi_ip_address(void)
{
    return wifi_ip_address;
}