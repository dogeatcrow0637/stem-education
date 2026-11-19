# 两块 24 行 RGB565 内部缓冲（合计约 45KB），DMA 传输与下一块绘制重叠。
# 总内存小于原 50 行单缓冲；托管 BSP 更新后仍在配置阶段幂等应用。
set(EPW_BSP_SRC "${CMAKE_CURRENT_LIST_DIR}/../managed_components/waveshare__esp32_s3_touch_amoled_1_75c/esp32_s3_touch_amoled_1_75c.c")
if(EXISTS "${EPW_BSP_SRC}")
    file(READ "${EPW_BSP_SRC}" _epw_bsp_src)
    string(REGEX REPLACE "[.]buffer_height = (10|24|50),[^\r\n]*" ".buffer_height = 24," _epw_bsp_patched "${_epw_bsp_src}")
    string(REPLACE ".use_psram = true," ".use_psram = false," _epw_bsp_patched "${_epw_bsp_patched}")
    string(REPLACE ".require_double_buffer = false," ".require_double_buffer = true," _epw_bsp_patched "${_epw_bsp_patched}")
    # 双麦立体声的每块 DMA 控制在 480 字节，避免联网后再次申请大块内部 RAM。
    if(NOT _epw_bsp_patched MATCHES "chan_cfg.dma_frame_num = 120;")
        string(REPLACE "chan_cfg.auto_clear = true;" "chan_cfg.dma_frame_num = 120;\n    chan_cfg.auto_clear = true;" _epw_bsp_patched "${_epw_bsp_patched}")
    endif()
    # 初始化失败必须停通道并清空句柄，否则再次启动会误报成功并断言。
    foreach(_epw_channel i2s_tx_chan i2s_rx_chan)
        string(REPLACE "        i2s_del_channel(${_epw_channel});\n    }"
            "        i2s_channel_disable(${_epw_channel});\n        i2s_del_channel(${_epw_channel});\n        ${_epw_channel} = NULL;\n    }"
            _epw_bsp_patched "${_epw_bsp_patched}")
    endforeach()
    string(REPLACE "    assert(i2s_data_if);" "    if (i2s_data_if == NULL) return NULL;"
        _epw_bsp_patched "${_epw_bsp_patched}")
    if(NOT _epw_bsp_patched STREQUAL _epw_bsp_src)
        file(WRITE "${EPW_BSP_SRC}" "${_epw_bsp_patched}")
        message(STATUS "epw: patched BSP to two 24-line internal draw buffers")
    endif()
endif()

# esp_codec_dev 旧版忽略 I2S 配置/启动错误，会在 DMA 分配失败后访问空描述符。
set(EPW_CODEC_SRC "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__esp_codec_dev/esp_codec_dev.c")
if(EXISTS "${EPW_CODEC_SRC}")
    file(READ "${EPW_CODEC_SRC}" _epw_codec_src)
    set(_epw_codec_patched "${_epw_codec_src}")
    foreach(_epw_call "data_if->set_fmt(data_if, dev->dev_caps, fs)" "data_if->enable(data_if, dev->dev_caps, true)")
        string(REPLACE "        ${_epw_call};" "        int ret = ${_epw_call};\n        if (ret != ESP_CODEC_DEV_OK) {\n            dev->input_opened = dev->output_opened = false;\n            return ret;\n        }" _epw_codec_patched "${_epw_codec_patched}")
    endforeach()
    if(NOT _epw_codec_patched STREQUAL _epw_codec_src)
        file(WRITE "${EPW_CODEC_SRC}" "${_epw_codec_patched}")
    endif()
endif()
set(EPW_I2S_SRC "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__esp_codec_dev/platform/audio_codec_data_i2s.c")
if(EXISTS "${EPW_I2S_SRC}")
    file(READ "${EPW_I2S_SRC}" _epw_i2s_src)
    string(REPLACE "        _i2s_drv_enable(paired, true, true);" "        if (ret == ESP_CODEC_DEV_OK) ret = _i2s_drv_enable(paired, true, true);" _epw_i2s_patched "${_epw_i2s_src}")
    if(NOT _epw_i2s_patched STREQUAL _epw_i2s_src)
        file(WRITE "${EPW_I2S_SRC}" "${_epw_i2s_patched}")
    endif()
endif()

idf_component_register(
    SRCS "main.c" "display_sync.c" "ui.c" "power.c" "wifi.c" "shot.c" "button.c" "doubao_bridge.c"
         "audio.c" "voice_client.c" "voice_barge.c" "weather.c" "nursery.c" "watch_system.c"
         "assets/epw_cjk_16.c" "assets/epw_cjk_20.c" "assets/epw_cjk_28.c" "assets/epw_time_150.c"
         "assets/pip_faces.c"
    INCLUDE_DIRS "." "assets"
    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio esp_driver_i2s esp_wifi esp_netif esp_event
                  nvs_flash esp_timer spi_flash esp_http_client mbedtls esp_psram
                  esp_websocket_client esp_audio_codec esp-sr cjson
)
