/*
 * ESP32 MQTT 执行器 - WiFi 配网 + MQTT 监听
 * 订阅 agent 主题，收到消息时 GPIO2 闪烁 10 次 (0.7s 高, 0.3s 低)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "mqtt_client.h"
#include "wifi_provisioning.h"
#include "simple_wifi_sta.h"

static const char *TAG = "app_main";

#define MQTT_BROKER_URI  "mqtt://rovesoul.synology.me:1883"
#define MQTT_TOPIC       "agent"
#define GPIO_OUTPUT      GPIO_NUM_2

#define BLINK_HIGH_MS    700   // 0.7秒高电平
#define BLINK_LOW_MS     300   // 0.3秒低电平
#define BLINK_COUNT      8     // 闪烁10次

static esp_timer_handle_t blink_timer;
static int blink_remain = 0;

static void blink_callback(void *arg)
{
    if (blink_remain > 0) {
        // 切换电平
        static bool is_high = true;
        is_high = !is_high;
        gpio_set_level(GPIO_OUTPUT, is_high ? 1 : 0);

        int delay_ms = is_high ? BLINK_HIGH_MS : BLINK_LOW_MS;
        blink_remain--;
        esp_timer_start_once(blink_timer, delay_ms * 1000);  // 转换为微秒
    } else {
        gpio_set_level(GPIO_OUTPUT, 0);
        ESP_LOGI(TAG, "GPIO2 blink finished");
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        esp_mqtt_client_subscribe(client, MQTT_TOPIC, 1);
        ESP_LOGI(TAG, "Subscribed to %s", MQTT_TOPIC);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT data: topic=%.*s, data=%.*s",
                 event->topic_len, event->topic,
                 event->data_len, event->data);
        // 收到消息，GPIO2 闪烁
        blink_remain = BLINK_COUNT * 2;  // 每个周期2次切换
        gpio_set_level(GPIO_OUTPUT, 1);
        esp_timer_start_once(blink_timer, BLINK_HIGH_MS * 1000);
        ESP_LOGI(TAG, "GPIO2 blink start: %d times (0.7s high, 0.3s low)", BLINK_COUNT);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

static void mqtt_app_start(void)
{
    gpio_reset_pin(GPIO_OUTPUT);
    gpio_set_direction(GPIO_OUTPUT, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_OUTPUT, 0);

    esp_timer_create_args_t timer_conf = {
        .callback = blink_callback,
        .arg = NULL,
        .name = "blink_timer"
    };
    esp_timer_create(&timer_conf, &blink_timer);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_ERROR_CHECK(nvs_flash_init());

    // WiFi STA 初始化（包含配网逻辑）
    esp_err_t ret = wifi_sta_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "[APP] WiFi initialization completed");

    // 启动 MQTT
    mqtt_app_start();
}