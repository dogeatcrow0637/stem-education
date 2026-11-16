cmake_minimum_required(VERSION 3.16)

# 设备上报给服务端的版本号。
# OTA 文件名必须是 {model}_{PROJECT_VER}.bin，例如 epw-watch-175_0.2.0.bin。
set(PROJECT_VER "0.1.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(english_pet_watch)
