idf_component_register(
    SRCS "main.c" "ui.c" "power.c" "wifi.c" "shot.c" "button.c" "doubao_bridge.c"
         "audio.c"
         "assets/epw_cjk_16.c" "assets/epw_cjk_20.c" "assets/epw_time_150.c"
         "assets/pip_faces.c"
    INCLUDE_DIRS "." "assets"
    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio esp_wifi esp_netif esp_event nvs_flash spi_flash
                  esp_http_client mbedtls esp_psram
)
