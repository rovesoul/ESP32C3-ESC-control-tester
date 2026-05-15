/*
 * ESP32-C3 ESC tester
 *
 * First boot: SoftAP WiFi provisioning.
 * Normal boot: connect to saved WiFi and expose a local ESC control page.
 */

#include <inttypes.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "esc_pwm.h"
#include "esc_web.h"
#include "simple_wifi_sta.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_ERROR_CHECK(nvs_flash_init());

    esp_err_t ret = wifi_sta_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_ERROR_CHECK(esc_pwm_init());
    ESP_ERROR_CHECK(esc_web_start());

    ESP_LOGI(TAG, "ESC web console ready. Open http://%s", get_wifi_ip_address());
}
