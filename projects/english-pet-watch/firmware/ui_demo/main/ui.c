/*
 * 英语宠物怀表 · UI Demo V0.10
 * 目标板：Waveshare ESP32-S3-Touch-AMOLED-1.75C（466×466 圆屏）
 *
 * 视觉：面向小朋友的纯数字表盘。没有刻度环、没有分钟数字，只有
 *       大号时间、中文日期、天气、电量与 Wi-Fi；其他页面延续同一套
 *       深色底 + 彩色小图标语言，配色不只一种绿。
 *       外圈三色环固定为 003399 深蓝 / FFFF00 亮黄 / FF6600 橙。
 *
 * 交互（参考 Apple Watch / 华为手表）：
 *   表盘（唯一常驻页）：下滑 → 控制中心（表盘不再响应点按切页）
 *   控制中心（4+3 圆形磁贴）：状态 / 天气 / 电量 / 手电
 *                             亮度 / 音量 / AI
 *     ├─ 点「状态」或「电量」→ 状态页（从左往右滑关闭）
 *     ├─ 点「亮度」「音量」→ 亮度页 / 音量页（上下拖动调整，右滑关闭）
 *     ├─ 点「手电」→ 白色满亮度手电页（双击白屏退出）
 *     ├─ 点「AI」→ 宠物页（Pip · 3D 正脸猫，9 种情绪）
 *     └─ 上滑 → 回到表盘
 *   BOOT 短按 → 宠物页；PWR 短按 → 应用桌面（备用入口，长按 PWR 仍由 PMU 关机）。
 *   宠物页：表情 + 对话气泡，全手势交互（无文字按钮）：
 *     点猫=摸头，左右滑=聊天学英语，上滑=兴奋，下滑=睡觉，长按=思考，
 *     左上角箭头返回；本地情景引擎按电量/充电/网络/时间换状态。
 *     豆包动态台词会先做字形过滤，字体缺字绝不会渲染成方框。
 *
 * 不展示心率/血氧/步数等本机不支持的指标；不初始化音频，音量默认 0
 * （音量页只做 UI 演示，不会真的出声）。
 * 充电模式：底部电量弧变青柠并加柔光；拔线后恢复按电量配色。
 */
#include "ui.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "power.h"
#include "wifi.h"
#include "assets/pip_faces.h"

/* 中文子集字体 + 大号时间字体（由 tools/gen_ui_assets.sh 生成） */
extern const lv_font_t epw_cjk_16;
extern const lv_font_t epw_cjk_20;
extern const lv_font_t epw_time_150;

/* -------------------------------------------------------------------------- */
/* 设计常量                                                                    */
/* -------------------------------------------------------------------------- */

#define SCREEN_W 466
#define SCREEN_H 466
#define CENTER_X (SCREEN_W / 2)
#define CENTER_Y (SCREEN_H / 2)

/* 表盘：三色装饰环贴边，电量弧再向内收一圈，中间只留时间与日期。 */
#define RING_SIZE      446
#define RING_WIDTH     8
#define BATT_SIZE      414
#define BATT_WIDTH     7
#define BATT_FROM_DEG  120
#define BATT_TO_DEG    240

#define TILE_D      84
#define TILE_DOT_D  7

/* 亮度 / 音量页：竖向轨道（底部 0%，顶部 100%） */
#define SLIDER_TRACK_W   34
#define SLIDER_TRACK_H   224
#define SLIDER_TRACK_TOP 158
#define SLIDER_TRACK_X   (CENTER_X - SLIDER_TRACK_W / 2)

/* 应用桌面：6 个圆形图标，2 行 × 3 列，可拖动换位 */
#define APP_COUNT   6
#define APP_ICON_D  96
static const int s_app_cx[3] = {101, 233, 365};
static const int s_app_cy[2] = {190, 320};

static const char *TAG = "epw-ui";

/* 深色底 + 多种强调色：时间暖白，其余小图标用青 / 蓝 / 琥珀 / 紫 / 粉 / 珊瑚区分层级。 */
#define COL_BG      lv_color_hex(0x000000)
#define COL_SURF    lv_color_hex(0x12161C)
#define COL_SURF_HI lv_color_hex(0x1C232C)
#define COL_LINE    lv_color_hex(0x2A3340)
#define COL_TEXT    lv_color_hex(0xFFFFFF)
#define COL_MUTED   lv_color_hex(0x8B97A8)
#define COL_TEAL    lv_color_hex(0x36E0C0)
#define COL_TEAL_HI lv_color_hex(0xBFFBF0)
#define COL_LIME    lv_color_hex(0xB8F26A)
#define COL_BLUE    lv_color_hex(0x5EAEFF)
#define COL_AMBER   lv_color_hex(0xFFC24B)
#define COL_ORANGE  lv_color_hex(0xFF9F5A)
#define COL_CORAL   lv_color_hex(0xFF6B7A)
#define COL_PURPLE  lv_color_hex(0xB39BFF)
#define COL_PINK    lv_color_hex(0xFF8FB1)
#define COL_INK     lv_color_hex(0x0A0E14)

/* 表盘三色环：亮黄 + 橙 + 天蓝，最下面一段用天蓝收尾 */
#define COL_RING_YELLOW lv_color_hex(0xFFFF00)
#define COL_RING_ORANGE lv_color_hex(0xFF6600)
#define COL_RING_SKY    lv_color_hex(0x00BFFF)
/* 底部电量弧：普通模式红色，充电模式天蓝色（不再用绿色） */
#define COL_BATT_RED    lv_color_hex(0xFF0000)
/* 天气分割线 / 调节页统一绿色 */
#define COL_GREEN       lv_color_hex(0x2EE66B)

/* -------------------------------------------------------------------------- */
/* 状态                                                                        */
/* -------------------------------------------------------------------------- */

static lv_obj_t *s_screen;
static lv_obj_t *s_home;
static lv_obj_t *s_center;
static lv_obj_t *s_status;
static lv_obj_t *s_slider;
static lv_obj_t *s_torch;
static lv_obj_t *s_apps;
static lv_obj_t *s_pet;

static bool s_center_open;
static bool s_status_open;
static bool s_slider_open;
static bool s_torch_open;
static bool s_apps_open;
static bool s_pet_open;

/* 表盘 */
static lv_obj_t *s_time;
static lv_obj_t *s_date;
/* 三色装饰环（常驻） + 底部电量弧（随电量变化，充电时青柠色发光） */
static lv_obj_t *s_batt_glow;
static lv_obj_t *s_batt_track;
static lv_obj_t *s_batt_arc;
static lv_obj_t *s_batt_pct;
static lv_obj_t *s_batt_bolt;
static lv_obj_t *s_home_wifi[4]; /* 3 段弧 + 底部圆点，用颜色表达联网状态 */

/* 控制中心磁贴上的动态元素 */
static lv_obj_t *s_cc_bat_arc;
static lv_obj_t *s_cc_bat_pct;
static lv_obj_t *s_cc_bat_bolt;
static lv_obj_t *s_cc_bright_arc;
static lv_obj_t *s_cc_bright_pct;
static lv_obj_t *s_cc_vol_pct;
static lv_obj_t *s_ai_img;

/* 应用桌面：图标对象、图标->槽位映射、拖动状态 */
static lv_obj_t *s_app_icons[APP_COUNT];
static int s_app_slot[APP_COUNT];
static int s_app_drag_icon = -1;
static lv_point_t s_app_drag_press;
static lv_coord_t s_app_drag_x0;
static lv_coord_t s_app_drag_y0;
static bool s_app_drag_moved;

/* 宠物页 */
#define PET_IMG_SIZE 208
#define PET_IMG_X ((SCREEN_W - PET_IMG_SIZE) / 2)
#define PET_IMG_Y 92
#define PET_BUBBLE_Y 292
static lv_obj_t *s_pet_img;
static lv_obj_t *s_pet_glow;
static lv_obj_t *s_pet_halo;
static lv_obj_t *s_pet_bubble;
static lv_obj_t *s_pet_bubble_label;
static lv_obj_t *s_pet_mood;
static lv_obj_t *s_pet_spark[8];
static ui_pet_state_t s_pet_state = UI_PET_IDLE;
static int s_pet_phase;
static int s_pet_pop;
static int s_pet_last_bob;
static bool s_pet_chat_busy;
static bool s_pet_long_pressed; /* 长按已触发，拦截随后的 CLICKED */
static int s_pet_chat_stage;
static int s_pet_scene_idx;
static uint32_t s_pet_bridge_ms;
static lv_timer_t *s_pet_chat_timer;

/* 状态页 */
static lv_obj_t *s_status_bat;
static lv_obj_t *s_status_net;
static lv_obj_t *s_status_ip;

/* 亮度 / 音量调节页 */
enum {
    SLIDER_BRIGHT = 0,
    SLIDER_VOLUME = 1,
};
static int s_slider_kind;
static bool s_slider_dragging;
static lv_obj_t *s_slider_value;
static lv_obj_t *s_slider_fill;
static lv_obj_t *s_slider_track;

/* 提示条 */
static lv_obj_t *s_toast;
static lv_obj_t *s_toast_label;
static lv_timer_t *s_toast_timer;

static bool s_charging;
static int s_bat_percent = -1;
static int s_brightness = 80;
static int s_volume = 0;

/* 手电页 */
static lv_obj_t *s_torch_hint;
static lv_timer_t *s_torch_hint_timer;
static uint32_t s_torch_last_click;

/* 演示用电源模式覆盖：-1 自动，0 普通，1 充电 */
static int s_power_override = -1;

/* 兜底时钟（SNTP 未同步时从 10:09 走起） */
static int s_fb_minutes = 10 * 60 + 9;
static int s_fb_seconds;
static int s_last_h = -1;
static int s_last_m = -1;
static int s_last_day = -1;

static const char *s_week_cn[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

static void status_open(void);
static void status_close(void);
static void status_close_now(void);
static void slider_open(int kind);
static void slider_close(void);
static void slider_close_now(void);
static void slider_refresh(void);
static void torch_open(void);
static void torch_close(void);
static void torch_close_now(void);
static void center_open(void);
static void center_close(void);
static void center_close_now(void);
static void apps_open(void);
static void apps_close(void);
static void apps_close_now(void);
static void pet_open(void);
static void pet_close(void);
static void pet_close_now(void);
static void pet_apply(ui_pet_state_t state, const char *text);
static void brightness_apply(void);

/* -------------------------------------------------------------------------- */
/* 绘制小工具                                                                  */
/* -------------------------------------------------------------------------- */

static lv_obj_t *mk_box(lv_obj_t *parent, int x, int y, int w, int h,
                        lv_color_t color, lv_opa_t opa, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    return o;
}

static lv_obj_t *mk_circle(lv_obj_t *parent, int cx, int cy, int d,
                           lv_color_t color, lv_opa_t opa)
{
    return mk_box(parent, cx - d / 2, cy - d / 2, d, d, color, opa, LV_RADIUS_CIRCLE);
}

static lv_obj_t *mk_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                          lv_color_t color, int x, int y, int w, lv_text_align_t align)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    if (w > 0) {
        lv_obj_set_width(l, w);
        lv_obj_set_style_text_align(l, align, 0);
    }
    return l;
}

/* 圆弧：rotation 270 让 0° 从 12 点开始顺时针增长。
 * track_deg 是底轨长度，prog_deg 是指示弧长度；0 表示不画。
 * 注意 LVGL 9 只做一次 >360 归一，整圆必须写 0→360。 */
static lv_obj_t *mk_arc(lv_obj_t *parent, int cx, int cy, int d, int w,
                        lv_color_t track, lv_opa_t track_opa, int track_deg,
                        lv_color_t prog, lv_opa_t prog_opa, int prog_deg)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(arc, cx - d / 2, cy - d / 2);
    lv_obj_set_size(arc, d, d);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);

    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, w, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, track, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, track_opa, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);

    lv_obj_set_style_arc_width(arc, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, prog, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, prog_opa, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, prog_deg > 0, LV_PART_INDICATOR);

    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, track_deg);
    lv_arc_set_angles(arc, 0, prog_deg);
    return arc;
}

/* 任意起止角度的底轨（rotation 270，0° 在 12 点）。
 * 三色装饰环、电量底轨都用它；圆头让线条不像 CAD 线框。 */
static lv_obj_t *mk_arc_seg(lv_obj_t *parent, int cx, int cy, int d, int w,
                            int from, int to, lv_color_t color, lv_opa_t opa)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(arc, cx - d / 2, cy - d / 2);
    lv_obj_set_size(arc, d, d);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, w, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, opa, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);

    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, from, to);
    lv_arc_set_angles(arc, 0, 0);
    return arc;
}

/* 只有指示弧（可随时改角度），用作电量进度 / 充电辉光 */
static lv_obj_t *mk_arc_prog(lv_obj_t *parent, int cx, int cy, int d, int w,
                             int from, int to, lv_color_t color, lv_opa_t opa)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(arc, cx - d / 2, cy - d / 2);
    lv_obj_set_size(arc, d, d);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, opa, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, from, to);
    lv_arc_set_angles(arc, from, to);
    return arc;
}

/* 三色外环单段：同色柔光打底 + 主弧 + 外侧白色高光，
 * 让环有一点毛玻璃的通透反光，不再是纯色 CAD 线条。 */
static void mk_ring_seg(lv_obj_t *parent, int from, int to, lv_color_t color)
{
    mk_arc_seg(parent, CENTER_X, CENTER_Y, RING_SIZE, RING_WIDTH + 10,
               from, to, color, LV_OPA_20);
    mk_arc_seg(parent, CENTER_X, CENTER_Y, RING_SIZE, RING_WIDTH,
               from, to, color, LV_OPA_COVER);
    mk_arc_seg(parent, CENTER_X, CENTER_Y, RING_SIZE + 2, 2,
               from + 2, to - 2, lv_color_hex(0xFFFFFF), LV_OPA_30);
}

static void dot_place(lv_obj_t *dot, int cx, int cy, int d, int w, int deg)
{
    float rad = (float)deg * (float)M_PI / 180.0f;
    float r = (float)(d - w) / 2.0f;
    int x = cx + (int)lroundf(r * sinf(rad));
    int y = cy - (int)lroundf(r * cosf(rad));
    lv_obj_set_pos(dot, x - TILE_DOT_D / 2, y - TILE_DOT_D / 2);
}

static void draw_sun(lv_obj_t *parent, int cx, int cy, int r, lv_color_t color)
{
    for (int i = 0; i < 8; i++) {
        int deg = i * 45;
        float rad = (float)deg * (float)M_PI / 180.0f;
        int rr = r + 6;
        int x = cx + (int)lroundf((float)rr * sinf(rad));
        int y = cy - (int)lroundf((float)rr * cosf(rad));
        lv_obj_t *ray = mk_box(parent, x - 2, y - 5, 4, 10, color, LV_OPA_COVER, 2);
        lv_obj_set_style_transform_rotation(ray, deg * 10, 0);
    }
    mk_circle(parent, cx, cy, r * 2, color, LV_OPA_COVER);
}

/* 独立的太阳图标根对象：便于在表盘顶部按整体宽高定位 */
static lv_obj_t *mk_sun_icon(lv_obj_t *parent, int size, lv_color_t color)
{
    lv_obj_t *root = mk_box(parent, 0, 0, size, size, color, LV_OPA_TRANSP, 0);
    int c = size / 2;
    int core = size / 3;
    mk_circle(root, c, c, core, color, LV_OPA_COVER);
    for (int i = 0; i < 8; i++) {
        int deg = i * 45;
        float rad = (float)deg * (float)M_PI / 180.0f;
        int rr = c - 2;
        int x = c + (int)lroundf((float)rr * sinf(rad));
        int y = c - (int)lroundf((float)rr * cosf(rad));
        lv_obj_t *ray = mk_box(root, x - 1, y - 3, 2, 6, color, LV_OPA_COVER, 1);
        lv_obj_set_style_transform_rotation(ray, deg * 10, 0);
    }
    return root;
}

static void draw_info(lv_obj_t *parent, int cx, int cy, lv_color_t color)
{
    mk_circle(parent, cx, cy - 13, 7, color, LV_OPA_COVER);
    mk_box(parent, cx - 3, cy - 3, 6, 20, color, LV_OPA_COVER, 3);
}

static void draw_speaker(lv_obj_t *parent, int x, int y, lv_color_t color)
{
    static const lv_point_precise_t body[] = {
        {0, 7}, {8, 7}, {17, 0}, {17, 22}, {8, 15}, {0, 15}, {0, 7},
    };
    lv_obj_t *b = lv_line_create(parent);
    lv_line_set_points(b, body, 7);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_line_width(b, 3, 0);
    lv_obj_set_style_line_color(b, color, 0);
    lv_obj_set_style_line_rounded(b, true, 0);

    static const lv_point_precise_t slash[] = {{20, 0}, {32, 22}};
    lv_obj_t *s = lv_line_create(parent);
    lv_line_set_points(s, slash, 2);
    lv_obj_set_pos(s, x, y);
    lv_obj_set_style_line_width(s, 3, 0);
    lv_obj_set_style_line_color(s, COL_CORAL, 0);
    lv_obj_set_style_line_rounded(s, true, 0);
}

/* 手电图标：灯头 + 手柄 + 三束光 */
static void draw_torch(lv_obj_t *parent, int cx, int cy, lv_color_t color)
{
    mk_box(parent, cx - 12, cy - 22, 24, 13, color, LV_OPA_COVER, 4);
    mk_box(parent, cx - 6, cy - 9, 12, 24, color, LV_OPA_COVER, 4);

    static const lv_point_precise_t beams[3][2] = {
        {{0, 0}, {-7, -9}}, {{4, 0}, {4, -11}}, {{8, 0}, {15, -9}},
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_line_create(parent);
        lv_line_set_points(b, beams[i], 2);
        lv_obj_set_pos(b, cx - 4, cy - 24);
        lv_obj_set_style_line_width(b, 3, 0);
        lv_obj_set_style_line_color(b, color, 0);
        lv_obj_set_style_line_rounded(b, true, 0);
    }
}

/* 小 Wi-Fi：三层同心圆弧 + 底部圆点 */
static void draw_wifi(lv_obj_t *parent, int cx, int bottom_y, lv_color_t color)
{
    static const int radii[3] = {13, 9, 5};
    for (int i = 0; i < 3; i++) {
        int r = radii[i];
        lv_obj_t *arc = lv_arc_create(parent);
        lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(arc, cx - r, bottom_y - r);
        lv_obj_set_size(arc, r * 2, r * 2);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 2, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, color, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_arc_set_rotation(arc, 0);
        lv_arc_set_bg_angles(arc, 225, 315);
        lv_arc_set_angles(arc, 0, 0);
        s_home_wifi[i] = arc;
    }
    s_home_wifi[3] = mk_circle(parent, cx, bottom_y, 5, color, LV_OPA_COVER);
}

static void anim_y_cb(void *obj, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)obj, value);
}

static void anim_x_cb(void *obj, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)obj, value);
}

static void anim_opa_cb(void *obj, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

static bool tap_is_drag(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    return indev != NULL && lv_indev_get_press_moved(indev);
}

/* -------------------------------------------------------------------------- */
/* 提示条                                                                      */
/* -------------------------------------------------------------------------- */

static void toast_hide_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    s_toast_timer = NULL;
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
}

static void toast_show(const char *text)
{
    if (s_toast == NULL) {
        return;
    }
    lv_label_set_text(s_toast_label, text);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_toast);
    lv_obj_fade_in(s_toast, 140, 0);

    if (s_toast_timer != NULL) {
        lv_timer_del(s_toast_timer);
        s_toast_timer = NULL;
    }
    s_toast_timer = lv_timer_create(toast_hide_cb, 1500, NULL);
    lv_timer_set_repeat_count(s_toast_timer, 1);
}

/* -------------------------------------------------------------------------- */
/* 亮度                                                                        */
/* -------------------------------------------------------------------------- */

static void brightness_apply(void)
{
    if (s_brightness < 10) {
        s_brightness = 10;
    }
    if (s_brightness > 100) {
        s_brightness = 100;
    }

    bsp_display_brightness_set(s_brightness);

    if (s_cc_bright_pct != NULL) {
        lv_label_set_text_fmt(s_cc_bright_pct, "%d%%", s_brightness);
    }
    if (s_cc_bright_arc != NULL) {
        lv_arc_set_angles(s_cc_bright_arc, 0, s_brightness * 360 / 100);
    }
    slider_refresh();
}

static void slider_refresh(void)
{
    if (s_slider == NULL) {
        return;
    }

    int v = (s_slider_kind == SLIDER_BRIGHT) ? s_brightness : s_volume;
    int h = SLIDER_TRACK_H * v / 100;

    if (s_slider_value != NULL) {
        lv_label_set_text_fmt(s_slider_value, "%d%%", v);
    }
    if (s_slider_fill != NULL) {
        lv_obj_set_y(s_slider_fill, SLIDER_TRACK_TOP + SLIDER_TRACK_H - h);
        lv_obj_set_height(s_slider_fill, h);
        if (h <= 0) {
            lv_obj_add_flag(s_slider_fill, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(s_slider_fill, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void slider_apply_value(int v)
{
    if (v < 0) {
        v = 0;
    }
    if (v > 100) {
        v = 100;
    }

    if (s_slider_kind == SLIDER_BRIGHT) {
        s_brightness = v;
        brightness_apply();
        return;
    }

    s_volume = v;
    if (s_cc_vol_pct != NULL) {
        lv_label_set_text_fmt(s_cc_vol_pct, "%d", s_volume);
    }
    slider_refresh();
}

/* 上下拖动：只有按在轨道上才生效，避免和右滑关闭互相误触 */
static void slider_press_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED) {
        s_slider_dragging = false;
        return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) {
        return;
    }
    s_slider_dragging = true;

    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    int y = p.y;
    if (y < SLIDER_TRACK_TOP) {
        y = SLIDER_TRACK_TOP;
    }
    if (y > SLIDER_TRACK_TOP + SLIDER_TRACK_H) {
        y = SLIDER_TRACK_TOP + SLIDER_TRACK_H;
    }
    slider_apply_value((SLIDER_TRACK_TOP + SLIDER_TRACK_H - y) * 100 / SLIDER_TRACK_H);
}

/* -------------------------------------------------------------------------- */
/* 电池 / 充电模式                                                             */
/* -------------------------------------------------------------------------- */

static void battery_visuals(void)
{
    int pct = s_bat_percent < 0 ? 0 : (s_bat_percent > 100 ? 100 : s_bat_percent);
    /* 普通模式电量弧红色；充电时整段变天蓝，并带一圈柔光。 */
    lv_color_t level = s_charging ? COL_RING_SKY : COL_BATT_RED;

    char text[24];
    if (s_bat_percent < 0) {
        snprintf(text, sizeof(text), "--");
    } else {
        snprintf(text, sizeof(text), "%d%%", s_bat_percent);
    }

    if (s_batt_pct != NULL) {
        lv_label_set_text(s_batt_pct, text);
        lv_obj_set_style_text_color(s_batt_pct, level, 0);
    }
    if (s_batt_arc != NULL) {
        int to = BATT_FROM_DEG + (BATT_TO_DEG - BATT_FROM_DEG) * pct / 100;
        lv_arc_set_angles(s_batt_arc, BATT_FROM_DEG, to);
        lv_obj_set_style_arc_color(s_batt_arc, level, LV_PART_INDICATOR);
    }
    if (s_batt_glow != NULL) {
        if (s_charging) {
            int to = BATT_FROM_DEG + (BATT_TO_DEG - BATT_FROM_DEG) * pct / 100;
            lv_arc_set_angles(s_batt_glow, BATT_FROM_DEG, to);
            lv_obj_set_style_arc_color(s_batt_glow, COL_RING_SKY, LV_PART_INDICATOR);
            lv_obj_remove_flag(s_batt_glow, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_batt_glow, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_batt_bolt != NULL) {
        /* 小闪电常驻，代表“电量”；充电时随弧线一起变青柠色 */
        lv_obj_set_style_line_color(s_batt_bolt, level, 0);
    }
    if (s_cc_bat_pct != NULL) {
        lv_label_set_text(s_cc_bat_pct, text);
        lv_obj_set_style_text_color(s_cc_bat_pct, s_charging ? COL_RING_SKY : COL_BATT_RED, 0);
    }
    if (s_cc_bat_arc != NULL) {
        lv_arc_set_angles(s_cc_bat_arc, 0, pct * 360 / 100);
        lv_obj_set_style_arc_color(s_cc_bat_arc, level, LV_PART_INDICATOR);
    }
    if (s_cc_bat_bolt != NULL) {
        if (s_charging) {
            lv_obj_remove_flag(s_cc_bat_bolt, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_cc_bat_bolt, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_status_bat != NULL) {
        if (s_charging) {
            if (s_bat_percent >= 100) {
                lv_label_set_text(s_status_bat, "已充满");
            } else if (s_bat_percent < 0) {
                lv_label_set_text(s_status_bat, "充电中");
            } else {
                lv_label_set_text_fmt(s_status_bat, "充电中 %d%%", s_bat_percent);
            }
            lv_obj_set_style_text_color(s_status_bat, COL_RING_SKY, 0);
        } else {
            lv_label_set_text(s_status_bat, text);
            lv_obj_set_style_text_color(s_status_bat, COL_BATT_RED, 0);
        }
    }
}

void ui_update_battery(int percent, bool usb_powered)
{
    if (s_power_override >= 0) {
        usb_powered = (s_power_override == 1);
    }
    s_bat_percent = percent;
    s_charging = usb_powered;

    battery_visuals();
}

static void battery_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    ui_update_battery(power_battery_percent(), power_is_vbus());
}

/* -------------------------------------------------------------------------- */
/* 时间 / 日期                                                                 */
/* -------------------------------------------------------------------------- */

static void time_tick_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    struct tm tm_now;
    time_t now = time(NULL);
    localtime_r(&now, &tm_now);

    int h, m, day, wd, mon;
    bool valid = (tm_now.tm_year + 1900) >= 2024;

    if (valid) {
        h = tm_now.tm_hour;
        m = tm_now.tm_min;
        day = tm_now.tm_mday;
        mon = tm_now.tm_mon + 1;
        wd = tm_now.tm_wday;
    } else {
        s_fb_seconds++;
        if (s_fb_seconds >= 60) {
            s_fb_seconds = 0;
            s_fb_minutes = (s_fb_minutes + 1) % (24 * 60);
        }
        h = s_fb_minutes / 60;
        m = s_fb_minutes % 60;
        day = 16;
        mon = 11;
        wd = 1;
    }

    if (h != s_last_h || m != s_last_m) {
        if (s_time != NULL) {
            lv_label_set_text_fmt(s_time, "%02d:%02d", h, m);
        }
        s_last_h = h;
        s_last_m = m;
    }

    if (day != s_last_day && s_date != NULL) {
        lv_label_set_text_fmt(s_date, "%d月%d日 %s", mon, day, s_week_cn[wd]);
        s_last_day = day;
    }
}

/* -------------------------------------------------------------------------- */
/* 网络                                                                        */
/* -------------------------------------------------------------------------- */

static const char *wifi_state_cn(epw_wifi_state_t state)
{
    switch (state) {
    case EPW_WIFI_CONNECTED:
        return "已连接";
    case EPW_WIFI_CONNECTING:
        return "连接中";
    case EPW_WIFI_FAILED:
        return "未连接";
    case EPW_WIFI_OFF:
    default:
        return "--";
    }
}

static void network_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (s_status_net == NULL && s_home_wifi[0] == NULL && s_status_ip == NULL) {
        return;
    }

    epw_wifi_state_t state = wifi_get_state();

    lv_color_t color = COL_MUTED;
    if (state == EPW_WIFI_CONNECTED) {
        color = COL_TEAL;
    } else if (state == EPW_WIFI_CONNECTING) {
        color = COL_AMBER;
    } else if (state == EPW_WIFI_FAILED) {
        color = COL_CORAL;
    }

    if (s_status_net != NULL) {
        lv_label_set_text(s_status_net, wifi_state_cn(state));
        lv_obj_set_style_text_color(s_status_net, color, 0);
    }
    /* 表盘上不写文字，只用 Wi-Fi 图标颜色表达：青=已连接，琥珀=连接中，珊瑚=断开 */
    for (int i = 0; i < 3; i++) {
        if (s_home_wifi[i] != NULL) {
            lv_obj_set_style_arc_color(s_home_wifi[i], color, LV_PART_MAIN);
        }
    }
    if (s_home_wifi[3] != NULL) {
        lv_obj_set_style_bg_color(s_home_wifi[3], color, 0);
    }
    if (s_status_ip != NULL) {
        lv_label_set_text(s_status_ip, wifi_get_ip_str());
        lv_obj_set_style_text_color(s_status_ip,
                                   state == EPW_WIFI_CONNECTED ? COL_TEAL : COL_MUTED, 0);
    }
}

/* -------------------------------------------------------------------------- */
/* 控制中心 / 状态页 滑入滑出                                                  */
/* -------------------------------------------------------------------------- */

static void center_hide_done(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    if (!s_center_open) {
        lv_obj_add_flag(s_center, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(s_center, 0);
    }
}

static void center_open(void)
{
    if (s_center == NULL || s_center_open) {
        return;
    }
    /* 打开控制中心前收起应用桌面 / 宠物页，避免页面叠加 */
    apps_close_now();
    pet_close_now();

    s_center_open = true;

    lv_anim_delete(s_center, NULL);
    lv_obj_set_y(s_center, -SCREEN_H);
    lv_obj_remove_flag(s_center, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_center);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_center);
    lv_anim_set_values(&a, -SCREEN_H, 0);
    lv_anim_set_duration(&a, 300);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_start(&a);
    ESP_LOGI(TAG, "center open");
}

static void center_close(void)
{
    if (s_center == NULL || !s_center_open) {
        return;
    }
    if (s_torch_open) {
        torch_close();
        return;
    }
    if (s_slider_open) {
        slider_close();
        return;
    }
    if (s_status_open) {
        status_close();
        return;
    }
    s_center_open = false;

    lv_anim_delete(s_center, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_center);
    lv_anim_set_values(&a, 0, -SCREEN_H);
    lv_anim_set_duration(&a, 240);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_set_completed_cb(&a, center_hide_done);
    lv_anim_start(&a);
    ESP_LOGI(TAG, "center close");
}

static void center_close_now(void)
{
    status_close_now();
    slider_close_now();
    torch_close_now();
    if (s_center == NULL) {
        return;
    }
    s_center_open = false;
    lv_anim_delete(s_center, NULL);
    lv_obj_add_flag(s_center, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(s_center, 0);
}

static void status_hide_done(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    if (!s_status_open) {
        lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(s_status, 0);
    }
}

static void status_open(void)
{
    if (s_status == NULL || s_status_open) {
        return;
    }
    s_status_open = true;

    lv_anim_delete(s_status, NULL);
    lv_obj_set_x(s_status, SCREEN_W);
    lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_status);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_status);
    lv_anim_set_values(&a, SCREEN_W, 0);
    lv_anim_set_duration(&a, 280);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_start(&a);
    ESP_LOGI(TAG, "status open");
}

static void status_close(void)
{
    if (s_status == NULL || !s_status_open) {
        return;
    }
    s_status_open = false;

    lv_anim_delete(s_status, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_status);
    lv_anim_set_values(&a, 0, SCREEN_W);
    lv_anim_set_duration(&a, 240);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_completed_cb(&a, status_hide_done);
    lv_anim_start(&a);
    ESP_LOGI(TAG, "status close");
}

static void status_close_now(void)
{
    if (s_status == NULL) {
        return;
    }
    s_status_open = false;
    lv_anim_delete(s_status, NULL);
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(s_status, 0);
}

/* -------------------------------------------------------------------------- */
/* 亮度 / 音量调节页                                                           */
/* -------------------------------------------------------------------------- */

static void slider_hide_done(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    if (!s_slider_open) {
        lv_obj_add_flag(s_slider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(s_slider, 0);
    }
}

static void slider_open(int kind)
{
    if (s_slider == NULL || s_slider_open) {
        return;
    }
    s_slider_kind = kind;
    s_slider_open = true;

    /* 亮度 / 音量统一绿色，页面上只保留当前值和 0-100 刻度 */
    lv_color_t accent = COL_GREEN;
    lv_obj_set_style_bg_color(s_slider_fill, accent, 0);
    lv_obj_set_style_bg_color(s_slider_track, accent, 0);
    lv_obj_set_style_bg_opa(s_slider_track, LV_OPA_30, 0);
    lv_obj_set_style_text_color(s_slider_value, accent, 0);

    lv_anim_delete(s_slider, NULL);
    lv_obj_set_x(s_slider, SCREEN_W);
    lv_obj_remove_flag(s_slider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_slider);
    slider_refresh();

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_slider);
    lv_anim_set_values(&a, SCREEN_W, 0);
    lv_anim_set_duration(&a, 280);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_start(&a);
    ESP_LOGI(TAG, "slider open kind=%d", kind);
}

static void slider_close(void)
{
    if (s_slider == NULL || !s_slider_open) {
        return;
    }
    s_slider_open = false;

    lv_anim_delete(s_slider, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_slider);
    lv_anim_set_values(&a, 0, SCREEN_W);
    lv_anim_set_duration(&a, 240);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_completed_cb(&a, slider_hide_done);
    lv_anim_start(&a);
    ESP_LOGI(TAG, "slider close");
}

static void slider_close_now(void)
{
    if (s_slider == NULL) {
        return;
    }
    s_slider_open = false;
    lv_anim_delete(s_slider, NULL);
    lv_obj_add_flag(s_slider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(s_slider, 0);
}

/* -------------------------------------------------------------------------- */
/* 手电筒                                                                      */
/* -------------------------------------------------------------------------- */

static void torch_hint_hide_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    s_torch_hint_timer = NULL;
    if (s_torch_hint != NULL) {
        lv_obj_add_flag(s_torch_hint, LV_OBJ_FLAG_HIDDEN);
    }
}

static void torch_open(void)
{
    if (s_torch == NULL || s_torch_open) {
        return;
    }
    s_torch_open = true;
    s_torch_last_click = 0;

    /* 手电就是纯白满亮度；关闭时恢复到进入前的亮度 */
    bsp_display_brightness_set(100);
    lv_obj_remove_flag(s_torch, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_torch);

    lv_obj_remove_flag(s_torch_hint, LV_OBJ_FLAG_HIDDEN);
    if (s_torch_hint_timer != NULL) {
        lv_timer_del(s_torch_hint_timer);
        s_torch_hint_timer = NULL;
    }
    s_torch_hint_timer = lv_timer_create(torch_hint_hide_cb, 2600, NULL);
    lv_timer_set_repeat_count(s_torch_hint_timer, 1);
    ESP_LOGI(TAG, "torch open");
}

static void torch_close(void)
{
    if (s_torch == NULL || !s_torch_open) {
        return;
    }
    s_torch_open = false;
    lv_obj_add_flag(s_torch, LV_OBJ_FLAG_HIDDEN);
    if (s_torch_hint_timer != NULL) {
        lv_timer_del(s_torch_hint_timer);
        s_torch_hint_timer = NULL;
    }
    bsp_display_brightness_set(s_brightness);
    ESP_LOGI(TAG, "torch close");
}

static void torch_close_now(void)
{
    if (s_torch == NULL) {
        return;
    }
    bool was_open = s_torch_open;
    s_torch_open = false;
    lv_obj_add_flag(s_torch, LV_OBJ_FLAG_HIDDEN);
    if (s_torch_hint_timer != NULL) {
        lv_timer_del(s_torch_hint_timer);
        s_torch_hint_timer = NULL;
    }
    if (was_open) {
        bsp_display_brightness_set(s_brightness);
    }
}

/* 白屏双击退出：两次 CLICKED 间隔 < 450ms */
static void torch_click_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    uint32_t now = lv_tick_get();
    if (s_torch_last_click != 0u && (uint32_t)(now - s_torch_last_click) <= 450u) {
        s_torch_last_click = 0;
        torch_close();
        return;
    }
    s_torch_last_click = now;
}

/* -------------------------------------------------------------------------- */
/* 应用桌面 / 宠物页 页面切换                                                   */
/* -------------------------------------------------------------------------- */

static void apps_hide_done(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    if (!s_apps_open) {
        lv_obj_add_flag(s_apps, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(s_apps, 0);
        lv_obj_set_style_opa(s_apps, LV_OPA_COVER, 0);
    }
}

static void apps_open(void)
{
    if (s_apps == NULL || s_apps_open) {
        return;
    }
    /* 应用桌面是最底层覆盖页：先把控制中心 / 状态页 / 宠物页收起 */
    center_close_now();
    status_close_now();
    pet_close_now();

    s_apps_open = true;
    lv_anim_delete(s_apps, NULL);
    lv_obj_remove_flag(s_apps, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_apps);
    lv_obj_set_style_opa(s_apps, LV_OPA_TRANSP, 0);
    lv_obj_set_y(s_apps, 20);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_apps);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, 180);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_apps);
    lv_anim_set_values(&a, 20, 0);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_start(&a);
    ESP_LOGI(TAG, "apps open");
}

static void apps_close(void)
{
    if (s_apps == NULL || !s_apps_open) {
        return;
    }
    s_apps_open = false;

    lv_anim_delete(s_apps, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_apps);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, 160);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_apps);
    lv_anim_set_values(&a, 0, 20);
    lv_anim_set_duration(&a, 160);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_set_completed_cb(&a, apps_hide_done);
    lv_anim_start(&a);
    ESP_LOGI(TAG, "apps close");
}

static void apps_close_now(void)
{
    if (s_apps == NULL) {
        return;
    }
    s_apps_open = false;
    lv_anim_delete(s_apps, NULL);
    lv_obj_add_flag(s_apps, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(s_apps, 0);
    lv_obj_set_style_opa(s_apps, LV_OPA_COVER, 0);
}

static void pet_open(void)
{
    if (s_pet == NULL || s_pet_open) {
        return;
    }
    /* 宠物页为最上层覆盖页：先收起控制中心 / 状态页 / 应用桌面 */
    center_close_now();
    status_close_now();
    apps_close_now();

    s_pet_open = true;
    lv_anim_delete(s_pet, NULL);
    /* 直接显示：整页 466x466 淡入会连续重绘全屏，是进入迟钝的主因 */
    lv_obj_set_style_opa(s_pet, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_pet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_pet);

    s_pet_pop = 40;
    s_pet_last_bob = -10000; /* 让下一帧必定写入 y */
    s_pet_long_pressed = false;
    ESP_LOGI(TAG, "pet open");
}

static void pet_close(void)
{
    if (s_pet == NULL || !s_pet_open) {
        return;
    }
    s_pet_open = false;

    lv_anim_delete(s_pet, NULL);
    lv_obj_add_flag(s_pet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s_pet, LV_OPA_COVER, 0);
    ESP_LOGI(TAG, "pet close");
}

static void pet_close_now(void)
{
    if (s_pet == NULL) {
        return;
    }
    s_pet_open = false;
    lv_anim_delete(s_pet, NULL);
    lv_obj_add_flag(s_pet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s_pet, LV_OPA_COVER, 0);
}

/* -------------------------------------------------------------------------- */
/* 磁贴内容绘制                                                                */
/* -------------------------------------------------------------------------- */

static lv_obj_t *tile_create(lv_obj_t *parent, int cx, int cy, const char *label,
                             lv_color_t accent, int track_deg, int prog_deg)
{
    lv_obj_t *root = mk_circle(parent, cx, cy, TILE_D, COL_SURF, LV_OPA_COVER);
    lv_obj_set_style_border_width(root, 1, 0);
    lv_obj_set_style_border_color(root, COL_LINE, 0);
    lv_obj_set_style_border_opa(root, LV_OPA_70, 0);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);

    /* 磁贴柔光：参考华为小组件的发光质感 */
    lv_obj_set_style_shadow_width(root, 20, 0);
    lv_obj_set_style_shadow_color(root, accent, 0);
    lv_obj_set_style_shadow_opa(root, LV_OPA_20, 0);

    lv_obj_t *arc = mk_arc(root, TILE_D / 2, TILE_D / 2, TILE_D, 4,
                           COL_LINE, LV_OPA_50, track_deg,
                           accent, LV_OPA_COVER, prog_deg);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    if (prog_deg > 0) {
        lv_obj_t *dot = mk_circle(root, 0, 0, TILE_DOT_D, accent, LV_OPA_COVER);
        dot_place(dot, TILE_D / 2, TILE_D / 2, TILE_D, 4, prog_deg);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    }

    mk_label(root, label, &epw_cjk_16, COL_MUTED, 0, 14, TILE_D, LV_TEXT_ALIGN_CENTER);
    return root;
}

/* -------------------------------------------------------------------------- */
/* 事件                                                                        */
/* -------------------------------------------------------------------------- */

/* 表盘下滑：打开控制中心（iWatch / 华为手表交互；表盘不再响应点按切页） */
static void dial_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
        return;
    }
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        return;
    }
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM) {
        center_open();
    }
}

static void center_status_cb(lv_event_t *e)
{
    if (tap_is_drag(e)) {
        return;
    }
    status_open();
}

static void center_weather_cb(lv_event_t *e)
{
    if (tap_is_drag(e)) {
        return;
    }
    toast_show("天气 24°");
}

static void center_battery_cb(lv_event_t *e)
{
    if (tap_is_drag(e)) {
        return;
    }
    status_open();
}

static void center_bright_cb(lv_event_t *e)
{
    if (tap_is_drag(e)) {
        return;
    }
    slider_open(SLIDER_BRIGHT);
}

static void center_volume_cb(lv_event_t *e)
{
    if (tap_is_drag(e)) {
        return;
    }
    slider_open(SLIDER_VOLUME);
}

/* 控制中心手电：整屏纯白 + 最大亮度 */
static void center_torch_cb(lv_event_t *e)
{
    if (tap_is_drag(e)) {
        return;
    }
    torch_open();
}

/* 控制中心宠物磁贴：点按进入完整宠物页（表情 / 说话 / 思考由情景引擎驱动） */
static void center_ai_cb(lv_event_t *e)
{
    if (tap_is_drag(e)) {
        return;
    }
    ui_pet_set_state(UI_PET_HAPPY, NULL);
}

static void center_close_cb(lv_event_t *e)
{
    if (tap_is_drag(e)) {
        return;
    }
    center_close();
}

static void status_back_cb(lv_event_t *e)
{
    if (tap_is_drag(e)) {
        return;
    }
    status_close();
}

static void center_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
        return;
    }
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        return;
    }
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) {
        center_close();
    }
}

static void status_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
        return;
    }
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        return;
    }
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) {
        status_close();
    }
}

static void slider_back_cb(lv_event_t *e)
{
    if (tap_is_drag(e)) {
        return;
    }
    slider_close();
}

/* 亮度 / 音量页：从左往右滑关闭，返回控制中心 */
static void slider_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
        return;
    }
    /* 从轨道上开始的滑动只调节数值，不关闭页面 */
    if (s_slider_dragging) {
        return;
    }
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        return;
    }
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) {
        slider_close();
    }
}

/* -------------------------------------------------------------------------- */
/* 表盘构建                                                                    */
/* -------------------------------------------------------------------------- */

static void build_dial(lv_obj_t *page)
{
    /* 外圈：亮黄 + 橙的暖色儿童科技环，常驻。三段之间留 4° 呼吸口，不做刻度。
     * 每段自带柔光与白色外侧高光，形成一点毛玻璃反光。 */
    mk_ring_seg(page, 2, 118, COL_RING_YELLOW);
    mk_ring_seg(page, 122, 238, COL_RING_ORANGE);
    mk_ring_seg(page, 242, 358, COL_RING_SKY);

    /* 底部电量弧：贴着外圈向里收一圈，闪电 + 百分比就是电量语义。
     * 普通模式固定红色，充电时整段变青柠并带辉光。 */
    s_batt_glow = mk_arc_prog(page, CENTER_X, CENTER_Y, BATT_SIZE, BATT_WIDTH * 2,
                              BATT_FROM_DEG, BATT_TO_DEG, COL_LIME, LV_OPA_20);
    lv_obj_add_flag(s_batt_glow, LV_OBJ_FLAG_HIDDEN);
    s_batt_track = mk_arc_seg(page, CENTER_X, CENTER_Y, BATT_SIZE, BATT_WIDTH,
                              BATT_FROM_DEG, BATT_TO_DEG, COL_LINE, LV_OPA_60);
    s_batt_arc = mk_arc_prog(page, CENTER_X, CENTER_Y, BATT_SIZE, BATT_WIDTH,
                             BATT_FROM_DEG, BATT_TO_DEG, COL_BATT_RED, LV_OPA_COVER);

    static const lv_point_precise_t bolt_pts[] = {
        {6, 0}, {1, 8}, {5, 8}, {2, 16}, {11, 6}, {6, 6}, {6, 0},
    };
    s_batt_bolt = lv_line_create(page);
    lv_line_set_points(s_batt_bolt, bolt_pts, 7);
    lv_obj_set_pos(s_batt_bolt, 194, 371);
    lv_obj_set_style_line_width(s_batt_bolt, 2, 0);
    lv_obj_set_style_line_color(s_batt_bolt, COL_BATT_RED, 0);
    lv_obj_set_style_line_rounded(s_batt_bolt, true, 0);

    s_batt_pct = mk_label(page, "94%", &epw_cjk_20, COL_BATT_RED,
                          203, 368, 60, LV_TEXT_ALIGN_CENTER);

    /* 顶部天气：绿色分割弧 + 太阳 + 温度 + 天气词，贴近上沿 */
    mk_arc_seg(page, CENTER_X, CENTER_Y, 402, 3, 338, 382, COL_GREEN, LV_OPA_80);
    lv_obj_t *sun = mk_sun_icon(page, 22, COL_AMBER);
    lv_obj_set_pos(sun, 184, 44);
    mk_label(page, "24°", &epw_cjk_20, COL_TEXT, 212, 38, 56, LV_TEXT_ALIGN_LEFT);
    mk_label(page, "晴", &epw_cjk_16, COL_AMBER, 266, 42, 24, LV_TEXT_ALIGN_LEFT);

    /* Wi-Fi 状态：只保留图标，用颜色表达（青=在线 / 琥珀=连接中 / 珊瑚=断开） */
    draw_wifi(page, 372, 102, COL_TEAL);

    /* 中央：超大号圆润数字时间，垂直居中并整体下移，不再顶在上沿 */
    s_time = mk_label(page, "10:09", &epw_time_150, COL_TEXT,
                      0, 176, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_letter_space(s_time, 1, 0);

    /* 中文日期 + 星期：20px，位于时间下方留白里 */
    s_date = mk_label(page, "11月16日 周一", &epw_cjk_20,
                      lv_color_hex(0xD6E2F0), 0, 300, SCREEN_W, LV_TEXT_ALIGN_CENTER);

    /* 表盘下滑打开控制中心（iWatch / 华为手表交互） */
    lv_obj_add_event_cb(page, dial_event_cb, LV_EVENT_GESTURE, NULL);
}

/* -------------------------------------------------------------------------- */
/* 控制中心构建                                                                */
/* -------------------------------------------------------------------------- */

static void build_center(void)
{
    s_center = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_center);
    lv_obj_remove_flag(s_center, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_center, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_center, 0, 0);
    lv_obj_set_style_bg_color(s_center, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_center, LV_OPA_COVER, 0);
    /* 自己收手势，子磁贴保留默认冒泡 */
    lv_obj_remove_flag(s_center, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_center, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_center, center_event_cb, LV_EVENT_GESTURE, NULL);

    mk_label(s_center, "控制中心", &epw_cjk_20, COL_TEXT,
             0, 48, SCREEN_W, LV_TEXT_ALIGN_CENTER);

    /* 4 + 3 磁贴：第一行四个功能，第二行三个调节项，给手电留出位置 */
    const int cols4[4] = {74, 180, 286, 392};
    const int cols3[3] = {128, 233, 338};
    const int row_top = 168;
    const int row_bottom = 300;

    /* 状态 */
    lv_obj_t *t_status = tile_create(s_center, cols4[0], row_top, "状态",
                                     COL_TEAL, 270, 270);
    lv_obj_add_event_cb(t_status, center_status_cb, LV_EVENT_CLICKED, NULL);
    draw_info(t_status, TILE_D / 2, TILE_D / 2 + 6, COL_TEAL);

    /* 天气 */
    lv_obj_t *t_weather = tile_create(s_center, cols4[1], row_top, "天气",
                                      COL_BLUE, 270, 270);
    lv_obj_add_event_cb(t_weather, center_weather_cb, LV_EVENT_CLICKED, NULL);
    draw_sun(t_weather, TILE_D / 2, TILE_D / 2 - 6, 8, COL_AMBER);
    mk_label(t_weather, "24°", &epw_cjk_20, COL_TEXT,
             0, 54, TILE_D, LV_TEXT_ALIGN_CENTER);

    /* 电量 */
    lv_obj_t *t_bat = tile_create(s_center, cols4[2], row_top, "电量",
                                  COL_AMBER, 360, 0);
    lv_obj_add_event_cb(t_bat, center_battery_cb, LV_EVENT_CLICKED, NULL);
    s_cc_bat_arc = mk_arc(t_bat, TILE_D / 2, TILE_D / 2, TILE_D, 4,
                          COL_LINE, LV_OPA_60, 360, COL_AMBER, LV_OPA_COVER, 0);
    s_cc_bat_pct = mk_label(t_bat, "86%", &lv_font_montserrat_24, COL_TEXT,
                            0, 40, TILE_D, LV_TEXT_ALIGN_CENTER);
    static const lv_point_precise_t cc_bolt[] = {
        {7, 0}, {1, 10}, {6, 10}, {3, 19}, {12, 7}, {7, 7}, {7, 0},
    };
    s_cc_bat_bolt = lv_line_create(t_bat);
    lv_line_set_points(s_cc_bat_bolt, cc_bolt, 7);
    lv_obj_set_pos(s_cc_bat_bolt, 54, 20);
    lv_obj_set_style_line_width(s_cc_bat_bolt, 3, 0);
    lv_obj_set_style_line_color(s_cc_bat_bolt, COL_LIME, 0);
    lv_obj_set_style_line_rounded(s_cc_bat_bolt, true, 0);
    lv_obj_add_flag(s_cc_bat_bolt, LV_OBJ_FLAG_HIDDEN);

    /* 手电：整屏纯白 + 最大亮度，双击白屏退出 */
    lv_obj_t *t_torch = tile_create(s_center, cols4[3], row_top, "手电",
                                    COL_RING_YELLOW, 270, 270);
    lv_obj_add_event_cb(t_torch, center_torch_cb, LV_EVENT_CLICKED, NULL);
    draw_torch(t_torch, TILE_D / 2, TILE_D / 2 + 8, COL_RING_YELLOW);

    /* 亮度：点开独立调节页，上下拖动 */
    lv_obj_t *t_bright = tile_create(s_center, cols3[0], row_bottom, "亮度",
                                     COL_PURPLE, 360, 0);
    lv_obj_add_event_cb(t_bright, center_bright_cb, LV_EVENT_CLICKED, NULL);
    draw_sun(t_bright, TILE_D / 2, TILE_D / 2 - 12, 6, COL_AMBER);
    s_cc_bright_pct = mk_label(t_bright, "80%", &lv_font_montserrat_20, COL_TEXT,
                               0, 50, TILE_D, LV_TEXT_ALIGN_CENTER);
    s_cc_bright_arc = mk_arc(t_bright, TILE_D / 2, TILE_D / 2, TILE_D, 4,
                             COL_LINE, LV_OPA_60, 360, COL_PURPLE, LV_OPA_COVER, 0);
    lv_arc_set_angles(s_cc_bright_arc, 0, s_brightness * 360 / 100);

    /* 音量：点开独立调节页，上下拖动（音频未初始化，仅界面演示） */
    lv_obj_t *t_vol = tile_create(s_center, cols3[1], row_bottom, "音量",
                                  COL_PINK, 270, 270);
    lv_obj_add_event_cb(t_vol, center_volume_cb, LV_EVENT_CLICKED, NULL);
    draw_speaker(t_vol, 28, 28, COL_PINK);
    s_cc_vol_pct = mk_label(t_vol, "0", &lv_font_montserrat_20, COL_TEXT,
                            0, 54, TILE_D, LV_TEXT_ALIGN_CENTER);

    /* 宠物（Pip 3D 正脸） */
    lv_obj_t *t_ai = tile_create(s_center, cols3[2], row_bottom, "AI",
                                 COL_AMBER, 270, 270);
    lv_obj_add_event_cb(t_ai, center_ai_cb, LV_EVENT_CLICKED, NULL);
    s_ai_img = lv_image_create(t_ai);
    lv_image_set_src(s_ai_img, &pip_face_happy);
    lv_image_set_scale(s_ai_img, 66);
    lv_obj_remove_flag(s_ai_img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_ai_img, LV_ALIGN_CENTER, 0, 6);

    /* 收起 */
    lv_obj_t *close = mk_box(s_center, 167, 396, 132, 34, COL_SURF, LV_OPA_COVER, 17);
    lv_obj_set_style_border_width(close, 1, 0);
    lv_obj_set_style_border_color(close, COL_LINE, 0);
    lv_obj_add_flag(close, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(close, center_close_cb, LV_EVENT_CLICKED, NULL);
    mk_label(close, "上滑收起", &epw_cjk_16, COL_MUTED,
             0, 7, 132, LV_TEXT_ALIGN_CENTER);

    lv_obj_add_flag(s_center, LV_OBJ_FLAG_HIDDEN);
}

/* -------------------------------------------------------------------------- */
/* 状态页构建                                                                  */
/* -------------------------------------------------------------------------- */

static void add_status_row(lv_obj_t *parent, int y, const char *name,
                           const char *value, lv_color_t value_color,
                           lv_color_t dot_color, lv_obj_t **value_out)
{
    mk_circle(parent, 92, y + 8, 6, dot_color, LV_OPA_COVER);
    mk_label(parent, name, &epw_cjk_16, COL_MUTED, 106, y, 130, LV_TEXT_ALIGN_LEFT);
    lv_obj_t *v = mk_label(parent, value, &epw_cjk_16, value_color,
                           236, y, 160, LV_TEXT_ALIGN_RIGHT);
    if (value_out != NULL) {
        *value_out = v;
    }
}

static void build_status(void)
{
    s_status = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_status);
    lv_obj_remove_flag(s_status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_status, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_status, 0, 0);
    lv_obj_set_style_bg_color(s_status, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_status, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_status, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_status, status_event_cb, LV_EVENT_GESTURE, NULL);

    mk_label(s_status, "状态", &epw_cjk_20, COL_TEXT,
             0, 44, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    mk_label(s_status, "ESP32-S3 · 1.75\" AMOLED · 实时读取", &epw_cjk_16, COL_MUTED,
             0, 72, SCREEN_W, LV_TEXT_ALIGN_CENTER);

    /* 返回圆钮（主要关闭方式是从左往右滑） */
    lv_obj_t *back = mk_circle(s_status, 54, 60, 38, COL_SURF, LV_OPA_COVER);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_color(back, COL_LINE, 0);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, status_back_cb, LV_EVENT_CLICKED, NULL);
    static const lv_point_precise_t chev[] = {{21, 8}, {11, 19}, {21, 30}};
    lv_obj_t *l = lv_line_create(back);
    lv_line_set_points(l, chev, 3);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, 3, 0);
    lv_obj_set_style_line_color(l, COL_MUTED, 0);
    lv_obj_set_style_line_rounded(l, true, 0);

    /* 信息卡：圆角面板，延续表盘的彩色小图标语言（亮度调节已移到控制中心独立页） */
    lv_obj_t *card = mk_box(s_status, 66, 104, 334, 300, COL_SURF, LV_OPA_COVER, 30);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, COL_LINE, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_60, 0);

    /* 硬件信息（运行时读取） */
    char flash_txt[24] = "--";
    char ram_txt[24] = "--";
    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK && flash_size > 0) {
        snprintf(flash_txt, sizeof(flash_txt), "%u MB",
                 (unsigned)(flash_size / (1024u * 1024u)));
    }
    size_t psram_size = esp_psram_get_size();
    if (psram_size == 0) {
        psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    }
    if (psram_size > 0) {
        snprintf(ram_txt, sizeof(ram_txt), "%u MB PSRAM",
                 (unsigned)(psram_size / (1024u * 1024u)));
    }

    static const char *names[] = {
        "显示", "触摸", "电池", "音频", "网络", "IP", "芯片", "闪存", "内存",
    };
    static const char *values[] = {
        "466 x 466", "CST9217", "--", "静音", "未连接", "--",
        "ESP32-S3", "", "",
    };
    const lv_color_t dots[] = {
        COL_BLUE, COL_PURPLE, COL_TEAL, COL_CORAL, COL_BLUE,
        COL_TEAL, COL_PURPLE, COL_AMBER, COL_PINK,
    };
    const lv_color_t vals[] = {
        COL_BLUE, COL_PURPLE, COL_TEXT, COL_CORAL, COL_TEXT,
        COL_TEAL, COL_PURPLE, COL_AMBER, COL_PINK,
    };

    const int row_top = 106;
    const int row_h = 24;
    for (int i = 0; i < 9; i++) {
        int y = row_top + i * row_h;
        const char *val = values[i];
        if (i == 7) {
            val = flash_txt;
        } else if (i == 8) {
            val = ram_txt;
        }

        lv_obj_t *v = NULL;
        add_status_row(s_status, y, names[i], val, vals[i], dots[i], &v);
        if (i == 2) {
            s_status_bat = v;
        } else if (i == 4) {
            s_status_net = v;
        } else if (i == 5) {
            s_status_ip = v;
        }
        if (i < 8) {
            mk_box(s_status, 106, y + row_h - 1, 274, 1, COL_LINE, LV_OPA_40, 0);
        }
    }

    /* 亮度调节已移到控制中心的独立页；状态页只保留只读硬件信息 */

    lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
}

/* -------------------------------------------------------------------------- */
/* 亮度 / 音量调节页构建                                                       */
/* -------------------------------------------------------------------------- */

static void build_slider(void)
{
    s_slider = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_slider);
    lv_obj_remove_flag(s_slider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_slider, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_slider, 0, 0);
    lv_obj_set_style_bg_color(s_slider, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_slider, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_slider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_slider, slider_event_cb, LV_EVENT_GESTURE, NULL);

    /* 返回圆钮（主要关闭方式是从左往右滑） */
    lv_obj_t *back = mk_circle(s_slider, 54, 60, 38, COL_SURF, LV_OPA_COVER);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_color(back, COL_LINE, 0);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, slider_back_cb, LV_EVENT_CLICKED, NULL);
    static const lv_point_precise_t chev[] = {{21, 8}, {11, 19}, {21, 30}};
    lv_obj_t *l = lv_line_create(back);
    lv_line_set_points(l, chev, 3);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, 3, 0);
    lv_obj_set_style_line_color(l, COL_MUTED, 0);
    lv_obj_set_style_line_rounded(l, true, 0);

    /* 只保留当前值；不写标题和操作说明 */
    s_slider_value = mk_label(s_slider, "80%", &lv_font_montserrat_28, COL_GREEN,
                              0, 52, SCREEN_W, LV_TEXT_ALIGN_CENTER);

    /* 竖向粗轨道：底部 0%，顶部 100%，必须按在轨道上才能拖动 */
    s_slider_track = mk_box(s_slider, SLIDER_TRACK_X, SLIDER_TRACK_TOP,
                            SLIDER_TRACK_W, SLIDER_TRACK_H,
                            COL_LINE, LV_OPA_60, SLIDER_TRACK_W / 2);
    lv_obj_add_event_cb(s_slider_track, slider_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_slider_track, slider_press_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_slider_track, slider_press_cb, LV_EVENT_RELEASED, NULL);

    s_slider_fill = mk_box(s_slider, SLIDER_TRACK_X, SLIDER_TRACK_TOP,
                           SLIDER_TRACK_W, SLIDER_TRACK_H,
                           COL_GREEN, LV_OPA_COVER, SLIDER_TRACK_W / 2);
    lv_obj_add_event_cb(s_slider_fill, slider_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_slider_fill, slider_press_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_slider_fill, slider_press_cb, LV_EVENT_RELEASED, NULL);

    mk_label(s_slider, "100", &epw_cjk_16, COL_MUTED,
             SLIDER_TRACK_X + SLIDER_TRACK_W + 14, SLIDER_TRACK_TOP - 8,
             42, LV_TEXT_ALIGN_LEFT);
    mk_label(s_slider, "0", &epw_cjk_16, COL_MUTED,
             SLIDER_TRACK_X + SLIDER_TRACK_W + 14, SLIDER_TRACK_TOP + SLIDER_TRACK_H - 10,
             42, LV_TEXT_ALIGN_LEFT);

    lv_obj_add_flag(s_slider, LV_OBJ_FLAG_HIDDEN);
}

/* -------------------------------------------------------------------------- */
/* 手电页构建                                                                  */
/* -------------------------------------------------------------------------- */

static void build_torch(void)
{
    s_torch = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_torch);
    lv_obj_remove_flag(s_torch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_torch, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_torch, 0, 0);
    lv_obj_set_style_bg_color(s_torch, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_torch, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_torch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_torch, torch_click_cb, LV_EVENT_CLICKED, NULL);

    /* 浅灰提示，2.6s 后自动消失；不抢点击 */
    s_torch_hint = mk_label(s_torch, "双击白屏退出", &epw_cjk_16,
                            lv_color_hex(0xBDBDBD), 0, 408,
                            SCREEN_W, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_flag(s_torch_hint, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(s_torch, LV_OBJ_FLAG_HIDDEN);
}

/* -------------------------------------------------------------------------- */
/* 应用桌面（Mac Launchpad 风格）                                               */
/* -------------------------------------------------------------------------- */

enum {
    APP_PET = 0,
    APP_CENTER,
    APP_STATUS,
    APP_WEATHER,
    APP_BRIGHT,
    APP_VOLUME,
};

#define APP_ROOT_W 120
#define APP_ROOT_H 132

static void app_slot_pos(int slot, lv_coord_t *x, lv_coord_t *y)
{
    *x = s_app_cx[slot % 3] - APP_ROOT_W / 2;
    *y = s_app_cy[slot / 3] - APP_ICON_D / 2;
}

/* NVS 持久化：6 个图标各自的槽位（0..5） */
static void apps_load_slots(void)
{
    for (int i = 0; i < APP_COUNT; i++) {
        s_app_slot[i] = i;
    }

    nvs_handle_t h;
    if (nvs_open("epw_apps", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t buf[APP_COUNT];
    size_t len = sizeof(buf);
    if (nvs_get_blob(h, "slots", buf, &len) == ESP_OK && len == APP_COUNT) {
        bool used[APP_COUNT] = {0};
        bool ok = true;
        for (int i = 0; i < APP_COUNT; i++) {
            if (buf[i] >= APP_COUNT || used[buf[i]]) {
                ok = false;
                break;
            }
            used[buf[i]] = true;
        }
        if (ok) {
            memcpy(s_app_slot, buf, APP_COUNT);
        }
    }
    nvs_close(h);
}

static void apps_save_slots(void)
{
    uint8_t buf[APP_COUNT];
    for (int i = 0; i < APP_COUNT; i++) {
        buf[i] = (uint8_t)s_app_slot[i];
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open("epw_apps", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs open failed: %s", esp_err_to_name(err));
        return;
    }
    if (nvs_set_blob(h, "slots", buf, sizeof(buf)) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}

static void app_icon_move_to(lv_obj_t *icon, lv_coord_t x, lv_coord_t y)
{
    lv_anim_delete(icon, NULL);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, icon);
    lv_anim_set_values(&a, lv_obj_get_x(icon), x);
    lv_anim_set_duration(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, icon);
    lv_anim_set_values(&a, lv_obj_get_y(icon), y);
    lv_anim_set_duration(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_start(&a);
}

static void apps_layout_icons(void)
{
    for (int i = 0; i < APP_COUNT; i++) {
        if (s_app_icons[i] == NULL) {
            continue;
        }
        lv_coord_t x, y;
        app_slot_pos(s_app_slot[i], &x, &y);
        app_icon_move_to(s_app_icons[i], x, y);
    }
}

static void app_open_index(int idx)
{
    switch (idx) {
    case APP_PET:
        ui_pet_set_state(UI_PET_HAPPY, NULL);
        break;
    case APP_CENTER:
        center_open();
        break;
    case APP_STATUS:
        status_open();
        break;
    case APP_WEATHER:
        toast_show("天气 24°");
        break;
    case APP_BRIGHT:
        slider_open(SLIDER_BRIGHT);
        break;
    case APP_VOLUME:
        slider_open(SLIDER_VOLUME);
        break;
    default:
        break;
    }
}

/* 松手吸附到最近的槽位；目标槽被占用时两个图标交换位置 */
static void apps_drop_icon(int idx)
{
    lv_obj_t *icon = s_app_icons[idx];
    int cx = lv_obj_get_x(icon) + APP_ROOT_W / 2;
    int cy = lv_obj_get_y(icon) + APP_ICON_D / 2;

    int best = 0;
    long best_d = 0x7fffffffL;
    for (int s = 0; s < APP_COUNT; s++) {
        int dx = cx - s_app_cx[s % 3];
        int dy = cy - s_app_cy[s / 3];
        long d = (long)dx * dx + (long)dy * dy;
        if (d < best_d) {
            best_d = d;
            best = s;
        }
    }

    int old = s_app_slot[idx];
    if (best != old) {
        int other = -1;
        for (int i = 0; i < APP_COUNT; i++) {
            if (i != idx && s_app_slot[i] == best) {
                other = i;
                break;
            }
        }
        s_app_slot[idx] = best;
        if (other >= 0) {
            s_app_slot[other] = old;
        }
        apps_save_slots();
    }
    apps_layout_icons();
}

static void app_icon_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *icon = (lv_obj_t *)lv_event_get_current_target(e);
    if (icon == NULL) {
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        if (s_app_drag_icon >= 0) {
            return;
        }
        s_app_drag_icon = idx;
        s_app_drag_moved = false;
        lv_indev_t *indev = lv_event_get_indev(e);
        if (indev != NULL) {
            lv_indev_get_point(indev, &s_app_drag_press);
        }
        s_app_drag_x0 = lv_obj_get_x(icon);
        s_app_drag_y0 = lv_obj_get_y(icon);
        lv_obj_move_foreground(icon);
    } else if (code == LV_EVENT_PRESSING) {
        if (s_app_drag_icon != idx) {
            return;
        }
        lv_indev_t *indev = lv_event_get_indev(e);
        if (indev == NULL) {
            return;
        }
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        int dx = p.x - s_app_drag_press.x;
        int dy = p.y - s_app_drag_press.y;
        if (abs(dx) > 8 || abs(dy) > 8) {
            s_app_drag_moved = true;
        }
        if (s_app_drag_moved) {
            lv_obj_set_pos(icon, s_app_drag_x0 + dx, s_app_drag_y0 + dy);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_app_drag_icon != idx) {
            return;
        }
        s_app_drag_icon = -1;
        if (code == LV_EVENT_RELEASED && !s_app_drag_moved) {
            app_open_index(idx);
        } else {
            apps_drop_icon(idx);
        }
    }
}

static void apps_back_cb(lv_event_t *e)
{
    if (tap_is_drag(e)) {
        return;
    }
    apps_close();
}

/* 应用桌面上滑：像 Launchpad 一样收起 */
static void apps_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
        return;
    }
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        return;
    }
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) {
        apps_close();
    }
}

static lv_obj_t *app_icon_create(lv_obj_t *parent, int idx, const char *label,
                                 lv_color_t accent)
{
    lv_obj_t *root = mk_box(parent, 0, 0, APP_ROOT_W, APP_ROOT_H,
                            COL_BG, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, app_icon_event_cb, LV_EVENT_PRESSED,
                        (void *)(intptr_t)idx);
    lv_obj_add_event_cb(root, app_icon_event_cb, LV_EVENT_PRESSING,
                        (void *)(intptr_t)idx);
    lv_obj_add_event_cb(root, app_icon_event_cb, LV_EVENT_RELEASED,
                        (void *)(intptr_t)idx);
    lv_obj_add_event_cb(root, app_icon_event_cb, LV_EVENT_PRESS_LOST,
                        (void *)(intptr_t)idx);

    lv_obj_t *disc = mk_circle(root, APP_ROOT_W / 2, APP_ICON_D / 2, APP_ICON_D,
                               COL_SURF_HI, LV_OPA_COVER);
    lv_obj_set_style_border_width(disc, 1, 0);
    lv_obj_set_style_border_color(disc, accent, 0);
    lv_obj_set_style_border_opa(disc, LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(disc, 18, 0);
    lv_obj_set_style_shadow_color(disc, accent, 0);
    lv_obj_set_style_shadow_opa(disc, LV_OPA_20, 0);

    mk_label(root, label, &epw_cjk_16, COL_TEXT,
             0, APP_ICON_D + 6, APP_ROOT_W, LV_TEXT_ALIGN_CENTER);
    return root;
}

static void build_apps(void)
{
    s_apps = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_apps);
    lv_obj_remove_flag(s_apps, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_apps, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_apps, 0, 0);
    lv_obj_set_style_bg_color(s_apps, lv_color_hex(0x060A10), 0);
    lv_obj_set_style_bg_grad_color(s_apps, lv_color_hex(0x111E2C), 0);
    lv_obj_set_style_bg_grad_dir(s_apps, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_apps, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_apps, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_apps, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_apps, apps_event_cb, LV_EVENT_GESTURE, NULL);

    mk_label(s_apps, "Pip 的桌面", &epw_cjk_20, COL_TEXT,
             0, 34, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    mk_label(s_apps, "点按打开 · 拖动换位", &epw_cjk_16, COL_MUTED,
             0, 62, SCREEN_W, LV_TEXT_ALIGN_CENTER);

    apps_load_slots();

    s_app_icons[APP_PET] = app_icon_create(s_apps, APP_PET, "宠物", COL_PINK);
    lv_obj_t *disc = lv_obj_get_child(s_app_icons[APP_PET], 0);
    lv_obj_t *img = lv_image_create(disc);
    lv_image_set_src(img, &pip_face_happy);
    lv_image_set_scale(img, 84);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    s_app_icons[APP_CENTER] = app_icon_create(s_apps, APP_CENTER, "控制中心", COL_TEAL);
    disc = lv_obj_get_child(s_app_icons[APP_CENTER], 0);
    for (int gy = 0; gy < 3; gy++) {
        for (int gx = 0; gx < 3; gx++) {
            mk_box(disc, 48 - 11 + gx * 8, 48 - 11 + gy * 8, 5, 5,
                   COL_TEAL, LV_OPA_COVER, 2);
        }
    }

    s_app_icons[APP_STATUS] = app_icon_create(s_apps, APP_STATUS, "状态", COL_BLUE);
    disc = lv_obj_get_child(s_app_icons[APP_STATUS], 0);
    draw_info(disc, 48, 42, COL_BLUE);

    s_app_icons[APP_WEATHER] = app_icon_create(s_apps, APP_WEATHER, "天气", COL_BLUE);
    disc = lv_obj_get_child(s_app_icons[APP_WEATHER], 0);
    draw_sun(disc, 48, 44, 10, COL_AMBER);

    s_app_icons[APP_BRIGHT] = app_icon_create(s_apps, APP_BRIGHT, "亮度", COL_AMBER);
    disc = lv_obj_get_child(s_app_icons[APP_BRIGHT], 0);
    draw_sun(disc, 48, 40, 7, COL_AMBER);
    mk_label(disc, "%", &lv_font_montserrat_20, COL_TEXT,
             0, 58, APP_ICON_D, LV_TEXT_ALIGN_CENTER);

    s_app_icons[APP_VOLUME] = app_icon_create(s_apps, APP_VOLUME, "音量", COL_MUTED);
    disc = lv_obj_get_child(s_app_icons[APP_VOLUME], 0);
    draw_speaker(disc, 32, 36, COL_MUTED);

    /* 返回表盘胶囊 */
    lv_obj_t *back = mk_box(s_apps, 173, 410, 120, 34, COL_SURF, LV_OPA_COVER, 17);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_color(back, COL_LINE, 0);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, apps_back_cb, LV_EVENT_CLICKED, NULL);
    mk_label(back, "返回表盘", &epw_cjk_16, COL_MUTED, 0, 7, 120, LV_TEXT_ALIGN_CENTER);

    lv_obj_add_flag(s_apps, LV_OBJ_FLAG_HIDDEN);
    apps_layout_icons();
}

/* -------------------------------------------------------------------------- */
/* 宠物页（Pip · 3D 正脸猫）                                                   */
/* -------------------------------------------------------------------------- */

static lv_color_t pet_accent(ui_pet_state_t state)
{
    switch (state) {
    case UI_PET_HAPPY:
        return COL_AMBER;
    case UI_PET_EXCITED:
        return COL_ORANGE;
    case UI_PET_THINKING:
        return COL_PURPLE;
    case UI_PET_SPEAKING:
        return COL_BLUE;
    case UI_PET_SLEEPY:
        return COL_MUTED;
    case UI_PET_SAD:
        return COL_CORAL;
    case UI_PET_LOVE:
        return COL_PINK;
    case UI_PET_LISTENING:
        return COL_TEAL;
    case UI_PET_IDLE:
    default:
        return COL_TEAL;
    }
}

static const lv_image_dsc_t *pet_face(ui_pet_state_t state)
{
    switch (state) {
    case UI_PET_HAPPY:
        return &pip_face_happy;
    case UI_PET_EXCITED:
        return &pip_face_excited;
    case UI_PET_THINKING:
        return &pip_face_thinking;
    case UI_PET_SPEAKING:
        return &pip_face_speaking;
    case UI_PET_SLEEPY:
        return &pip_face_sleepy;
    case UI_PET_SAD:
        return &pip_face_sad;
    case UI_PET_LOVE:
        return &pip_face_love;
    case UI_PET_LISTENING:
    case UI_PET_IDLE:
    default:
        return &pip_face_idle;
    }
}

static const char *pet_mood_cn(ui_pet_state_t state)
{
    switch (state) {
    case UI_PET_HAPPY:
        return "开心";
    case UI_PET_EXCITED:
        return "兴奋";
    case UI_PET_THINKING:
        return "思考";
    case UI_PET_SPEAKING:
        return "说话";
    case UI_PET_SLEEPY:
        return "困了";
    case UI_PET_SAD:
        return "难过";
    case UI_PET_LOVE:
        return "喜欢";
    case UI_PET_LISTENING:
        return "倾听";
    case UI_PET_IDLE:
    default:
        return "发呆";
    }
}

static const char *pet_default_text(ui_pet_state_t state)
{
    switch (state) {
    case UI_PET_HAPPY:
        return "见到你真开心！";
    case UI_PET_EXCITED:
        return "哇！太棒啦！";
    case UI_PET_THINKING:
        return "让我想一想…";
    case UI_PET_SPEAKING:
        return "Hello！跟我读：cat！";
    case UI_PET_SLEEPY:
        return "我有点困了，晚安～";
    case UI_PET_SAD:
        return "别难过，我陪着你。";
    case UI_PET_LOVE:
        return "我最喜欢你啦！";
    case UI_PET_LISTENING:
        return "我在听，你说吧。";
    case UI_PET_IDLE:
    default:
        return "喵～ 你好呀！我是 Pip。";
    }
}

/*
 * 豆包返回的台词是动态的，字体不可能覆盖所有生僻字 / emoji。
 * 这里按气泡字体逐字检查字形，丢掉没有字形的码点，避免整句出现方框。
 * 只在 LVGL 任务里调用（桥接线程持有 display lock），静态缓冲安全。
 */
static const char *pet_safe_text(const char *in)
{
    static char out[320];
    size_t o = 0;

    if (in == NULL) {
        return NULL;
    }

    const uint8_t *p = (const uint8_t *)in;
    while (*p != '\0' && o + 5 < sizeof(out)) {
        uint32_t cp;
        int n;
        if (*p < 0x80) {
            cp = *p;
            n = 1;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = *p & 0x1F;
            n = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = *p & 0x0F;
            n = 3;
        } else if ((*p & 0xF8) == 0xF0) {
            cp = *p & 0x07;
            n = 4;
        } else {
            p++;
            continue;
        }

        bool valid = true;
        for (int i = 1; i < n; i++) {
            if (p[i] == '\0' || (p[i] & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        if (!valid) {
            p++;
            continue;
        }

        bool supported = (cp == '\n');
        if (!supported) {
            lv_font_glyph_dsc_t dsc;
            supported = lv_font_get_glyph_dsc(&epw_cjk_16, &dsc, cp, 0)
                        && !dsc.is_placeholder;
        }
        if (supported) {
            memcpy(out + o, p, (size_t)n);
            o += (size_t)n;
        }
        p += n;
    }
    out[o] = '\0';
    return out;
}

static void pet_apply(ui_pet_state_t state, const char *text)
{
    if ((int)state < 0 || state >= UI_PET_STATE_COUNT) {
        state = UI_PET_IDLE;
    }
    s_pet_state = state;

    if (s_pet == NULL) {
        return;
    }

    lv_color_t accent = pet_accent(state);
    lv_image_set_src(s_pet_img, pet_face(state));
    lv_obj_set_style_text_color(s_pet_mood, accent, 0);
    lv_label_set_text(s_pet_mood, pet_mood_cn(state));

    const char *line = (text != NULL && text[0] != '\0')
                           ? text : pet_default_text(state);
    line = pet_safe_text(line);
    if (line == NULL || line[0] == '\0') {
        line = "喵～ 你好呀！我是 Pip。";
    }
    lv_label_set_text(s_pet_bubble_label, line);
    lv_obj_set_style_border_color(s_pet_bubble, accent, 0);
    lv_obj_set_style_bg_color(s_pet_glow, accent, 0);

    if (s_pet_halo != NULL) {
        lv_obj_set_style_bg_color(s_pet_halo, accent, 0);
    }

    /* 气泡轻轻上跳一下（只改 y，不做缩放合成，避免整块 layer 重绘） */
    lv_anim_delete(s_pet_bubble, anim_y_cb);
    lv_obj_set_y(s_pet_bubble, PET_BUBBLE_Y);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_pet_bubble);
    lv_anim_set_values(&a, PET_BUBBLE_Y + 8, PET_BUBBLE_Y);
    lv_anim_set_duration(&a, 150);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_start(&a);

    s_pet_pop = 24;
}

static void pet_anim_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (s_pet == NULL || !s_pet_open) {
        return;
    }

    s_pet_phase++;
    int ph = s_pet_phase;
    int bob = 0;

    /* 只做上下位移：图片始终 1:1 直绘，不做缩放/旋转，避免每帧软件变换 */
    switch (s_pet_state) {
    case UI_PET_HAPPY:
        bob = -(int)(7.0f * fabsf(sinf(ph * 0.25f)));
        break;
    case UI_PET_EXCITED:
        bob = -(int)(15.0f * fabsf(sinf(ph * 0.45f)));
        break;
    case UI_PET_THINKING:
        bob = -(int)(2.0f * fabsf(sinf(ph * 0.10f)));
        break;
    case UI_PET_SPEAKING:
        bob = (int)(2.0f * sinf(ph * 0.5f));
        break;
    case UI_PET_SLEEPY:
        bob = 6 + (int)(3.0f * sinf(ph * 0.05f));
        break;
    case UI_PET_SAD:
        bob = 9;
        break;
    case UI_PET_LOVE:
        bob = -5 - (int)(3.0f * fabsf(sinf(ph * 0.2f)));
        break;
    case UI_PET_LISTENING:
        bob = -(int)(1.5f * fabsf(sinf(ph * 0.15f)));
        break;
    case UI_PET_IDLE:
    default:
        bob = (int)(3.0f * sinf(ph * 0.12f));
        break;
    }

    if (s_pet_pop > 0) {
        bob -= s_pet_pop / 3;
        s_pet_pop -= 3;
        if (s_pet_pop < 0) {
            s_pet_pop = 0;
        }
    }

    /* 只有位置真的变了才写，避免无意义的重绘 */
    if (bob != s_pet_last_bob) {
        s_pet_last_bob = bob;
        lv_obj_set_y(s_pet_img, PET_IMG_Y + bob);
    }

    /* 星点只在兴奋/喜欢时出现：固定尺寸，每 2 个 tick 更新一次位置 */
    bool sparkle = (s_pet_state == UI_PET_EXCITED || s_pet_state == UI_PET_LOVE);
    if (!sparkle) {
        for (int i = 0; i < 8; i++) {
            if (s_pet_spark[i] != NULL) {
                lv_obj_add_flag(s_pet_spark[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        return;
    }
    if ((ph & 1) != 0) {
        return;
    }

    for (int i = 0; i < 8; i++) {
        lv_obj_t *sp = s_pet_spark[i];
        if (sp == NULL) {
            continue;
        }
        lv_obj_remove_flag(sp, LV_OBJ_FLAG_HIDDEN);
        int speed = (s_pet_state == UI_PET_EXCITED) ? 6 : 4;
        float ang = (float)(ph * speed + i * 45) * (float)M_PI / 180.0f;
        int r = (s_pet_state == UI_PET_EXCITED ? 112 : 100)
                + (int)(6.0f * sinf(ph * 0.1f + i));
        int x = CENTER_X + (int)lroundf((float)r * cosf(ang));
        int y = 196 + (int)lroundf((float)r * sinf(ang));
        lv_obj_set_pos(sp, x - 5, y - 5);
        lv_obj_set_style_bg_color(sp,
                                  s_pet_state == UI_PET_LOVE ? COL_PINK : COL_AMBER, 0);
    }
}

static void pet_touch_cb(lv_event_t *e)
{
    /* 长按触发后 LVGL 还会补发 CLICKED，这里吞掉，避免“想”立刻变“摸头” */
    if (s_pet_long_pressed) {
        s_pet_long_pressed = false;
        return;
    }
    if (tap_is_drag(e)) {
        return;
    }
    if (s_pet_state == UI_PET_LOVE) {
        pet_apply(UI_PET_HAPPY, "嘿嘿，好痒呀！");
    } else {
        pet_apply(UI_PET_LOVE, "摸摸我的头，好开心！");
    }
}

static void pet_press_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    /* 每次新的按下都清标志，防止上一次长按状态残留吞掉正常点按 */
    s_pet_long_pressed = false;
}

static void pet_long_press_cb(lv_event_t *e)
{
    if (tap_is_drag(e)) {
        return;
    }
    s_pet_long_pressed = true;
    pet_apply(UI_PET_THINKING, "让我想一想…");
}

static void pet_chat_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (s_pet_chat_stage == 1) {
        pet_apply(UI_PET_THINKING, "让我想一想…");
    } else if (s_pet_chat_stage == 2) {
        pet_apply(UI_PET_SPEAKING, "Hello！跟我学英语吧！");
    } else {
        s_pet_chat_busy = false;
        s_pet_chat_timer = NULL;
        pet_apply(UI_PET_HAPPY, "我们去学新单词吧！");
    }
    s_pet_chat_stage++;
}

/* 左右滑：进入一轮“听 → 想 → 说”的学英语对话 */
static void pet_begin_chat(void)
{
    if (s_pet_chat_timer != NULL) {
        lv_timer_del(s_pet_chat_timer);
        s_pet_chat_timer = NULL;
    }
    s_pet_chat_busy = true;
    s_pet_chat_stage = 0;
    s_pet_chat_timer = lv_timer_create(pet_chat_timer_cb, 1100, NULL);
    lv_timer_set_repeat_count(s_pet_chat_timer, 3);
    pet_apply(UI_PET_LISTENING, "我在听，你说吧。");
}

static void pet_back_cb(lv_event_t *e)
{
    if (tap_is_drag(e)) {
        return;
    }
    pet_close();
}

/* 宠物页手势：左右滑=聊天，上滑=兴奋，下滑=睡觉（全部触屏，无文字按钮） */
static void pet_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
        return;
    }
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        return;
    }

    switch (lv_indev_get_gesture_dir(indev)) {
    case LV_DIR_LEFT:
    case LV_DIR_RIGHT:
        pet_begin_chat();
        break;
    case LV_DIR_TOP:
        pet_apply(UI_PET_EXCITED, "哇！我飞起来啦！");
        break;
    case LV_DIR_BOTTOM:
        pet_apply(UI_PET_SLEEPY, "呼…我先睡一会儿，晚安。");
        break;
    default:
        break;
    }
}

/* 本地情景引擎：桥接静默 30s 后，按电量 / 充电 / 网络 / 时间自动换状态 */
static void pet_scene_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (s_pet == NULL || !s_pet_open || s_pet_chat_busy) {
        return;
    }
    uint32_t now = lv_tick_get();
    if (s_pet_bridge_ms != 0 && (uint32_t)(now - s_pet_bridge_ms) < 30000u) {
        return;
    }

    if (s_charging || s_power_override == 1) {
        pet_apply(UI_PET_HAPPY, "充电中，能量满满！");
        return;
    }
    if (s_bat_percent >= 0 && s_bat_percent < 20) {
        pet_apply(UI_PET_SAD, "电量有点低，快给我充电吧。");
        return;
    }
    if (wifi_get_state() == EPW_WIFI_CONNECTING) {
        pet_apply(UI_PET_THINKING, "我在找网络，等一下哦…");
        return;
    }

    struct tm tm_now;
    time_t now_s = time(NULL);
    localtime_r(&now_s, &tm_now);
    int hour = ((tm_now.tm_year + 1900) >= 2024) ? tm_now.tm_hour : 12;
    if (hour >= 21 || hour < 7) {
        pet_apply(UI_PET_SLEEPY, "很晚啦，我们睡觉吧，晚安～");
        return;
    }

    static const char *const lines[] = {
        "Hello！跟我学英语吧！",
        "Cat 是小猫，Dog 是小狗。",
        "Apple 是苹果，Banana 是香蕉。",
        "我们一起数数：one, two, three！",
        "今天想学哪个单词呀？",
        "你真是个小天才！",
        "摸摸我的头吧！",
    };
    static const ui_pet_state_t seq[] = {
        UI_PET_IDLE, UI_PET_HAPPY, UI_PET_THINKING, UI_PET_LISTENING,
    };
    s_pet_scene_idx = (s_pet_scene_idx + 1) %
                      (int)(sizeof(lines) / sizeof(lines[0]));
    pet_apply(seq[s_pet_scene_idx % 4], lines[s_pet_scene_idx]);
}

static void build_pet(void)
{
    s_pet = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_pet);
    lv_obj_remove_flag(s_pet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_pet, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_pet, 0, 0);
    lv_obj_set_style_bg_color(s_pet, lv_color_hex(0x05070C), 0);
    lv_obj_set_style_bg_grad_color(s_pet, lv_color_hex(0x10202A), 0);
    lv_obj_set_style_bg_grad_dir(s_pet, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_pet, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_pet, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_pet, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_pet, pet_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s_pet, pet_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_pet, pet_touch_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_pet, pet_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);

    /* 返回圆钮 */
    lv_obj_t *back = mk_circle(s_pet, 52, 56, 38, COL_SURF, LV_OPA_COVER);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_color(back, COL_LINE, 0);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, pet_back_cb, LV_EVENT_CLICKED, NULL);
    static const lv_point_precise_t chev[] = {{21, 8}, {11, 19}, {21, 30}};
    lv_obj_t *l = lv_line_create(back);
    lv_line_set_points(l, chev, 3);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, 3, 0);
    lv_obj_set_style_line_color(l, COL_MUTED, 0);
    lv_obj_set_style_line_rounded(l, true, 0);

    mk_label(s_pet, "皮皮 Pip", &epw_cjk_20, COL_TEXT,
             80, 42, 150, LV_TEXT_ALIGN_LEFT);
    s_pet_mood = mk_label(s_pet, "发呆", &epw_cjk_16, COL_TEAL,
                          250, 46, 160, LV_TEXT_ALIGN_RIGHT);

    /* 宠物背后的柔光：两层同心圆纯色填充。
     * 不用 shadow（SW 模糊每帧重算，是宠物页卡顿的主因）。 */
    s_pet_halo = mk_circle(s_pet, CENTER_X, 196, 300, COL_TEAL, LV_OPA_10);
    s_pet_glow = mk_circle(s_pet, CENTER_X, 196, 246, COL_TEAL, LV_OPA_20);

    /* 3D 正脸猫：素材本身就是 208px，1:1 直绘（scale=256，无运行时变换） */
    s_pet_img = lv_image_create(s_pet);
    lv_image_set_src(s_pet_img, &pip_face_idle);
    lv_obj_set_pos(s_pet_img, PET_IMG_X, PET_IMG_Y);
    lv_obj_remove_flag(s_pet_img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_pet_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_pet_img, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_pet_img, pet_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s_pet_img, pet_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_pet_img, pet_touch_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_pet_img, pet_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);

    /* 兴奋 / 喜欢时绕着头顶转的小光点 */
    for (int i = 0; i < 8; i++) {
        s_pet_spark[i] = mk_circle(s_pet, 0, 0, 10, COL_AMBER, LV_OPA_COVER);
        lv_obj_add_flag(s_pet_spark[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* 对话气泡（去掉旋转小尾巴，避免 transform layer 合成） */
    s_pet_bubble = mk_box(s_pet, 63, PET_BUBBLE_Y, 340, 88,
                          COL_SURF_HI, LV_OPA_COVER, 26);
    lv_obj_set_style_border_width(s_pet_bubble, 1, 0);
    lv_obj_set_style_border_color(s_pet_bubble, COL_TEAL, 0);
    lv_obj_set_style_border_opa(s_pet_bubble, LV_OPA_60, 0);
    s_pet_bubble_label = mk_label(s_pet_bubble, "喵～ 你好呀！我是 Pip。",
                                  &epw_cjk_16, COL_TEXT,
                                  18, 12, 304, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(s_pet_bubble_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(s_pet_bubble_label, 64);

    /* 装饰层不拦截触摸，点按交给 s_pet / 猫咪本体处理 */
    lv_obj_remove_flag(s_pet_halo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_pet_glow, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_pet_bubble, LV_OBJ_FLAG_CLICKABLE);

    /* 全部交互走手势：点按 / 左右滑 / 上下滑 / 长按，页面不放任何文字按钮 */

    lv_obj_add_flag(s_pet, LV_OBJ_FLAG_HIDDEN);
    pet_apply(UI_PET_IDLE, NULL);
}

/* -------------------------------------------------------------------------- */
/* 初始化                                                                      */
/* -------------------------------------------------------------------------- */

void ui_init(void)
{
    s_screen = lv_screen_active();
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev != NULL) {
        lv_indev_set_long_press_time(indev, 500);
    }

    /* 表盘（唯一的常驻页面） */
    s_home = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_home);
    lv_obj_remove_flag(s_home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_home, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_home, 0, 0);
    lv_obj_add_flag(s_home, LV_OBJ_FLAG_CLICKABLE);

    build_dial(s_home);
    build_center();
    build_status();
    build_slider();
    build_torch();
    build_apps();
    build_pet();

    /* 全局提示条 */
    s_toast = mk_box(s_screen, 83, 210, 300, 44, COL_TEXT, LV_OPA_COVER, 22);
    lv_obj_set_style_shadow_width(s_toast, 22, 0);
    lv_obj_set_style_shadow_color(s_toast, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(s_toast, LV_OPA_40, 0);
    s_toast_label = mk_label(s_toast, "音量固定为 0", &epw_cjk_16, COL_INK,
                             0, 11, 300, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    time_tick_cb(NULL);
    ui_update_battery(power_battery_percent(), power_is_vbus());
    brightness_apply();

    lv_timer_create(time_tick_cb, 1000, NULL);
    lv_timer_create(battery_timer_cb, 3000, NULL);
    lv_timer_create(network_timer_cb, 2000, NULL);
    lv_timer_create(pet_anim_timer_cb, 80, NULL);
    lv_timer_create(pet_scene_timer_cb, 9000, NULL);
}

/* -------------------------------------------------------------------------- */
/* 演示 / 调试接口                                                             */
/* -------------------------------------------------------------------------- */

void ui_demo_open_center(void)
{
    center_open();
}

void ui_demo_close_center(void)
{
    center_close();
}

void ui_demo_open_status(void)
{
    status_open();
}

void ui_demo_close_status(void)
{
    status_close();
}

void ui_demo_open_bright(void)
{
    slider_open(SLIDER_BRIGHT);
}

void ui_demo_close_bright(void)
{
    if (s_slider_kind == SLIDER_BRIGHT) {
        slider_close();
    }
}

void ui_demo_open_volume(void)
{
    slider_open(SLIDER_VOLUME);
}

void ui_demo_close_volume(void)
{
    if (s_slider_kind == SLIDER_VOLUME) {
        slider_close();
    }
}

void ui_demo_open_torch(void)
{
    torch_open();
}

void ui_demo_close_torch(void)
{
    torch_close();
}

void ui_demo_power_mode(int mode)
{
    s_power_override = (mode == 0 || mode == 1) ? mode : -1;
    ui_update_battery(power_battery_percent(), power_is_vbus());
}

void ui_demo_toggle_menu(void)
{
    if (s_torch_open) {
        torch_close();
    } else if (s_slider_open) {
        slider_close();
    } else if (s_status_open) {
        status_close();
    } else if (s_center_open) {
        center_close();
    } else {
        center_open();
    }
}

void ui_demo_open_apps(void)
{
    apps_open();
}

void ui_demo_close_apps(void)
{
    apps_close();
}

void ui_demo_toggle_apps(void)
{
    if (s_apps_open) {
        apps_close();
    } else {
        apps_open();
    }
}

void ui_demo_open_pet(void)
{
    ui_pet_set_state(UI_PET_HAPPY, NULL);
}

void ui_demo_close_pet(void)
{
    pet_close();
}

void ui_demo_toggle_pet(void)
{
    if (s_pet_open) {
        pet_close();
    } else {
        ui_pet_set_state(UI_PET_HAPPY, NULL);
    }
}

void ui_pet_set_state(ui_pet_state_t state, const char *text)
{
    pet_apply(state, text);
    pet_open();
}

void ui_pet_set_state_by_name(const char *emotion, const char *text)
{
    static const struct {
        const char *name;
        ui_pet_state_t state;
    } map[] = {
        {"idle", UI_PET_IDLE},
        {"happy", UI_PET_HAPPY},
        {"excited", UI_PET_EXCITED},
        {"thinking", UI_PET_THINKING},
        {"speaking", UI_PET_SPEAKING},
        {"sleepy", UI_PET_SLEEPY},
        {"sad", UI_PET_SAD},
        {"love", UI_PET_LOVE},
        {"listening", UI_PET_LISTENING},
        {"发呆", UI_PET_IDLE},
        {"开心", UI_PET_HAPPY},
        {"兴奋", UI_PET_EXCITED},
        {"思考", UI_PET_THINKING},
        {"说话", UI_PET_SPEAKING},
        {"困了", UI_PET_SLEEPY},
        {"难过", UI_PET_SAD},
        {"喜欢", UI_PET_LOVE},
        {"倾听", UI_PET_LISTENING},
    };

    ui_pet_state_t state = UI_PET_IDLE;
    if (emotion != NULL) {
        for (int i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++) {
            if (strcmp(emotion, map[i].name) == 0) {
                state = map[i].state;
                break;
            }
        }
    }

    /* 豆包桥接只更新状态/台词，不主动弹出宠物页，避免打扰当前操作。 */
    s_pet_bridge_ms = lv_tick_get();
    pet_apply(state, text);
}
