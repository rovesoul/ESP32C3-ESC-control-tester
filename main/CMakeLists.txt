idf_component_register(
    SRCS "app_main.c" "wifi_provisioning.c" "simple_wifi_sta.c" "esc_pwm.c" "esc_web.c"
    PRIV_REQUIRES esp-tls esp_wifi esp_event nvs_flash esp_driver_gpio esp_driver_ledc esp_http_server esp_timer
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif nvs_flash esp_driver_gpio esp_driver_ledc esp_http_server esp_timer
)
