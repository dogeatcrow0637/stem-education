# 50 行 RGB565 内部单缓冲（约 46KB），启动时预分配，避免 PSRAM 刷屏的临时 DMA 拷贝。
# 保持较大分块以降低 SPI 提交次数；托管 BSP 更新后仍在配置阶段幂等应用。
set(EPW_BSP_SRC "${CMAKE_CURRENT_LIST_DIR}/../managed_components/waveshare__esp32_s3_touch_amoled_1_75c/esp32_s3_touch_amoled_1_75c.c")
if(EXISTS "${EPW_BSP_SRC}")
    file(READ "${EPW_BSP_SRC}" _epw_bsp_src)
    string(REGEX REPLACE "[.]buffer_height = (10|50),[^\r\n]*" ".buffer_height = 50," _epw_bsp_patched "${_epw_bsp_src}")
    string(REPLACE ".use_psram = true," ".use_psram = false," _epw_bsp_patched "${_epw_bsp_patched}")
    string(REPLACE ".require_double_buffer = true," ".require_double_buffer = false," _epw_bsp_patched "${_epw_bsp_patched}")
    if(NOT _epw_bsp_patched STREQUAL _epw_bsp_src)
        file(WRITE "${EPW_BSP_SRC}" "${_epw_bsp_patched}")
        message(STATUS "epw: patched BSP to a 50-line internal single draw buffer")
    endif()
endif()

idf_component_register(
    SRCS "main.c" "ui.c" "power.c" "wifi.c" "shot.c" "button.c" "doubao_bridge.c"
         "audio.c" "voice_client.c" "weather.c"
         "assets/epw_cjk_16.c" "assets/epw_cjk_20.c" "assets/epw_time_150.c"
         "assets/pip_faces.c"
    INCLUDE_DIRS "." "assets"
    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio esp_driver_i2s esp_wifi esp_netif esp_event
                  nvs_flash spi_flash esp_http_client mbedtls esp_psram
                  esp_websocket_client esp_audio_codec cjson
)
