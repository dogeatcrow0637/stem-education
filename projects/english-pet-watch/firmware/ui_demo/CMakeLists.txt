# 英语宠物怀表 · UI Demo（独立 ESP-IDF 工程）
# 目标板：Waveshare ESP32-S3-Touch-AMOLED-1.75C（466×466 / 32MB Flash / 8MB PSRAM）
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-Wno-format)

project(epw_ui_demo)
