/*
 * 真机截图：LVGL 快照 -> RGB565 -> 每行 base64 经串口输出
 *
 * 用法（主机侧）：
 *   grep -o 'SHOT:[^:]*:[0-9]*:[A-Za-z0-9+/=]*' monitor.log > shot.txt
 *   再按 label 重组并写成 BMP（见 tools/decode_shots.py）。
 *
 * 说明：
 *   - 只在 CONFIG_EPW_BOOT_SHOTS=y 时自动播放并截图，正常固件不启用。
 *   - 输出不依赖文件系统，只用 printf，方便 USB Serial/JTAG 抓取。
 *
 * V0.11 截图序列（标签）：
 *   home         表盘（真实电源状态，通常为插线充电）
 *   home_normal  表盘（强制普通模式，便于对比外圈）
 *   center       控制中心（4+3 圆形磁贴，含手电）
 *   status       状态页（只读硬件信息 / 网络）
 *   bright       亮度调节页
 *   vol          音量调节页
 */
#include "shot.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "ui.h"

static const char *TAG = "epw-shot";

static const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void b64_encode(const uint8_t *in, size_t len, char *out)
{
    size_t i = 0;
    size_t o = 0;
    while (i + 2 < len) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = kB64[(v >> 18) & 0x3F];
        out[o++] = kB64[(v >> 12) & 0x3F];
        out[o++] = kB64[(v >> 6) & 0x3F];
        out[o++] = kB64[v & 0x3F];
        i += 3;
    }
    if (i < len) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < len) {
            v |= (uint32_t)in[i + 1] << 8;
        }
        out[o++] = kB64[(v >> 18) & 0x3F];
        out[o++] = kB64[(v >> 12) & 0x3F];
        out[o++] = (i + 1 < len) ? kB64[(v >> 6) & 0x3F] : '=';
        out[o++] = '=';
    }
    out[o] = '\0';
}

void shot_dump(const char *label)
{
    esp_err_t err = bsp_display_lock(2000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display lock failed: %s", esp_err_to_name(err));
        return;
    }
    lv_draw_buf_t *buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    bsp_display_unlock();

    if (buf == NULL) {
        ESP_LOGE(TAG, "snapshot failed");
        return;
    }

    int w = buf->header.w;
    int h = buf->header.h;
    int stride = buf->header.stride;

    printf("SHOTMETA:%s:%d:%d:%d\n", label, w, h, stride);

    char line[1600];
    for (int y = 0; y < h; y++) {
        b64_encode(buf->data + (size_t)y * stride, (size_t)w * 2, line);
        printf("SHOT:%s:%03d:%s\n", label, y, line);
        /* 长串口输出会饿死 IDLE 任务触发 TWDT；每 8 行让出 1ms。 */
        if ((y & 7) == 7) {
            vTaskDelay(1);
        }
    }
    printf("SHOTEND:%s\n", label);
    fflush(stdout);

    ESP_LOGI(TAG, "dumped '%s' %dx%d", label, w, h);
    lv_draw_buf_destroy(buf);
}

static void shot_ui(void (*fn)(void), const char *label, int delay_ms)
{
    bsp_display_lock(-1);
    fn();
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    shot_dump(label);
}

void shot_capture_demo_pages(void *arg)
{
    (void)arg;
    esp_log_level_set("*", ESP_LOG_NONE);

    vTaskDelay(pdMS_TO_TICKS(6000));

    /* 表盘：拔线普通模式，便于对比外圈 */
    bsp_display_lock(-1);
    ui_demo_power_mode(0);
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(600));
    shot_dump("home_normal");

    /* 控制中心 */
    bsp_display_lock(-1);
    ui_demo_open_center();
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(500));
    shot_dump("center");

    /* 状态页 */
    bsp_display_lock(-1);
    ui_demo_open_status();
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(500));
    shot_dump("status");
    bsp_display_lock(-1);
    ui_demo_close_status();
    bsp_display_unlock();

    /* 亮度调节页 */
    bsp_display_lock(-1);
    ui_demo_open_bright();
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(500));
    shot_dump("bright");
    bsp_display_lock(-1);
    ui_demo_close_bright();
    bsp_display_unlock();

    /* 音量调节页 */
    bsp_display_lock(-1);
    ui_demo_open_volume();
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(500));
    shot_dump("vol");
    bsp_display_lock(-1);
    ui_demo_close_volume();
    ui_demo_close_center();
    bsp_display_unlock();

    printf("SHOTALLDONE\n");
    fflush(stdout);
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "demo page capture finished");

    vTaskDelete(NULL);
}
