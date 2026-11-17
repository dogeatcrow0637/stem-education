# ---------------------------------------------------------------------------
# 本地补丁：Waveshare BSP 把 LVGL 分块高度写死为 50 行（466×50×2≈46KB）。
# LVGL 画布在 PSRAM，SPI 每次刷新要申请同样大小的内部 DMA 反弹缓冲；
# Wi-Fi 扫描时内部堆碎片化（最大连续块约 24KB），整屏刷新会失败。
# managed_components 重新拉取会覆盖，这里在配置阶段幂等改成 10 行（≈9KB）。
# ---------------------------------------------------------------------------
set(EPW_BSP_SRC "${CMAKE_CURRENT_LIST_DIR}/../managed_components/waveshare__esp32_s3_touch_amoled_1_75c/esp32_s3_touch_amoled_1_75c.c")
if(EXISTS "${EPW_BSP_SRC}")
    file(READ "${EPW_BSP_SRC}" _epw_bsp_src)
    string(REPLACE ".buffer_height = 50," ".buffer_height = 10," _epw_bsp_patched "${_epw_bsp_src}")
    if(NOT _epw_bsp_patched STREQUAL _epw_bsp_src)
        file(WRITE "${EPW_BSP_SRC}" "${_epw_bsp_patched}")
        message(STATUS "epw: patched BSP LVGL draw buffer height 50 -> 10")
    endif()
endif()

idf_component_register(
    SRCS "main.c" "ui.c" "power.c" "wifi.c" "shot.c" "button.c" "doubao_bridge.c"
         "audio.c" "voice_client.c"
         "assets/epw_cjk_16.c" "assets/epw_cjk_20.c" "assets/epw_time_150.c"
         "assets/pip_faces.c"
    INCLUDE_DIRS "." "assets"
    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio esp_driver_i2s esp_wifi esp_netif esp_event
                  nvs_flash spi_flash esp_http_client mbedtls esp_psram
                  esp_websocket_client esp_audio_codec cjson
)
