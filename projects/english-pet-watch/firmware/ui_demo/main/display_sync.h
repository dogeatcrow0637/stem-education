#pragma once

#include "lvgl.h"

/* 在显示锁内初始化；每帧首块发送前等待板载 TE，缺失信号时退回异步刷新。 */
void display_sync_init(lv_display_t *display);
