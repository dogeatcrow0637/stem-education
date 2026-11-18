/*
 * 466×466 圆屏：表盘、六磁贴控制中心、应用桌面、网络和宠物页。
 * 未校时显示等待状态，不提供无数据来源的天气。
 * 音量页调节音频输出；宠物页左滑聊天，右滑/退出停止语音。
 */
#include "ui.h"
#include "button.h"

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
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/draw/lv_draw_buf_private.h" /* 图标缓存指定 PSRAM 分配器 */
#include "src/core/lv_obj_draw_private.h" /* lv_obj_get_ext_draw_size（LVGL 9.5 未公开到头文件） */
#include "nvs.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "nursery.h"
#include "watch_system.h"
#include "voice_client.h"
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
/* 电量弧收窄到 90°，明显短于底部橙色环段（122°-238°，116°）。 */
#define BATT_FROM_DEG  135
#define BATT_TO_DEG    225

#define TILE_D      84
#define TILE_DOT_D  10

/* 亮度 / 音量页：竖向轨道（底部最小、顶部最大），整体垂直居中 */
#define SLIDER_TRACK_W   44
#define SLIDER_TRACK_H   280
#define SLIDER_TRACK_TOP (CENTER_Y - SLIDER_TRACK_H / 2)
#define SLIDER_TRACK_X   (CENTER_X - SLIDER_TRACK_W / 2)

/* 固定九宫格：中心最大，周边递减；拖动仅轻微偏移，松手归位。 */
#define APP_COUNT          9
#define APP_ICON_D         120
#define APP_ROOT_W         APP_ICON_D
#define APP_ROOT_H         APP_ICON_D
#define APP_ICON_MIN_D     64
#define APP_GRID_STEP      116
#define APP_PAN_OVERSHOOT  16
#define APP_GRID_DRAG_TH   16

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

/* 表盘三色环：亮黄（右上）+ 橙（底部）+ 天蓝（左侧），三段贴边常驻 */
#define COL_RING_YELLOW lv_color_hex(0xFFFF00)
#define COL_RING_ORANGE lv_color_hex(0xFF6600)
#define COL_RING_SKY    lv_color_hex(0x00BFFF)
/* 天气分割线绿色；调节页用科技感浅绿 */
#define COL_GREEN       lv_color_hex(0x2EE66B)
#define COL_SLEEP_ACTIVE lv_color_hex(0x58408F)
#define COL_TECH_GREEN  lv_color_hex(0x5CF39A)

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
static lv_obj_t *s_nursery;
static lv_obj_t *s_notice;

static bool s_center_open;
static bool s_status_open;
static bool s_slider_open;
static bool s_torch_open;
static bool s_apps_open;
static bool s_pet_open;
static bool s_nursery_open;
static lv_obj_t *s_tool_drag_page;
static bool s_tool_dragging, s_tool_drag_engaged;
static lv_point_t s_tool_press;
static lv_obj_t *s_tool_snap_img;
static lv_draw_buf_t *s_tool_snap;
static bool s_tool_snap_valid, s_tool_snap_showing;
static int s_tool_snap_ext, s_tool_x;

/* 控制中心抽屉式滑动：s_center_reveal 表示整块面板从顶部滑下的距离 0..SCREEN_H，
 * 面板 y = reveal - SCREEN_H，内容（标题、磁贴）随面板一起平移 */
#define CENTER_DRAG_TH  8
static bool s_center_dragging;
static bool s_center_drag_engaged;
static int s_center_press_x;
static int s_center_press_y;
static int s_center_press_reveal;
static int s_center_reveal;

/* 底部通知抽屉：s_notice_reveal 表示面板从底部上拉出来的高度 0..NOTICE_H，
 * 面板 y = SCREEN_H - reveal。起手区域与顶部下拉对称，只认底部 1/12。 */
#define NOTICE_H        300
#define NOTICE_EDGE_H   (SCREEN_H / 12)
static bool s_notice_open;
static bool s_notice_dragging;
static bool s_notice_drag_engaged;
static int s_notice_press_x;
static int s_notice_press_y;
static int s_notice_press_reveal;
static int s_notice_reveal;

/* 下拉/收起时只移动一张预渲染快照：真实面板有 6 个磁贴（圆环 + 图标 + 文字），
 * 每帧重绘整屏会明显掉帧；抽屉和底层表盘均用 RGB565 快照，拖动时只做位图搬运。
 * 快照创建失败时自动退回直接移动真实面板。 */
static lv_obj_t *s_home_snap_img;
static lv_draw_buf_t *s_home_snap;
static bool s_home_snap_showing;
static bool s_home_snap_valid;
static bool s_home_snap_dirty = true;
static lv_obj_t *s_center_snap_img;   /* 抽屉快照图层（纯显示，不可点） */
static lv_draw_buf_t *s_center_snap;  /* RGB565 快照（PSRAM，约 434KB） */
static bool s_center_snap_valid;
static int s_center_snap_ext;         /* 快照外扩像素，贴图时对消 */

/* 儿歌页同样做成抽屉：表盘左滑跟手拉入，页内右滑跟手退出。
 * 滑入移动页面快照（页面关闭后预渲染），滑出移动真实页面以保持进度实时。 */
#define NUR_DRAG_TH  8
static bool s_nur_dragging;
static bool s_nur_drag_engaged;
static int s_nur_press_x;
static int s_nur_press_y;
static int s_nur_reveal = -SCREEN_W;  /* 0=完全打开，-SCREEN_W=收在主屏左侧 */
static lv_obj_t *s_nur_snap_img;
static lv_draw_buf_t *s_nur_snap;
static bool s_nur_snap_valid;
static int s_nur_snap_ext;

/* 页面切换动画期间短暂屏蔽抽屉手势：避免上一页还在滑动时又拉出控制中心，
 * 出现两个页面同屏。 */
static uint32_t s_page_anim_until;

/* 表盘 */
static lv_obj_t *s_time;
static lv_obj_t *s_date;
static lv_obj_t *s_weather;
static lv_obj_t *s_weather_icon;
/* 表盘装饰层（外环 / 电量 / 天气 / Wi-Fi / 日期）：睡眠模式下整层隐藏，只留时间 */
static lv_obj_t *s_dial_deco;
static bool s_sleep_mode;
/* 三色装饰环（常驻） + 底部电量弧（随电量变化，充电时青柠色发光） */
static lv_obj_t *s_batt_glow;
static lv_obj_t *s_batt_track;
static lv_obj_t *s_batt_arc;
static lv_obj_t *s_batt_pct;
static lv_obj_t *s_batt_bolt;
static lv_obj_t *s_home_wifi[4]; /* 3 段弧 + 底部圆点，用颜色表达联网状态 */

/* 控制中心睡眠磁贴（用于显示开/关状态） */
static lv_obj_t *s_cc_sleep;
static lv_obj_t *s_cc_sleep_off;
static lv_obj_t *s_cc_sleep_on;

/* 图标位置、缩放和命中都使用同一二维坐标。 */
static lv_obj_t *s_app_icons[APP_COUNT];
static lv_draw_buf_t *s_app_art[APP_COUNT];
static lv_draw_buf_t *s_app_scaled[APP_COUNT];
static int s_app_pixel_size[APP_COUNT];
static lv_obj_t *s_tools, *s_settings_page, *s_timer_page, *s_timer_value, *s_timer_hint;
static lv_obj_t *s_timer_action, *s_timer_reset_label;
static lv_obj_t *s_idle_options[6];
static lv_obj_t *s_timer_select, *s_timer_custom, *s_timer_run, *s_timer_arc;
static lv_obj_t *s_timer_rollers[3];
static uint32_t s_timer_total = 60;
static bool s_tools_open;
static int s_tools_kind;
static const uint32_t s_idle_choices[] = {15000, 30000, 60000, 120000, 300000, 0};
static const uint32_t s_timer_choices[] = {60, 180, 300, 600, 1800, 3600};
static lv_point_t s_app_pan;
static lv_point_t s_app_pan_start;
static lv_point_t s_app_settle_from;
static lv_point_t s_app_settle_to;
static bool s_app_drag_active;
static bool s_app_drag_moved;
static lv_point_t s_app_press;

/* 宠物页 */
#define PET_IMG_SIZE 208
#define PET_IMG_X ((SCREEN_W - PET_IMG_SIZE) / 2)
#define PET_IMG_Y 108
#define PET_BUBBLE_Y 304
static lv_obj_t *s_pet_img;
static lv_obj_t *s_pet_glow;
static lv_obj_t *s_pet_halo;
static lv_obj_t *s_pet_bubble;
static lv_obj_t *s_pet_bubble_label;
static char s_pet_text[320];
static size_t s_pet_text_end, s_pet_text_start;
static lv_obj_t *s_pet_mood;
static lv_obj_t *s_pet_spark[8];
static ui_pet_state_t s_pet_state = UI_PET_IDLE;
static int s_pet_phase;
static int s_pet_pop;
static int s_pet_last_bob;
static bool s_pet_long_pressed; /* 长按已触发，拦截随后的 CLICKED */
static int s_pet_scene_idx;

/* 3S 英语儿歌：播放器 / 本周曲库两张卡片，同页上下切换。
 * 卡片直接挂在页面下，纵向坐标由 s_nur_card 决定：
 * 播放器 100，本周曲库 522；切换时两张卡同时上移。 */
#define NUR_CARD_X       44
#define NUR_CARD_W       378
#define NUR_CARD_H       284
#define NUR_CARD_PLAYER_Y 100
#define NUR_CARD_WEEK_Y   (SCREEN_H + 56)
#define NUR_CARD_SHIFT    (NUR_CARD_WEEK_Y - NUR_CARD_PLAYER_Y)
static int s_nur_card; /* 0 = 播放器，1 = 本周曲库 */
static lv_obj_t *s_nur_player_card;
static lv_obj_t *s_nur_week_card;
static lv_obj_t *s_nur_arc;
static lv_obj_t *s_nur_title;
static lv_obj_t *s_nur_lyric;
static lv_obj_t *s_nur_library_label;
static lv_obj_t *s_nur_time;
static lv_obj_t *s_nur_volume;
static lv_obj_t *s_nur_play_btn;
static lv_obj_t *s_nur_mode_btn;
static lv_obj_t *s_nur_prev_btn;
static lv_obj_t *s_nur_next_btn;
static lv_obj_t *s_nur_icon_play;
static lv_obj_t *s_nur_icon_pause;
static lv_obj_t *s_nur_mode_label;
static lv_obj_t *s_nur_stars[5];
static lv_obj_t *s_nur_slot[3];
static lv_obj_t *s_nur_slot_label[3];
static lv_obj_t *s_nur_week_stars;
static lv_obj_t *s_nur_week_row[EPW_NURSERY_WEEKLY_MAX];
static lv_obj_t *s_nur_week_title[EPW_NURSERY_WEEKLY_MAX];
static lv_obj_t *s_nur_week_meta[EPW_NURSERY_WEEKLY_MAX];
static lv_obj_t *s_nur_wish_btn;
static lv_obj_t *s_nur_wish_label;
static bool s_nur_catalog_seen;
static int s_nur_last_done;
static int s_nur_last_stars;
static bool s_nur_slot_long;
static uint32_t s_nur_ui_ms;
/* 拍快照时临时放开隐藏页的 UI 刷新，让滑入的第一帧就是真实内容 */
static bool s_nur_force_update;
static uint32_t s_pet_bridge_ms;

/* 状态页 */
static lv_obj_t *s_status_bat;
static lv_obj_t *s_status_net;
static lv_obj_t *s_status_ip;

/* 网络 App：热点列表 + 密码页 */
static lv_obj_t *s_wifi;
static lv_obj_t *s_wifipwd;
static bool s_wifi_open;
static bool s_wifipwd_open;
static lv_obj_t *s_wifi_status;
static lv_obj_t *s_wifi_list;
static lv_obj_t *s_wifi_scan_hint;
static lv_timer_t *s_wifi_timer;
static epw_wifi_ap_t s_wifi_aps[EPW_WIFI_MAX_APS];
static int s_wifi_ap_count;
static bool s_wifi_scan_seen;
static char s_wifipwd_ssid[33];
static bool s_wifipwd_secure;
static lv_obj_t *s_wifipwd_title;
static lv_obj_t *s_wifipwd_ta;
static lv_obj_t *s_wifipwd_eye_label;
static lv_obj_t *s_wifipwd_btn;
static lv_obj_t *s_wifipwd_btn_label;
static lv_obj_t *s_wifipwd_hint;
static lv_obj_t *s_wifipwd_kb;
static int s_wifipwd_kb_mode;
static bool s_wifipwd_connecting;
static bool s_wifipwd_hidden;
static lv_obj_t *s_wifi_refresh;

/* 密码键盘模式（自绘 buttonmatrix，避免 lv_keyboard 的百分比尺寸/对齐） */
enum { KP_LC = 0, KP_UC, KP_NUM, KP_SYMBOL };
static void wifi_pwd_kb_apply(void);

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
static bool s_usb_powered;
static int s_bat_percent = -1;
static int s_brightness = 80;

/* 熄屏：30s 无操作关背光；点屏幕或按任意键点亮 */
#define UI_IDLE_TIMEOUT_MS watch_idle_timeout_ms()
#define UI_IDLE_POLL_MS    100
static bool s_screen_off;
static uint32_t s_last_activity;
static lv_obj_t *s_wake_layer;

/* 手电页 */
static lv_obj_t *s_torch_hint;
static lv_timer_t *s_torch_hint_timer;
static uint32_t s_torch_last_click;

/* 演示用电源模式覆盖：-1 自动，0 普通，1 充电 */
static int s_power_override = -1;

/* 兜底时钟（SNTP 未同步时从 10:09 走起） */
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
static void apps_layout_icons(void);
static void tools_open(int kind);
static void tools_close(void);
static void build_tools(void);
static void app_pan_anim_cb(void *var, int32_t value);
static int app_pan_bound(int value, bool elastic);
static void pet_open(void);
static void pet_close(void);
static void pet_close_now(void);
static void pet_apply(ui_pet_state_t state, const char *text);
static void nursery_open(void);
static void nursery_close(void);
static void nursery_close_now(void);
static void nursery_refresh_week(void);
static void nursery_update_ui(void);
static void nursery_card_set(int card, bool animate);
static void brightness_apply(void);
static void clear_clickable_tree(lv_obj_t *o);
static void wifi_open(void);
static void wifi_close(void);
static void wifi_close_now(void);
static void wifi_pwd_open(const epw_wifi_ap_t *ap);
static void wifi_pwd_close(void);
static void wifi_pwd_close_now(void);
static void notice_open(void);
static void notice_close(void);
static void notice_close_now(void);

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

/* 三色外环单段：只画实色主弧。
 * 不要再用更宽的半透明同色弧打底、也不要在外侧叠白色半透明高光：它们在
 * 黑底上都会变成一圈更粗的灰色/暗色轮廓，露在主弧外面，看起来像环没对齐。 */
static void mk_ring_seg(lv_obj_t *parent, int from, int to, lv_color_t color)
{
    mk_arc_seg(parent, CENTER_X, CENTER_Y, RING_SIZE, RING_WIDTH,
               from, to, color, LV_OPA_COVER);
}

/* 双色太阳：琥珀色光芒 + 亮黄核心。
 * 光芒与核心之间留出空隙，细长圆头且严格沿半径方向；否则小尺寸下光芒
 * 会贴着核心糊成一朵齿轮。光芒总长度控制在 2r 多一点，保证落在图标框内。 */
static void draw_sun_duo(lv_obj_t *parent, int cx, int cy, int r,
                         lv_color_t ray_color, lv_color_t core_color)
{
    const int gap = r >= 8 ? 3 : 2;
    const int len = r >= 8 ? 8 : 5;
    const int width = r >= 8 ? 3 : 2;
    const int mid = r + gap + len / 2;
    for (int i = 0; i < 8; i++) {
        int deg = i * 45;
        float rad = (float)deg * (float)M_PI / 180.0f;
        int x = cx + (int)lroundf((float)mid * sinf(rad));
        int y = cy - (int)lroundf((float)mid * cosf(rad));
        lv_obj_t *ray = mk_box(parent, x - width / 2, y - len / 2,
                               width, len, ray_color, LV_OPA_COVER, width / 2);
        /* 旋转默认轴心是对象左上角，必须移到光芒中心，否则 8 根光芒会绕着
         * 各自的角转，整朵太阳就散架了。 */
        lv_obj_set_style_transform_pivot_x(ray, width / 2, 0);
        lv_obj_set_style_transform_pivot_y(ray, len / 2, 0);
        lv_obj_set_style_transform_rotation(ray, deg * 10, 0);
    }
    mk_circle(parent, cx, cy, r * 2, core_color, LV_OPA_COVER);
}




/* 儿歌 App 图标：双音符（两个符头 + 符干 + 连梁） */
static void draw_note_icon(lv_obj_t *parent, int cx, int cy, lv_color_t color)
{
    mk_circle(parent, cx - 15, cy + 17, 15, color, LV_OPA_COVER);
    mk_circle(parent, cx + 15, cy + 11, 15, color, LV_OPA_COVER);
    mk_box(parent, cx - 10, cy - 22, 5, 40, color, LV_OPA_COVER, 2);
    mk_box(parent, cx + 20, cy - 28, 5, 40, color, LV_OPA_COVER, 2);

    static const lv_point_precise_t beam[] = {
        {0, 0}, {30, -6}, {30, 4}, {0, 10}, {0, 0},
    };
    lv_obj_t *b = lv_line_create(parent);
    lv_line_set_points(b, beam, 5);
    lv_obj_set_pos(b, cx - 10, cy - 22);
    lv_obj_set_style_line_width(b, 3, 0);
    lv_obj_set_style_line_color(b, color, 0);
    lv_obj_set_style_line_rounded(b, true, 0);
}

/* 控制中心图标统一为 44px 画布、3px 圆角线条。 */
static void icon_line(lv_obj_t *parent, const lv_point_precise_t *points, uint32_t count,
                      int cx, int cy, lv_color_t color)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, points, count);
    lv_obj_set_pos(line, cx - 22, cy - 22);
    lv_obj_set_style_line_width(line, 3, 0);
    lv_obj_set_style_line_color(line, color, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
}

static void icon_ring(lv_obj_t *parent, int cx, int cy, int diameter, lv_color_t color)
{
    lv_obj_t *ring = mk_circle(parent, cx, cy, diameter, color, LV_OPA_TRANSP);
    lv_obj_set_style_border_width(ring, 3, 0);
    lv_obj_set_style_border_color(ring, color, 0);
}

static void draw_status_icon(lv_obj_t *parent, int cx, int cy)
{
    icon_ring(parent, cx, cy, 38, COL_TEAL);
    mk_circle(parent, cx, cy - 9, 5, COL_TEXT, LV_OPA_COVER);
    mk_box(parent, cx - 2, cy - 2, 4, 14, COL_TEXT, LV_OPA_COVER, 2);
}

static void draw_battery_icon(lv_obj_t *parent, int cx, int cy)
{
    lv_obj_t *body = mk_box(parent, cx - 22, cy - 12, 42, 24, COL_GREEN, LV_OPA_TRANSP, 5);
    lv_obj_set_style_border_width(body, 3, 0);
    lv_obj_set_style_border_color(body, COL_GREEN, 0);
    mk_box(parent, cx + 22, cy - 5, 3, 10, COL_GREEN, LV_OPA_COVER, 1);
    static const lv_point_precise_t bolt[] = {{25, 14}, {18, 23}, {25, 23}, {20, 30}};
    icon_line(parent, bolt, 4, cx, cy, COL_TEXT);
}

static void draw_brightness_icon(lv_obj_t *parent, int cx, int cy, lv_color_t color)
{
    icon_ring(parent, cx, cy, 22, color);
    for (int i = 0; i < 8; i++) {
        float rad = (float)i * (float)M_PI / 4.0f;
        int x = cx + (int)lroundf(20.0f * sinf(rad));
        int y = cy - (int)lroundf(20.0f * cosf(rad));
        lv_obj_t *ray = mk_box(parent, x - 1, y - 3, 3, 7, color, LV_OPA_COVER, 2);
        lv_obj_set_style_transform_pivot_x(ray, 1, 0);
        lv_obj_set_style_transform_pivot_y(ray, 3, 0);
        lv_obj_set_style_transform_rotation(ray, i * 450, 0);
    }
}

static void draw_speaker(lv_obj_t *parent, int cx, int cy, lv_color_t color)
{
    static const lv_point_precise_t body[] = {
        {4, 16}, {13, 16}, {23, 7}, {23, 37}, {13, 28}, {4, 28}, {4, 16},
    };
    static const lv_point_precise_t cross_a[] = {{30, 16}, {33, 22}, {30, 28}};
    static const lv_point_precise_t cross_b[] = {{36, 10}, {41, 22}, {36, 34}};
    icon_line(parent, body, 7, cx, cy, color);
    icon_line(parent, cross_a, 3, cx, cy, color);
    icon_line(parent, cross_b, 3, cx, cy, color);
}

static void draw_torch(lv_obj_t *parent, int cx, int cy, lv_color_t color)
{
    static const lv_point_precise_t body[] = {
        {11, 13}, {33, 13}, {28, 23}, {28, 39}, {16, 39}, {16, 23}, {11, 13},
    };
    static const lv_point_precise_t neck[] = {{16, 23}, {28, 23}};
    static const lv_point_precise_t beam[][2] = {
        {{22, 2}, {22, 7}}, {{9, 4}, {12, 8}}, {{35, 4}, {32, 8}},
    };
    icon_line(parent, body, 7, cx, cy, COL_TEXT);
    icon_line(parent, neck, 2, cx, cy, color);
    for (int i = 0; i < 3; i++) icon_line(parent, beam[i], 2, cx, cy, color);
    mk_circle(parent, cx, cy + 8, 4, color, LV_OPA_COVER);
}

/* 网络 App 图标：三层 Wi-Fi 弧 + 底部圆点 */
static void draw_net_icon(lv_obj_t *parent, int cx, int cy, lv_color_t color)
{
    static const int radii[3] = {29, 20, 11};
    for (int i = 0; i < 3; i++) {
        int r = radii[i];
        lv_obj_t *arc = lv_arc_create(parent);
        lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(arc, cx - r, cy + 12 - r);
        lv_obj_set_size(arc, r * 2, r * 2);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 4, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, color, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_arc_set_rotation(arc, 0);
        lv_arc_set_bg_angles(arc, 225, 315);
        lv_arc_set_angles(arc, 0, 0);
    }
    mk_circle(parent, cx, cy + 14, 9, color, LV_OPA_COVER);
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

/* 控制中心抽屉滑动：整块面板/快照一起平移，不做逐帧布局，拖动才够顺。
 * 快照可能带外扩边距（ext），这里一并抵消，保证和真实面板像素对齐。 */
static void anim_center_h_cb(void *obj, int32_t value)
{
    s_center_reveal = value;
    lv_obj_set_y((lv_obj_t *)obj, value - SCREEN_H - s_center_snap_ext);
}

/* 页面切换动画窗口：期间不接受新的抽屉手势 / 点击，避免两个页面同时滑入 */
static void page_anim_lock(uint32_t ms)
{
    s_page_anim_until = lv_tick_get() + ms;
}

static bool page_anim_busy(void)
{
    return s_page_anim_until != 0 && (int32_t)(s_page_anim_until - lv_tick_get()) > 0;
}

/* 无效点击：正在拖动，或页面切换动画还没结束。
 * 动画期间底层页面还露着一半，若不禁点击，连点两下会同时拉起两个页面。 */
static bool tap_ignore(lv_event_t *e)
{
    if (page_anim_busy()) {
        return true;
    }
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

static int brightness_level(void)
{
    if (s_screen_off) return 0;
    if (s_sleep_mode) return 10;
    if (s_torch_open) return 100;
    return s_brightness;
}

static void brightness_apply(void)
{
    if (s_brightness < 10) {
        s_brightness = 10;
    }
    if (s_brightness > 100) {
        s_brightness = 100;
    }

    bsp_display_brightness_set(brightness_level());
    slider_refresh();
}

static void slider_refresh(void)
{
    if (s_slider == NULL) {
        return;
    }

    if (s_slider_kind == SLIDER_BRIGHT && s_sleep_mode) {
        lv_label_set_text(s_slider_value, "睡眠锁定 10%");
        lv_obj_set_style_text_font(s_slider_value, &epw_cjk_20, 0);
        lv_obj_set_y(s_slider_value, CENTER_Y - 12);
        lv_obj_add_flag(s_slider_track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_slider_fill, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    /* 亮度 / 音量显示当前数值。 */
    int v = s_slider_kind == SLIDER_VOLUME ? nursery_get_volume() : s_brightness;
    int h = SLIDER_TRACK_H * v / 100;

    lv_obj_set_style_text_font(s_slider_value, &lv_font_montserrat_28, 0);
    lv_obj_set_y(s_slider_value, 52);
    lv_obj_remove_flag(s_slider_track, LV_OBJ_FLAG_HIDDEN);
    if (s_slider_value != NULL) {
        lv_label_set_text_fmt(s_slider_value, "%d", v);
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
        /* 睡眠表盘锁在 10，不把用户设定值改掉；退出睡眠后自动恢复 */
        if (s_sleep_mode || s_screen_off || s_torch_open) {
            return;
        }
        s_brightness = v;
        brightness_apply();
    } else if (s_slider_kind == SLIDER_VOLUME) {
        nursery_set_volume(v);
        slider_refresh();
    }
}

/* 上下拖动：只有按在轨道上才生效，避免和右滑关闭互相误触 */
static void slider_press_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
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
    /* 表盘电量统一绿色；充电时只多一圈天蓝柔光，不改变电量主色。 */
    lv_color_t level = COL_GREEN;

    char text[24];
    if (s_bat_percent < 0) {
        snprintf(text, sizeof(text), "--");
    } else {
        snprintf(text, sizeof(text), "%d%%", s_bat_percent);
    }

    if (s_batt_pct != NULL) {
        if (s_bat_percent < 0) {
            lv_obj_add_flag(s_batt_pct, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(s_batt_pct, LV_OBJ_FLAG_HIDDEN);
        }
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
    if (s_status_bat != NULL) {
        if (s_bat_percent < 0) {
            lv_label_set_text(s_status_bat, s_usb_powered ? "USB供电" : "电量未知");
            lv_obj_set_style_text_color(s_status_bat, COL_MUTED, 0);
        } else if (s_charging) {
            if (s_bat_percent >= 100) {
                lv_label_set_text(s_status_bat, "已充满");
            } else {
                lv_label_set_text_fmt(s_status_bat, "充电中 %d%%", s_bat_percent);
            }
            lv_obj_set_style_text_color(s_status_bat, COL_RING_SKY, 0);
        } else if (s_usb_powered) {
            lv_label_set_text_fmt(s_status_bat, s_bat_percent >= 100 ? "已充满 %d%%" : "USB供电 %d%%", s_bat_percent);
            lv_obj_set_style_text_color(s_status_bat, COL_RING_SKY, 0);
        } else {
            lv_label_set_text(s_status_bat, text);
            lv_obj_set_style_text_color(s_status_bat, COL_GREEN, 0);
        }
    }
}

void ui_update_battery(int percent, bool charging, bool usb_powered)
{
    if (s_power_override >= 0) {
        usb_powered = (s_power_override == 1);
        charging = usb_powered;
    }
    if (percent != s_bat_percent || charging != s_charging || usb_powered != s_usb_powered)
        s_home_snap_dirty = true;
    s_bat_percent = percent;
    s_charging = charging;
    s_usb_powered = usb_powered;

    battery_visuals();
}

static void battery_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    ui_update_battery(power_battery_percent(), power_is_charging(), power_is_vbus());
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
    if (tm_now.tm_year + 1900 < 2024) {
        if (s_last_h != -1) s_home_snap_dirty = true;
        lv_obj_add_flag(s_time, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_date, "等待校时");
        s_last_h = s_last_m = s_last_day = -1;
        return;
    }
    lv_obj_remove_flag(s_time, LV_OBJ_FLAG_HIDDEN);
    if (tm_now.tm_hour != s_last_h || tm_now.tm_min != s_last_m) {
        s_home_snap_dirty = true;
        lv_label_set_text_fmt(s_time, "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
        s_last_h = tm_now.tm_hour;
        s_last_m = tm_now.tm_min;
    }
    int date_key = tm_now.tm_year * 366 + tm_now.tm_yday;
    if (date_key != s_last_day) {
        s_home_snap_dirty = true;
        lv_label_set_text_fmt(s_date, "%d月%d日 %s", tm_now.tm_mon + 1,
                              tm_now.tm_mday, s_week_cn[tm_now.tm_wday]);
        s_last_day = date_key;
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
    static int previous_state = -1;
    if ((int)state != previous_state) {
        s_home_snap_dirty = true;
        previous_state = state;
    }

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

/* 除控制中心外是否还有覆盖页处于打开状态 */
static bool pages_busy_except_center(void)
{
    return s_status_open || s_slider_open || s_torch_open || s_apps_open ||
           s_pet_open || s_wifi_open || s_wifipwd_open || s_tools_open ||
           s_notice_open;
}

/* 表盘快照只在内容变化且手指离开时预热，拖动入口不做昂贵重绘。 */
static void home_snap_refresh(void)
{
    if (s_home_snap_showing || !s_home_snap_dirty) return;
    /* 失败后等内容变化或下一次拖动再试，避免空闲轮询反复申请大块内存。 */
    s_home_snap_dirty = false;
    if (!s_home_snap) s_home_snap = lv_snapshot_create_draw_buf(s_home, LV_COLOR_FORMAT_RGB565);
    if (!s_home_snap || lv_snapshot_take_to_draw_buf(s_home, LV_COLOR_FORMAT_RGB565, s_home_snap) != LV_RESULT_OK) {
        s_home_snap_valid = false;
        ESP_LOGW(TAG, "home snapshot unavailable; using live background");
        return;
    }
    lv_image_cache_drop(s_home_snap);
    lv_image_set_src(s_home_snap_img, s_home_snap);
    s_home_snap_valid = true;
}

static void home_snap_begin(void)
{
    if (s_home_snap_showing || !s_home_snap_img) return;
    if (!s_home_snap_valid) {
        s_home_snap_dirty = true;
        home_snap_refresh();
    }
    if (!s_home_snap_valid) return;
    lv_obj_add_flag(s_home, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_home_snap_img, LV_OBJ_FLAG_HIDDEN);
    s_home_snap_showing = true;
}

static void home_snap_end(void)
{
    if (!s_home_snap_showing) return;
    /* shortcut: 恢复实时表盘仍有一次长帧；需降低收尾延迟时再改为常驻表盘缓存。 */
    lv_obj_add_flag(s_home_snap_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_home, LV_OBJ_FLAG_HIDDEN);
    s_home_snap_showing = false;
}

static void center_snap_invalidate(void)
{
    s_center_snap_valid = false;
    s_center_snap_ext = 0;
    if (s_center_snap_img != NULL) {
        /* 先断开图源，避免刷新时读到已释放的 PSRAM */
        lv_image_set_src(s_center_snap_img, NULL);
    }
    if (s_center_snap != NULL) {
        lv_draw_buf_destroy(s_center_snap);
        s_center_snap = NULL;
    }
}

/* 预渲染控制中心整页：内容全是静态图标，只需在睡眠开关改样式后重建。
 * 面板处于隐藏状态时快照只画面板自身与子磁贴，不会把下层表盘混进来。 */
static void center_snap_ensure(void)
{
    if (s_center_snap_valid) {
        return;
    }
    if (s_center == NULL || s_center_snap_img == NULL) {
        return;
    }
    center_snap_invalidate();
    s_center_snap = lv_snapshot_take(s_center, LV_COLOR_FORMAT_RGB565);
    if (s_center_snap == NULL) {
        ESP_LOGW(TAG, "center snapshot failed, drag falls back to live panel");
        return;
    }
    s_center_snap_ext = lv_obj_get_ext_draw_size(s_center);
    lv_image_set_src(s_center_snap_img, s_center_snap);
    lv_obj_set_pos(s_center_snap_img, -s_center_snap_ext,
                   s_center_reveal - SCREEN_H - s_center_snap_ext);
    s_center_snap_valid = true;
    ESP_LOGI(TAG, "center snapshot ready %dx%d ext=%d",
             (int)s_center_snap->header.w, (int)s_center_snap->header.h,
             s_center_snap_ext);
}

/* 当前负责跟手的“抽屉”：优先用快照图，失败时退回真实面板 */
static lv_obj_t *center_drawer_obj(void)
{
    return s_center_snap_valid ? s_center_snap_img : s_center;
}

static void center_anim_stop(void)
{
    lv_anim_delete(s_center, anim_center_h_cb);
    if (s_center_snap_img != NULL) {
        lv_anim_delete(s_center_snap_img, anim_center_h_cb);
    }
}

/* 开始跟手：快照可用时隐藏真实面板，只让一张图跟着手指走。
 * 真实面板的磁贴加过 PRESS_LOCK，事件仍会持续冒泡，不会因为面板隐藏而断掉拖动。 */
static void center_drawer_begin(void)
{
    home_snap_begin();
    if (s_center_snap_valid && s_center_snap_img != NULL) {
        lv_obj_add_flag(s_center, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_center_snap_img, -s_center_snap_ext,
                       s_center_reveal - SCREEN_H - s_center_snap_ext);
        lv_obj_remove_flag(s_center_snap_img, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_center, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(s_center, s_center_reveal - SCREEN_H);
    }
}

/* 抽屉落定：展开 → 真实磁贴接管交互；收起 → 两层都隐藏 */
static void center_drawer_finish(bool open)
{
    home_snap_end();
    if (s_center_snap_img != NULL) {
        lv_obj_add_flag(s_center_snap_img, LV_OBJ_FLAG_HIDDEN);
    }
    if (open) {
        s_center_open = true;
        s_center_reveal = SCREEN_H;
        lv_obj_set_y(s_center, 0);
        lv_obj_remove_flag(s_center, LV_OBJ_FLAG_HIDDEN);
    } else {
        s_center_open = false;
        s_center_reveal = 0;
        lv_obj_add_flag(s_center, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(s_center, -SCREEN_H);
    }
}

static void center_hide_done(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    if (!s_center_open) {
        center_drawer_finish(false);
    }
}

static void center_open_done(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    if (s_center_open) {
        center_drawer_finish(true);
    }
}

/* 直接把抽屉挪到指定滑出距离（拖动时用，不带动画、不做布局） */
static void center_reveal_set(int h)
{
    if (h < 0) {
        h = 0;
    }
    if (h > SCREEN_H) {
        h = SCREEN_H;
    }
    s_center_reveal = h;
    lv_obj_set_y(center_drawer_obj(), h - SCREEN_H - s_center_snap_ext);
}

static void center_animate_reveal(int32_t to, uint32_t dur,
                                  lv_anim_path_cb_t path,
                                  lv_anim_completed_cb_t done)
{
    lv_obj_t *drawer = center_drawer_obj();
    center_anim_stop();
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, drawer);
    lv_anim_set_values(&a, s_center_reveal, to);
    lv_anim_set_duration(&a, dur);
    lv_anim_set_path_cb(&a, path);
    lv_anim_set_exec_cb(&a, anim_center_h_cb);
    if (done != NULL) {
        lv_anim_set_completed_cb(&a, done);
    }
    lv_anim_start(&a);
    page_anim_lock(dur + 60);
}

static void center_open(void)
{
    if (s_center == NULL || s_center_open) {
        return;
    }
    /* 打开控制中心前收起所有覆盖页，保证同一时刻只有一个界面 */
    notice_close_now();
    status_close_now();
    slider_close_now();
    torch_close_now();
    apps_close_now();
    wifi_close_now();
    pet_close_now();
    nursery_close_now();

    s_center_open = true;

    center_snap_ensure();
    center_anim_stop();
    center_drawer_begin();
    center_animate_reveal(SCREEN_H, 240, lv_anim_path_ease_out, center_open_done);
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

    /* 从展开状态收起：先把显示切到快照图，动画期间只移动一张图 */
    center_snap_ensure();
    center_drawer_begin();
    center_animate_reveal(0, 240, lv_anim_path_ease_in, center_hide_done);
    ESP_LOGI(TAG, "center close");
}

static void center_consume(void)
{
    home_snap_end();
    if (s_center == NULL) {
        return;
    }
    s_center_open = false;
    s_center_dragging = false;
    s_center_drag_engaged = false;
    center_anim_stop();
    if (s_center_snap_img != NULL) {
        lv_obj_add_flag(s_center_snap_img, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(s_center, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(s_center, -SCREEN_H);
    s_center_reveal = 0;
}
static void center_close_now(void)
{
    status_close_now();
    slider_close_now();
    torch_close_now();
    notice_close_now();
    center_consume();
}

/* -------------------------------------------------------------------------- */
/* 底部通知抽屉                                                                */
/* -------------------------------------------------------------------------- */

static void anim_notice_h_cb(void *var, int32_t value)
{
    LV_UNUSED(var);
    if (value < 0) value = 0;
    if (value > NOTICE_H) value = NOTICE_H;
    s_notice_reveal = value;
    lv_obj_set_y(s_notice, SCREEN_H - value);
}

static void notice_anim_stop(void)
{
    if (s_notice != NULL) lv_anim_delete(s_notice, anim_notice_h_cb);
}

static void notice_reveal_set(int h)
{
    if (h < 0) h = 0;
    if (h > NOTICE_H) h = NOTICE_H;
    s_notice_reveal = h;
    lv_obj_set_y(s_notice, SCREEN_H - h);
}

static void notice_animate_reveal(int32_t to, uint32_t dur,
                                  lv_anim_path_cb_t path,
                                  lv_anim_completed_cb_t done)
{
    if (s_notice == NULL) return;
    notice_anim_stop();
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_notice);
    lv_anim_set_values(&a, s_notice_reveal, to);
    lv_anim_set_duration(&a, dur);
    lv_anim_set_path_cb(&a, path);
    lv_anim_set_exec_cb(&a, anim_notice_h_cb);
    if (done != NULL) lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
    page_anim_lock(dur + 60);
}

static void notice_open_done(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    if (!s_notice_open) return;
    s_notice_reveal = NOTICE_H;
    lv_obj_set_y(s_notice, SCREEN_H - NOTICE_H);
}

static void notice_hide_done(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    if (s_notice_open) return;
    s_notice_reveal = 0;
    lv_obj_set_y(s_notice, SCREEN_H);
    lv_obj_add_flag(s_notice, LV_OBJ_FLAG_HIDDEN);
}

static void notice_open(void)
{
    if (s_notice == NULL || s_notice_open) return;
    /* 通知抽屉与控制中心同级：打开前把其它覆盖页收干净 */
    status_close_now();
    slider_close_now();
    torch_close_now();
    apps_close_now();
    wifi_close_now();
    pet_close_now();
    nursery_close_now();
    center_consume();
    s_notice_dragging = false;
    s_notice_drag_engaged = false;
    s_notice_open = true;
    notice_anim_stop();
    lv_obj_remove_flag(s_notice, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_notice);
    notice_animate_reveal(NOTICE_H, 240, lv_anim_path_ease_out, notice_open_done);
    ESP_LOGI(TAG, "notice open");
}

static void notice_close(void)
{
    if (s_notice == NULL || !s_notice_open) return;
    s_notice_open = false;
    notice_animate_reveal(0, 220, lv_anim_path_ease_in, notice_hide_done);
    ESP_LOGI(TAG, "notice close");
}

static void notice_close_now(void)
{
    if (s_notice == NULL) return;
    s_notice_open = false;
    s_notice_dragging = false;
    s_notice_drag_engaged = false;
    notice_anim_stop();
    s_notice_reveal = 0;
    lv_obj_set_y(s_notice, SCREEN_H);
    lv_obj_add_flag(s_notice, LV_OBJ_FLAG_HIDDEN);
}

/* 通知抽屉内下滑收起：竖直方向占优才接管，避免横向误触。 */
static void notice_drag_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL || !s_notice_open || page_anim_busy()) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        notice_anim_stop();
        s_notice_dragging = true;
        s_notice_drag_engaged = false;
        s_notice_press_x = p.x;
        s_notice_press_y = p.y;
        s_notice_press_reveal = s_notice_reveal;
    } else if (code == LV_EVENT_PRESSING) {
        if (!s_notice_dragging) return;
        int dx = p.x - s_notice_press_x;
        if (dx < 0) dx = -dx;
        int dy = p.y - s_notice_press_y;
        if (!s_notice_drag_engaged) {
            if (dy <= CENTER_DRAG_TH || dy < dx) return;
            s_notice_drag_engaged = true;
        }
        notice_reveal_set(s_notice_press_reveal - dy);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!s_notice_dragging) return;
        s_notice_dragging = false;
        if (!s_notice_drag_engaged) return;
        s_notice_drag_engaged = false;
        if (s_notice_reveal < NOTICE_H / 2) {
            notice_close();
        } else {
            notice_animate_reveal(NOTICE_H, 160, lv_anim_path_ease_out, notice_open_done);
        }
    }
}

/* 与控制中心相同，拖动只搬运前景位图和表盘缓存，不重画控件/外环。 */
static void tool_snap_prepare(lv_obj_t *page)
{
    s_tool_snap_valid = false;
    if (s_tool_snap_img == NULL) {
        s_tool_snap_img = lv_image_create(s_screen);
        lv_obj_remove_flag(s_tool_snap_img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_tool_snap_img, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_tool_snap == NULL) s_tool_snap = lv_snapshot_create_draw_buf(page, LV_COLOR_FORMAT_RGB565);
    if (s_tool_snap == NULL ||
        lv_snapshot_take_to_draw_buf(page, LV_COLOR_FORMAT_RGB565, s_tool_snap) != LV_RESULT_OK) {
        ESP_LOGW(TAG, "tool snapshot unavailable; using live page");
        return;
    }
    s_tool_snap_ext = lv_obj_get_ext_draw_size(page);
    lv_image_cache_drop(s_tool_snap);
    lv_image_set_src(s_tool_snap_img, s_tool_snap);
    s_tool_snap_valid = true;
    home_snap_refresh();
}

static void tool_drawer_x_cb(void *page, int32_t value)
{
    s_tool_x = value;
    if (s_tool_snap_showing)
        lv_obj_set_pos(s_tool_snap_img, value - s_tool_snap_ext, -s_tool_snap_ext);
    else lv_obj_set_x(page, value);
}

static void tool_drawer_begin(lv_obj_t *page)
{
    s_tool_x = 0;
    if (!s_apps_open && !s_nursery_open) home_snap_begin();
    if (s_tool_snap_valid) {
        lv_obj_set_pos(s_tool_snap_img, -s_tool_snap_ext, -s_tool_snap_ext);
        lv_obj_move_foreground(s_tool_snap_img);
        lv_obj_remove_flag(s_tool_snap_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
        s_tool_snap_showing = true;
    }
}

static void tool_drawer_end(lv_obj_t *page, bool closed)
{
    lv_anim_delete(page, tool_drawer_x_cb);
    if (s_tool_snap_img) lv_obj_add_flag(s_tool_snap_img, LV_OBJ_FLAG_HIDDEN);
    s_tool_snap_showing = false;
    lv_obj_set_x(page, s_tool_x);
    if (!closed) lv_obj_remove_flag(page, LV_OBJ_FLAG_HIDDEN);
    home_snap_end();
}

static void tool_open_done(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    /* 目标页已覆盖屏幕后再收起控制中心，进入途中不露出表盘。 */
    center_consume();
    tool_snap_prepare(anim->var);
}

/* 表盘跟手下拉：控制中心顶边贴住屏幕顶部，露出高度直接跟手指走 */
static void home_pull_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_event_get_indev(e);
    /* 任一覆盖页（含正在滑出的页面）存在时都不接受下拉，避免同屏叠页 */
    if (indev == NULL || s_center_open || pages_busy_except_center() || page_anim_busy()) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        /* 下拉只在表盘顶部 1/12 起手：中部和边缘横向滑动留给其它手势，
         * 避免从右往左滑时被误判成下拉控制中心。 */
        s_center_dragging = false;
        s_center_drag_engaged = false;
        if (p.y > SCREEN_H / 12) {
            return;
        }
        center_anim_stop();
        s_center_dragging = true;
        s_center_drag_engaged = false;
        s_center_press_x = p.x;
        s_center_press_y = p.y;
        s_center_press_reveal = s_center_reveal;
    } else if (code == LV_EVENT_PRESSING) {
        if (!s_center_dragging || s_nur_drag_engaged) {
            return;
        }
        int dx = p.x - s_center_press_x;
        if (dx < 0) dx = -dx;
        int dy = p.y - s_center_press_y;
        if (!s_center_drag_engaged) {
            /* 竖向位移必须明显大于横向，横滑不抢手势 */
            if (dy <= CENTER_DRAG_TH || dy < dx) {
                return;
            }
            s_center_drag_engaged = true;
            /* 首次越过阈值才准备快照：此后每帧只移动这一张图 */
            center_snap_ensure();
            center_drawer_begin();
        }
        center_reveal_set(s_center_press_reveal + dy);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!s_center_dragging) {
            return;
        }
        s_center_dragging = false;
        if (!s_center_drag_engaged) {
            return;
        }
        if (s_center_reveal >= SCREEN_H / 4) {
            /* 拉过 1/4 就吸附展开，从当前高度继续滑到位 */
            center_open();
        } else {
            /* 没拉够，回弹收起 */
            s_center_open = false;
            center_animate_reveal(0, 200, lv_anim_path_ease_in, center_hide_done);
        }
    }
}

/* 表盘底部上拉：通知抽屉跟手滑出，与控制中心下拉对称。
 * 只从底部 1/12 起手，且竖向位移必须大于横向。 */
static void home_notice_pull_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL || s_notice_open || s_center_open || s_center_dragging ||
        s_nur_drag_engaged || pages_busy_except_center() || page_anim_busy()) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        s_notice_dragging = false;
        s_notice_drag_engaged = false;
        if (p.y < SCREEN_H - NOTICE_EDGE_H) {
            return;
        }
        notice_anim_stop();
        s_notice_dragging = true;
        s_notice_drag_engaged = false;
        s_notice_press_x = p.x;
        s_notice_press_y = p.y;
        s_notice_press_reveal = s_notice_reveal;
    } else if (code == LV_EVENT_PRESSING) {
        if (!s_notice_dragging) {
            return;
        }
        int dx = p.x - s_notice_press_x;
        if (dx < 0) dx = -dx;
        int dy = s_notice_press_y - p.y;   /* 上拉为正 */
        if (!s_notice_drag_engaged) {
            if (dy <= CENTER_DRAG_TH || dy < dx) {
                return;
            }
            s_notice_drag_engaged = true;
            lv_obj_remove_flag(s_notice, LV_OBJ_FLAG_HIDDEN);
        }
        notice_reveal_set(s_notice_press_reveal + dy);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!s_notice_dragging) {
            return;
        }
        s_notice_dragging = false;
        if (!s_notice_drag_engaged) {
            return;
        }
        if (s_notice_reveal >= NOTICE_H / 4) {
            notice_open();
        } else {
            s_notice_open = false;
            notice_animate_reveal(0, 200, lv_anim_path_ease_in, notice_hide_done);
        }
    }
}

/* 控制中心跟手上滑收起；向下拖仍在屏幕内时回弹展开 */
static void center_drag_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL || !s_center_open || pages_busy_except_center() ||
        page_anim_busy()) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        center_anim_stop();
        s_center_dragging = true;
        s_center_drag_engaged = false;
        s_center_press_y = p.y;
        s_center_press_reveal = s_center_reveal;
    } else if (code == LV_EVENT_PRESSING) {
        if (!s_center_dragging) {
            return;
        }
        int dy = p.y - s_center_press_y;
        if (!s_center_drag_engaged) {
            if (dy >= -CENTER_DRAG_TH) {
                return;
            }
            s_center_drag_engaged = true;
            /* 收起方向也切到快照图：整屏只移动一张图，不重绘磁贴 */
            center_snap_ensure();
            center_drawer_begin();
        }
        center_reveal_set(s_center_press_reveal + dy);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!s_center_dragging) {
            return;
        }
        s_center_dragging = false;
        if (!s_center_drag_engaged) {
            return;
        }
        if (s_center_reveal < SCREEN_H * 3 / 4) {
            center_close();
        } else {
            center_animate_reveal(SCREEN_H, 160, lv_anim_path_ease_out,
                                  center_open_done);
        }
    }
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
    /* 状态页与亮度/音量、手电同层互斥，保证同屏只有一个覆盖页 */
    slider_close_now();
    torch_close_now();
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
    lv_anim_set_completed_cb(&a, tool_open_done);
    lv_anim_start(&a);
    page_anim_lock(280 + 60);
    ESP_LOGI(TAG, "status open");
}

static void status_close(void)
{
    if (s_status == NULL || !s_status_open) {
        return;
    }
    if (s_tool_drag_page == s_status && s_tool_drag_engaged) {
        tool_drawer_end(s_status, false);
        s_tool_dragging = s_tool_drag_engaged = false;
    }
    s_status_open = false;

    lv_anim_delete(s_status, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_status);
    lv_anim_set_values(&a, lv_obj_get_x(s_status), SCREEN_W);
    lv_anim_set_duration(&a, 240);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_completed_cb(&a, status_hide_done);
    lv_anim_start(&a);
    page_anim_lock(240 + 60);
    ESP_LOGI(TAG, "status close");
}

static void status_close_now(void)
{
    if (s_status == NULL) {
        return;
    }
    s_status_open = false;
    if (s_tool_drag_page == s_status) {
        if (s_tool_drag_engaged) tool_drawer_end(s_status, true);
        s_tool_dragging = s_tool_drag_engaged = false;
    }
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
    /* 亮度/音量页与状态页、手电互斥，避免滑入过程中叠页 */
    status_close_now();
    torch_close_now();
    s_slider_kind = kind;
    s_slider_dragging = false;
    s_slider_open = true;

    /* 亮度 / 音量统一科技浅绿，页面上只保留当前值 */
    lv_color_t accent = COL_TECH_GREEN;
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
    lv_anim_set_completed_cb(&a, tool_open_done);
    lv_anim_start(&a);
    page_anim_lock(280 + 60);
    ESP_LOGI(TAG, "slider open kind=%d", kind);
}

static void slider_close(void)
{
    if (s_slider == NULL || !s_slider_open) {
        return;
    }
    if (s_slider_open && s_slider_kind == SLIDER_VOLUME) nursery_save_volume();
    if (s_tool_drag_page == s_slider && s_tool_drag_engaged) {
        tool_drawer_end(s_slider, false);
        s_tool_dragging = s_tool_drag_engaged = false;
    }
    s_slider_open = false;
    s_slider_dragging = false;

    lv_anim_delete(s_slider, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_slider);
    lv_anim_set_values(&a, lv_obj_get_x(s_slider), SCREEN_W);
    lv_anim_set_duration(&a, 240);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_completed_cb(&a, slider_hide_done);
    lv_anim_start(&a);
    page_anim_lock(240 + 60);
    ESP_LOGI(TAG, "slider close");
}

static void slider_close_now(void)
{
    if (s_slider == NULL) {
        return;
    }
    if (s_slider_open && s_slider_kind == SLIDER_VOLUME) nursery_save_volume();
    s_slider_open = false;
    s_slider_dragging = false;
    if (s_tool_drag_page == s_slider) {
        if (s_tool_drag_engaged) tool_drawer_end(s_slider, true);
        s_tool_dragging = s_tool_drag_engaged = false;
    }
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
    /* 手电与状态页、亮度/音量页互斥 */
    status_close_now();
    slider_close_now();
    nursery_close_now();
    s_torch_open = true;
    s_torch_last_click = 0;

    /* 普通模式全亮；睡眠模式保持 10%，关闭后恢复当前模式亮度 */
    brightness_apply();
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
    brightness_apply();
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
        brightness_apply();
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
    wifi_close_now();
    pet_close_now();

    s_apps_open = true;
    lv_anim_delete(s_apps, NULL);
    lv_obj_remove_flag(s_apps, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_apps);
    s_app_drag_active = false;
    s_app_pan = (lv_point_t){0, 0};
    apps_layout_icons();
    lv_obj_set_style_opa(s_apps, LV_OPA_COVER, 0);
    lv_obj_set_y(s_apps, 0);
    page_anim_lock(120);
    ESP_LOGI(TAG, "apps open");
}

static void apps_close(void)
{
    if (s_apps == NULL || !s_apps_open) {
        return;
    }
    s_apps_open = false;
    s_app_drag_active = false;

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
    page_anim_lock(160 + 60);
    ESP_LOGI(TAG, "apps close");
}

static void apps_close_now(void)
{
    if (s_apps == NULL) {
        return;
    }
    s_apps_open = false;
    s_app_drag_active = false;
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
    /* 熄屏时不唤醒、不进入 Mimi Cat：只允许用户主动点亮屏幕 */
    if (s_screen_off) {
        return;
    }
    /* 宠物页为最上层覆盖页：先收起控制中心 / 状态页 / 应用桌面 */
    /* 儿歌与语音聊天共用 I2S：进宠物页前停止外放并收起儿歌页 */
    nursery_close_now();
    nursery_stop();
    center_close_now();
    status_close_now();
    apps_close_now();
    wifi_close_now();

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
    voice_client_set_active(false);

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
    voice_client_set_active(false);
    lv_anim_delete(s_pet, NULL);
    lv_obj_add_flag(s_pet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s_pet, LV_OPA_COVER, 0);
}

/* -------------------------------------------------------------------------- */
/* 3S 英语儿歌页 抽屉式滑入滑出                                                 */
/* -------------------------------------------------------------------------- */

/* 页面关闭后预渲染最近状态；
 * 下一次右滑只搬运这张位图，手感和控制中心下拉一致。 */
static void nursery_snap_refresh(void)
{
    if (s_nursery == NULL || s_nur_snap_img == NULL) {
        return;
    }
    s_nur_force_update = true;
    nursery_update_ui();
    s_nur_force_update = false;
    if (s_nur_snap == NULL) {
        s_nur_snap = lv_snapshot_create_draw_buf(s_nursery, LV_COLOR_FORMAT_RGB565);
    }
    if (s_nur_snap == NULL ||
        lv_snapshot_take_to_draw_buf(s_nursery, LV_COLOR_FORMAT_RGB565,
                                     s_nur_snap) != LV_RESULT_OK) {
        s_nur_snap_valid = false;
        s_nur_snap_ext = 0;
        ESP_LOGW(TAG, "nursery snapshot unavailable; drag uses live page");
        return;
    }
    s_nur_snap_ext = lv_obj_get_ext_draw_size(s_nursery);
    lv_image_cache_drop(s_nur_snap);
    lv_image_set_src(s_nur_snap_img, s_nur_snap);
    lv_obj_set_pos(s_nur_snap_img, -SCREEN_W - s_nur_snap_ext, -s_nur_snap_ext);
    s_nur_snap_valid = true;
}

static lv_obj_t *nursery_drawer_obj(void)
{
    return (s_nur_snap_valid && s_nur_snap_img != NULL) ? s_nur_snap_img : s_nursery;
}

/* 开始滑入：冻结下层表盘，只移动儿歌页快照（无快照时退回真实页面）。 */
static void nursery_drawer_begin(void)
{
    home_snap_begin();
    if (s_nur_snap_valid && s_nur_snap_img != NULL) {
        lv_obj_add_flag(s_nursery, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_nur_snap_img, s_nur_reveal - s_nur_snap_ext, -s_nur_snap_ext);
        lv_obj_remove_flag(s_nur_snap_img, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_nursery, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(s_nursery, s_nur_reveal);
    }
}

static void anim_nur_open_x_cb(void *obj, int32_t value)
{
    s_nur_reveal = (int)value;
    lv_obj_set_x((lv_obj_t *)obj, value - s_nur_snap_ext);
}

static void anim_nur_close_x_cb(void *obj, int32_t value)
{
    s_nur_reveal = (int)value;
    lv_obj_set_x((lv_obj_t *)obj, value);
}

/* 滑入阶段跟手：直接设置快照横向位置 */
static void nursery_set_reveal(int x)
{
    if (x < -SCREEN_W) {
        x = -SCREEN_W;
    } else if (x > 0) {
        x = 0;
    }
    s_nur_reveal = x;
    lv_obj_set_x(nursery_drawer_obj(), x - s_nur_snap_ext);
}

/* 滑出阶段跟手：移动真实页面，歌词与进度不会停在旧画面 */
static void nursery_set_reveal_real(int x)
{
    if (x < -SCREEN_W) {
        x = -SCREEN_W;
    } else if (x > 0) {
        x = 0;
    }
    s_nur_reveal = x;
    lv_obj_set_x(s_nursery, x);
}

static void nursery_open_done(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    home_snap_end();
    if (s_nur_snap_img != NULL) {
        lv_obj_add_flag(s_nur_snap_img, LV_OBJ_FLAG_HIDDEN);
    }
    s_nur_reveal = 0;
    lv_obj_set_x(s_nursery, 0);
    lv_obj_remove_flag(s_nursery, LV_OBJ_FLAG_HIDDEN);
    s_nursery_open = true;
    nursery_update_ui();
}

static void nursery_open_abort_done(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    home_snap_end();
    if (s_nur_snap_img != NULL) {
        lv_obj_add_flag(s_nur_snap_img, LV_OBJ_FLAG_HIDDEN);
    }
    s_nursery_open = false;
    s_nur_reveal = -SCREEN_W;
    lv_obj_add_flag(s_nursery, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(s_nursery, -SCREEN_W);
}

static void nursery_close_done(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    home_snap_end();
    s_nursery_open = false;
    s_nur_reveal = -SCREEN_W;
    lv_obj_add_flag(s_nursery, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(s_nursery, -SCREEN_W);
    nursery_card_set(0, false);
    /* 内容已定格，立刻给下一次右滑预渲染 */
    s_nur_snap_valid = false;
    s_nur_snap_ext = 0;
    nursery_snap_refresh();
}

static void nursery_close_abort_done(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    home_snap_end();
    s_nur_reveal = 0;
    lv_obj_set_x(s_nursery, 0);
    lv_obj_remove_flag(s_nursery, LV_OBJ_FLAG_HIDDEN);
    s_nursery_open = true;
}

static void nursery_open(void)
{
    if (s_nursery == NULL || s_nursery_open || s_screen_off) {
        return;
    }
    /* 儿歌播放与语音聊天共用 I2S：进入儿歌页先停掉录音，避免抢总线 */
    voice_client_set_active(false);
    center_close_now();
    status_close_now();
    slider_close_now();
    torch_close_now();
    apps_close_now();
    wifi_close_now();
    pet_close_now();

    s_nur_dragging = false;
    s_nur_drag_engaged = false;
    nursery_card_set(0, false);
    s_nur_catalog_seen = nursery_catalog_ready();
    lv_obj_set_pos(s_nur_player_card, NUR_CARD_X, NUR_CARD_PLAYER_Y);
    lv_obj_set_pos(s_nur_week_card, NUR_CARD_X, NUR_CARD_WEEK_Y);
    /* 复用空闲时预热的快照，避免按下后临时渲染整页。 */
    s_nur_reveal = -SCREEN_W;
    if (!s_nur_snap_valid) {
        nursery_snap_refresh();
    }

    nursery_drawer_begin();

    lv_obj_t *drawer = nursery_drawer_obj();
    lv_anim_delete(drawer, anim_nur_open_x_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, drawer);
    lv_anim_set_values(&a, s_nur_reveal, 0);
    lv_anim_set_duration(&a, 240);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_nur_open_x_cb);
    lv_anim_set_completed_cb(&a, nursery_open_done);
    lv_anim_start(&a);
    page_anim_lock(240 + 60);
    ESP_LOGI(TAG, "nursery open");
}

static void nursery_close(void)
{
    if (s_nursery == NULL || !s_nursery_open) {
        return;
    }
    /* 关闭移动真实页面，保持播放进度/歌词与当前状态一致 */
    home_snap_begin();
    lv_anim_delete(s_nursery, anim_nur_close_x_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_nursery);
    lv_anim_set_values(&a, s_nur_reveal, -SCREEN_W);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, anim_nur_close_x_cb);
    lv_anim_set_completed_cb(&a, nursery_close_done);
    lv_anim_start(&a);
    page_anim_lock(220 + 60);
    ESP_LOGI(TAG, "nursery close");
}

static void nursery_close_now(void)
{
    if (s_nursery == NULL) {
        return;
    }
    bool was_open = s_nursery_open;
    s_nur_dragging = false;
    s_nur_drag_engaged = false;
    s_nursery_open = false;
    lv_anim_delete(s_nursery, anim_nur_open_x_cb);
    lv_anim_delete(s_nursery, anim_nur_close_x_cb);
    if (s_nur_snap_img != NULL) {
        lv_anim_delete(s_nur_snap_img, anim_nur_open_x_cb);
        lv_obj_add_flag(s_nur_snap_img, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(s_nursery, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(s_nursery, -SCREEN_W);
    s_nur_reveal = -SCREEN_W;
    if (was_open) {
        nursery_card_set(0, false);
        /* 页面确实打开过才需要重拍；只是被别的页面顺手收起时保留原快照 */
        s_nur_snap_valid = false;
        s_nur_snap_ext = 0;
        nursery_snap_refresh();
    }
}

/* -------------------------------------------------------------------------- */
/* 3S 英语儿歌页 手势（跟手抽屉）                                               */
/* -------------------------------------------------------------------------- */

/* 表盘右滑：跟手拉出儿歌页，超过 1/4 吸附打开。 */
static void home_nursery_drag_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        return;
    }
    if (s_screen_off || s_nursery_open || page_anim_busy() ||
        s_center_open || pages_busy_except_center()) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        s_nur_dragging = true;
        s_nur_drag_engaged = false;
        s_nur_press_x = p.x;
        s_nur_press_y = p.y;
    } else if (code == LV_EVENT_PRESSING) {
        if (!s_nur_dragging) {
            return;
        }
        int dx = p.x - s_nur_press_x;
        int dy = p.y - s_nur_press_y;
        if (!s_nur_drag_engaged) {
            /* 下拉已经接管（斜向下滑）时不抢手势 */
            if (s_center_drag_engaged) {
                return;
            }
            if (dx < NUR_DRAG_TH || LV_ABS(dx) <= LV_ABS(dy)) {
                return;
            }
            s_nur_drag_engaged = true;
            nursery_drawer_begin();
        }
        nursery_set_reveal(-SCREEN_W + dx);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!s_nur_dragging) {
            return;
        }
        s_nur_dragging = false;
        if (!s_nur_drag_engaged) {
            return;
        }
        s_nur_drag_engaged = false;

        lv_obj_t *drawer = nursery_drawer_obj();
        lv_anim_delete(drawer, anim_nur_open_x_cb);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, drawer);
        if (s_nur_reveal >= -SCREEN_W * 3 / 4) {
            /* 拉过 1/4 就吸附打开，从当前位置继续滑到位 */
            lv_anim_set_values(&a, s_nur_reveal, 0);
            lv_anim_set_duration(&a, 220);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_set_completed_cb(&a, nursery_open_done);
        } else {
            lv_anim_set_values(&a, s_nur_reveal, -SCREEN_W);
            lv_anim_set_duration(&a, 180);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
            lv_anim_set_completed_cb(&a, nursery_open_abort_done);
        }
        lv_anim_set_exec_cb(&a, anim_nur_open_x_cb);
        lv_anim_start(&a);
        page_anim_lock(220 + 60);
    }
}

/* 慢滑兜底：个别触摸序列只给 GESTURE 不给 PRESSING，
 * 这里再补一次右滑打开；跟手拖动已经接管时绝不重复触发。 */
static void home_nursery_gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
        return;
    }
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL || lv_indev_get_gesture_dir(indev) != LV_DIR_RIGHT) {
        return;
    }
    if (s_screen_off || s_nursery_open || s_nur_drag_engaged ||
        s_center_drag_engaged || s_center_open || page_anim_busy() ||
        pages_busy_except_center()) {
        return;
    }
    nursery_open();
}

/* 儿歌页左滑：跟手退回表盘，超过 1/4 吸附退出。 */
static void nursery_page_drag_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL || !s_nursery_open || s_screen_off || page_anim_busy()) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        s_nur_dragging = true;
        s_nur_drag_engaged = false;
        s_nur_press_x = p.x;
        s_nur_press_y = p.y;
    } else if (code == LV_EVENT_PRESSING) {
        if (!s_nur_dragging) {
            return;
        }
        int dx = p.x - s_nur_press_x;
        int dy = p.y - s_nur_press_y;
        if (!s_nur_drag_engaged) {
            if (dx > -NUR_DRAG_TH || LV_ABS(dx) <= LV_ABS(dy)) {
                return;
            }
            s_nur_drag_engaged = true;
            home_snap_begin();
        }
        nursery_set_reveal_real(dx);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!s_nur_dragging) {
            return;
        }
        s_nur_dragging = false;
        if (!s_nur_drag_engaged) {
            return;
        }
        s_nur_drag_engaged = false;

        lv_anim_delete(s_nursery, anim_nur_close_x_cb);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_nursery);
        if (s_nur_reveal <= -SCREEN_W / 4) {
            lv_anim_set_values(&a, s_nur_reveal, -SCREEN_W);
            lv_anim_set_duration(&a, 200);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_set_completed_cb(&a, nursery_close_done);
        } else {
            lv_anim_set_values(&a, s_nur_reveal, 0);
            lv_anim_set_duration(&a, 180);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
            lv_anim_set_completed_cb(&a, nursery_close_abort_done);
        }
        lv_anim_set_exec_cb(&a, anim_nur_close_x_cb);
        lv_anim_start(&a);
        page_anim_lock(200 + 60);
    }
}

/* -------------------------------------------------------------------------- */
/* 磁贴内容绘制                                                                */
/* -------------------------------------------------------------------------- */

/* 控制中心磁贴：纯图标，不放任何文字 */
static lv_obj_t *tile_create(lv_obj_t *parent, int cx, int cy, lv_color_t accent)
{
    lv_obj_t *root = mk_circle(parent, cx, cy, TILE_D, COL_SURF, LV_OPA_COVER);
    lv_obj_set_style_border_width(root, 1, 0);
    lv_obj_set_style_border_color(root, COL_LINE, 0);
    lv_obj_set_style_border_opa(root, LV_OPA_50, 0);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    /* 整页拖动时面板会被快照图顶替，锁定按下的磁贴，PRESSING 才能持续冒泡到控制中心 */
    lv_obj_add_flag(root, LV_OBJ_FLAG_PRESS_LOCK);
    /* 让 PRESSED/PRESSING 冒泡到控制中心，整页都能跟手上滑收起 */
    lv_obj_add_flag(root, LV_OBJ_FLAG_EVENT_BUBBLE);
    /* 外阴影在整页拖动时每帧重绘，很吃性能；用 accent 描边保留层次感 */
    lv_obj_set_style_border_color(root, accent, 0);
    lv_obj_set_style_border_opa(root, LV_OPA_30, 0);
    return root;
}

static void draw_sleep_icon(lv_obj_t *parent, bool on)
{
    const int cx = TILE_D / 2, cy = TILE_D / 2;
    if (on) {
        mk_circle(parent, cx - 3, cy, 34, COL_TEXT, LV_OPA_COVER);
        mk_circle(parent, cx + 7, cy - 8, 32, COL_SLEEP_ACTIVE, LV_OPA_COVER);
        mk_box(parent, cx + 12, cy - 18, 9, 3, COL_TEXT, LV_OPA_COVER, 1);
        mk_box(parent, cx + 15, cy - 21, 3, 9, COL_TEXT, LV_OPA_COVER, 1);
        mk_circle(parent, cx + 18, cy + 1, 4, COL_TEXT, LV_OPA_COVER);
        mk_circle(parent, cx + 24, cy + 24, 17, COL_TEXT, LV_OPA_COVER);
        static const lv_point_precise_t check[] = {{42, 46}, {45, 49}, {51, 42}};
        icon_line(parent, check, 3, cx, cy, COL_SLEEP_ACTIVE);
    } else {
        /* 原 14 点折线做 Catmull-Rom 平滑采样（1px 精度），轮廓尺寸不变，只把折角磨圆。 */
        static const lv_point_precise_t moon[] = {
            {25, 2}, {24, 2}, {23, 2}, {21, 2}, {19, 3}, {18, 3},
            {16, 4}, {15, 5}, {13, 6}, {12, 7}, {10, 8}, {9, 10},
            {8, 11}, {7, 12}, {6, 14}, {6, 16}, {5, 18}, {5, 19},
            {5, 21}, {5, 23}, {6, 24}, {6, 26}, {7, 27}, {8, 29},
            {9, 30}, {10, 31}, {11, 32}, {13, 34}, {14, 34}, {16, 35},
            {17, 36}, {19, 37}, {20, 37}, {22, 37}, {24, 37}, {25, 37},
            {27, 37}, {28, 37}, {30, 36}, {31, 35}, {33, 34}, {34, 33},
            {35, 32}, {36, 31}, {37, 29}, {38, 28}, {39, 26}, {39, 25},
            {39, 24}, {38, 24}, {37, 24}, {36, 25}, {34, 25}, {32, 26},
            {31, 26}, {30, 26}, {28, 26}, {27, 25}, {25, 24}, {24, 24},
            {23, 23}, {22, 22}, {21, 21}, {20, 20}, {19, 19}, {18, 17},
            {18, 16}, {18, 15}, {18, 14}, {18, 12}, {18, 11}, {19, 10},
            {19, 9}, {20, 8}, {21, 6}, {23, 5}, {24, 4}, {25, 3},
        };
        icon_line(parent, moon, 78, cx, cy, COL_PURPLE);
        mk_circle(parent, cx + 24, cy + 24, 15, COL_LINE, LV_OPA_COVER);
        mk_box(parent, cx + 20, cy + 23, 8, 2, COL_MUTED, LV_OPA_COVER, 1);
    }
}

/* -------------------------------------------------------------------------- */
/* 事件                                                                        */
/* -------------------------------------------------------------------------- */

/* 表盘下滑已改为 home_pull_cb 跟手处理，不再依赖手势识别 */

static void center_status_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    status_open();
}



static void center_battery_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    status_open();
}

static void center_bright_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    slider_open(SLIDER_BRIGHT);
}

static void center_volume_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    slider_open(SLIDER_VOLUME);
}

/* 控制中心手电：整屏纯白，睡眠模式仍锁定 10% */
static void center_torch_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    torch_open();
    center_consume();
}

/* 睡眠模式：表盘只保留暗灰色时间，外环 / 电量 / 天气 / 日期 / Wi-Fi 全部隐藏。
 * 下拉手势仍作用在表盘根对象上，所以睡眠模式下照样能拉出控制中心再点回正常。 */
static void sleep_mode_apply(bool on)
{
    s_sleep_mode = on;
    s_home_snap_dirty = true;

    if (s_dial_deco != NULL) {
        if (on) {
            lv_obj_add_flag(s_dial_deco, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(s_dial_deco, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_time != NULL) {
        lv_obj_set_style_text_color(s_time, on ? lv_color_hex(0x9E9E9E) : COL_TEXT, 0);
    }
    /* 磁贴用边框亮度表示当前状态 */
    if (s_cc_sleep != NULL) {
        lv_obj_set_style_border_opa(s_cc_sleep, on ? LV_OPA_COVER : LV_OPA_30, 0);
        lv_obj_set_style_bg_color(s_cc_sleep, on ? COL_SLEEP_ACTIVE : COL_SURF, 0);
    }
    if (s_cc_sleep_off != NULL) {
        if (on) {
            lv_obj_add_flag(s_cc_sleep_off, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(s_cc_sleep_off, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_cc_sleep_on != NULL) {
        if (on) {
            lv_obj_remove_flag(s_cc_sleep_on, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_cc_sleep_on, LV_OBJ_FLAG_HIDDEN);
        }
    }
    /* 睡眠表盘只压暗背光，不改用户亮度设定 */
    brightness_apply();
    /* 磁贴外观变了：立刻重建抽屉快照，避免下次拖动看到旧状态 */
    if (s_center_snap_valid) {
        center_snap_invalidate();
        center_snap_ensure();
    }
    home_snap_refresh();
    ESP_LOGI(TAG, "sleep mode %s", on ? "on" : "off");
}

/* 点睡眠磁贴：切换后收起控制中心，直接看到表盘变化 */
static void center_sleep_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    sleep_mode_apply(!s_sleep_mode);
    center_close();
}

/* 控制中心不再放 Mimi Cat 磁贴：宠物页只保留 BOOT 短按入口 */

static void status_back_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    status_close();
}

static void tool_back_done(lv_anim_t *anim)
{
    lv_obj_t *page = anim->var;
    bool closed = s_tool_x != 0;
    tool_drawer_end(page, closed);
    s_tool_dragging = s_tool_drag_engaged = false;
    if (!closed) return;
    if (page == s_slider) slider_close_now();
    else status_close_now();
}

/* 横向拖多少，页面就移动多少；竖向动作及滑杆本体不触发返回。 */
static void tool_page_drag_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
        code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;
    lv_obj_t *page = lv_event_get_current_target(e);
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL || page_anim_busy() ||
        (page == s_slider ? !s_slider_open : !s_status_open)) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (code == LV_EVENT_PRESSED) {
        s_tool_drag_page = page;
        s_tool_dragging = true;
        s_tool_drag_engaged = false;
        s_tool_press = p;
        tool_snap_prepare(page);
    } else if (code == LV_EVENT_PRESSING) {
        if (!s_tool_dragging || s_tool_drag_page != page ||
            (page == s_slider && s_slider_dragging)) return;
        int dx = p.x - s_tool_press.x;
        int dy = p.y - s_tool_press.y;
        if (!s_tool_drag_engaged) {
            if (LV_ABS(dy) > CENTER_DRAG_TH && LV_ABS(dy) >= LV_ABS(dx)) {
                s_tool_dragging = false;
                return;
            }
            if (LV_ABS(dx) <= CENTER_DRAG_TH || LV_ABS(dx) <= LV_ABS(dy)) return;
            s_tool_drag_engaged = true;
            tool_drawer_begin(page);
        }
        tool_drawer_x_cb(page, LV_CLAMP(-SCREEN_W, dx, SCREEN_W));
    } else {
        if (!s_tool_dragging || s_tool_drag_page != page) return;
        s_tool_dragging = false;
        if (!s_tool_drag_engaged) return;
        int x = s_tool_x;
        int to = code == LV_EVENT_RELEASED && LV_ABS(x) >= SCREEN_W / 4 ?
                 (x < 0 ? -SCREEN_W : SCREEN_W) : 0;
        lv_anim_delete(page, tool_drawer_x_cb);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, page);
        lv_anim_set_values(&a, x, to);
        lv_anim_set_duration(&a, 180);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, tool_drawer_x_cb);
        lv_anim_set_completed_cb(&a, tool_back_done);
        lv_anim_start(&a);
        page_anim_lock(180 + 60);
    }
}

static void slider_back_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    slider_close();
}

/* -------------------------------------------------------------------------- */
/* 网络 App                                                                    */
/* -------------------------------------------------------------------------- */

/* 锁：半圆锁梁 + 锁体，表示需要密码 */
static void draw_lock(lv_obj_t *parent, int cx, int cy)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(arc, cx - 7, cy - 15);
    lv_obj_set_size(arc, 14, 14);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, COL_MUTED, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_bg_angles(arc, 180, 360);
    lv_arc_set_angles(arc, 0, 0);
    mk_box(parent, cx - 7, cy - 3, 14, 11, COL_MUTED, LV_OPA_COVER, 3);
}

/* 三格信号：按 RSSI 点亮 1~3 格 */
static void draw_signal_bars(lv_obj_t *parent, int x, int bottom_y, int rssi)
{
    int bars = rssi > -55 ? 3 : (rssi > -70 ? 2 : 1);
    static const int heights[3] = {6, 10, 14};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = mk_box(parent, x + i * 7, bottom_y - heights[i],
                             5, heights[i],
                             i < bars ? COL_GREEN : COL_LINE, LV_OPA_COVER, 2);
        lv_obj_set_style_bg_opa(b, i < bars ? LV_OPA_COVER : LV_OPA_60, 0);
    }
}

static void wifi_row_cb(lv_event_t *e);

static void wifi_status_refresh(void)
{
    if (s_wifi_status == NULL) {
        return;
    }
    char buf[96];
    lv_color_t color = COL_MUTED;
    const char *ssid = wifi_get_ssid();

    switch (wifi_get_state()) {
    case EPW_WIFI_CONNECTED:
        snprintf(buf, sizeof(buf), "已连接 · %s", ssid);
        color = COL_GREEN;
        break;
    case EPW_WIFI_CONNECTING:
        if (ssid[0] != '\0') {
            snprintf(buf, sizeof(buf), "正在连接 %s…", ssid);
        } else {
            snprintf(buf, sizeof(buf), "正在连接…");
        }
        color = COL_AMBER;
        break;
    case EPW_WIFI_FAILED:
        snprintf(buf, sizeof(buf), "连接失败，可重新选择网络");
        color = COL_CORAL;
        break;
    case EPW_WIFI_OFF:
    default:
        if (ssid[0] != '\0') {
            snprintf(buf, sizeof(buf), "未连接 · %s", ssid);
        } else {
            snprintf(buf, sizeof(buf), "未连接");
        }
        color = COL_MUTED;
        break;
    }
    lv_label_set_text(s_wifi_status, buf);
    lv_obj_set_style_text_color(s_wifi_status, color, 0);
}

/* 用最近一次扫描结果重建列表；只在扫描完成后调用 */
static void wifi_populate_list(void)
{
    if (s_wifi_list == NULL) {
        return;
    }
    lv_obj_clean(s_wifi_list);

    s_wifi_ap_count = wifi_scan_copy(s_wifi_aps, EPW_WIFI_MAX_APS);
    bool scan_failed = s_wifi_ap_count == -2;
    if (s_wifi_ap_count < 0) {
        s_wifi_ap_count = 0;
    }

    bool connected = wifi_get_state() == EPW_WIFI_CONNECTED;

    for (int i = 0; i < s_wifi_ap_count; i++) {
        epw_wifi_ap_t *ap = &s_wifi_aps[i];
        lv_obj_t *row = mk_box(s_wifi_list, 4, i * 64, 344, 56,
                               COL_SURF, LV_OPA_COVER, 18);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, COL_LINE, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_50, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(row, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, wifi_row_cb, LV_EVENT_CLICKED, NULL);

        if (ap->secure) {
            draw_lock(row, 24, 28);
        } else {
            mk_circle(row, 24, 28, 7, COL_MUTED, LV_OPA_60);
        }

        lv_obj_t *name = mk_label(row, ap->ssid, &epw_cjk_16, COL_TEXT,
                                  46, 19, 200, LV_TEXT_ALIGN_LEFT);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

        if (connected && strcmp(ap->ssid, wifi_get_ssid()) == 0) {
            mk_circle(row, 262, 28, 7, COL_GREEN, LV_OPA_COVER);
        }
        draw_signal_bars(row, 292, 40, ap->rssi);
        clear_clickable_tree(row);
    }

    if (s_wifi_scan_hint != NULL) {
        if (s_wifi_ap_count == 0) {
            lv_label_set_text(s_wifi_scan_hint, scan_failed ? "扫描失败\n点右上角重试" : "没有扫描到网络\n点右上角刷新");
            lv_obj_remove_flag(s_wifi_scan_hint, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_wifi_scan_hint, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* 网络页 400ms 轮询：状态文字、扫描结果、连接结果 */
static void wifi_poll_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (!s_wifi_open && !s_wifipwd_open) {
        return;
    }

    wifi_status_refresh();

    if (s_wifi_open) {
        if (wifi_scan_busy()) {
            if (s_wifi_scan_hint != NULL) {
                lv_label_set_text(s_wifi_scan_hint, "正在扫描…");
                lv_obj_remove_flag(s_wifi_scan_hint, LV_OBJ_FLAG_HIDDEN);
            }
        } else if (!s_wifi_scan_seen) {
            s_wifi_scan_seen = true;
            wifi_populate_list();
        }
    }

    if (s_wifipwd_connecting) {
        epw_wifi_state_t st = wifi_get_state();
        if (st == EPW_WIFI_CONNECTED) {
            s_wifipwd_connecting = false;
            toast_show("网络已连接");
            wifi_pwd_close_now();
            wifi_close();
        } else if (st == EPW_WIFI_FAILED) {
            s_wifipwd_connecting = false;
            if (s_wifipwd_open) {
                lv_label_set_text(s_wifipwd_btn_label, "重试");
                lv_obj_set_style_bg_color(s_wifipwd_btn, COL_TECH_GREEN, 0);
                lv_label_set_text(s_wifipwd_hint, "连接失败，请检查密码后重试");
                lv_obj_set_style_text_color(s_wifipwd_hint, COL_CORAL, 0);
            } else {
                toast_show("连接失败");
            }
        }
    }
}

static void wifi_open(void)
{
    if (s_wifi == NULL || s_wifi_open) {
        return;
    }
    /* 网络页从应用桌面进入：先把控制中心及其子页、宠物页收干净。
     * 应用桌面作为父级保留，返回时还能回到它。 */
    center_close_now();
    pet_close_now();
    nursery_close_now();
    s_wifi_open = true;
    s_wifi_ap_count = 0;
    s_wifi_scan_seen = false;

    if (s_wifi_list != NULL) {
        lv_obj_clean(s_wifi_list);
    }
    if (s_wifi_scan_hint != NULL) {
        lv_label_set_text(s_wifi_scan_hint, "正在扫描…");
        lv_obj_remove_flag(s_wifi_scan_hint, LV_OBJ_FLAG_HIDDEN);
    }
    wifi_status_refresh();

    lv_anim_delete(s_wifi, NULL);
    lv_obj_set_x(s_wifi, SCREEN_W);
    lv_obj_remove_flag(s_wifi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_wifi);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_wifi);
    lv_anim_set_values(&a, SCREEN_W, 0);
    lv_anim_set_duration(&a, 280);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_start(&a);

    if (wifi_scan_async() != ESP_OK) {
        /* 已在扫描时保持现状 */
    }
    page_anim_lock(280 + 60);
    ESP_LOGI(TAG, "wifi app open");
}

static void wifi_hide_done(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    if (!s_wifi_open) {
        lv_obj_add_flag(s_wifi, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(s_wifi, 0);
    }
}

static void wifi_close(void)
{
    if (s_wifi == NULL || !s_wifi_open) {
        return;
    }
    s_wifi_open = false;
    wifi_pwd_close_now();

    lv_anim_delete(s_wifi, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_wifi);
    lv_anim_set_values(&a, 0, SCREEN_W);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_completed_cb(&a, wifi_hide_done);
    lv_anim_start(&a);
    page_anim_lock(220 + 60);
    ESP_LOGI(TAG, "wifi app close");
}

static void wifi_close_now(void)
{
    if (s_wifi == NULL) {
        return;
    }
    s_wifi_open = false;
    wifi_pwd_close_now();
    lv_anim_delete(s_wifi, NULL);
    lv_obj_add_flag(s_wifi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(s_wifi, 0);
}

static void wifi_pwd_hide_done(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    if (!s_wifipwd_open) {
        lv_obj_add_flag(s_wifipwd, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(s_wifipwd, 0);
    }
}

static void wifi_pwd_open(const epw_wifi_ap_t *ap)
{
    if (s_wifipwd == NULL || ap == NULL || s_wifipwd_open) {
        return;
    }
    strncpy(s_wifipwd_ssid, ap->ssid, sizeof(s_wifipwd_ssid) - 1);
    s_wifipwd_ssid[sizeof(s_wifipwd_ssid) - 1] = '\0';
    s_wifipwd_secure = ap->secure;
    s_wifipwd_connecting = false;
    s_wifipwd_hidden = true;

    if (s_wifipwd_title != NULL) {
        lv_label_set_text(s_wifipwd_title, s_wifipwd_ssid);
    }
    if (s_wifipwd_ta != NULL) {
        lv_textarea_set_text(s_wifipwd_ta, "");
        lv_textarea_set_password_mode(s_wifipwd_ta, true);
        lv_textarea_set_cursor_pos(s_wifipwd_ta, LV_TEXTAREA_CURSOR_LAST);
    }
    if (s_wifipwd_kb != NULL) {
        /* 每次打开都回到小写字母键盘，并重新聚焦密码框 */
        s_wifipwd_kb_mode = KP_LC;
        wifi_pwd_kb_apply();
    }
    if (s_wifipwd_ta != NULL) {
        lv_obj_add_state(s_wifipwd_ta, LV_STATE_FOCUSED);
    }
    if (s_wifipwd_eye_label != NULL) {
        lv_label_set_text(s_wifipwd_eye_label, "显示");
    }
    if (s_wifipwd_btn_label != NULL) {
        lv_label_set_text(s_wifipwd_btn_label, "连接");
    }
    if (s_wifipwd_hint != NULL) {
        lv_label_set_text(s_wifipwd_hint, "请输入 Wi-Fi 密码");
        lv_obj_set_style_text_color(s_wifipwd_hint, COL_MUTED, 0);
    }

    s_wifipwd_open = true;
    lv_anim_delete(s_wifipwd, NULL);
    lv_obj_set_x(s_wifipwd, SCREEN_W);
    lv_obj_remove_flag(s_wifipwd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_wifipwd);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_wifipwd);
    lv_anim_set_values(&a, SCREEN_W, 0);
    lv_anim_set_duration(&a, 260);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_start(&a);
    page_anim_lock(260 + 60);
    ESP_LOGI(TAG, "wifi pwd open: %s", s_wifipwd_ssid);
}

static void wifi_pwd_close(void)
{
    if (s_wifipwd == NULL || !s_wifipwd_open) {
        return;
    }
    s_wifipwd_open = false;
    s_wifipwd_connecting = false;

    lv_anim_delete(s_wifipwd, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_wifipwd);
    lv_anim_set_values(&a, 0, SCREEN_W);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_completed_cb(&a, wifi_pwd_hide_done);
    lv_anim_start(&a);
    page_anim_lock(220 + 60);
    ESP_LOGI(TAG, "wifi pwd close");
}

static void wifi_pwd_close_now(void)
{
    if (s_wifipwd == NULL) {
        return;
    }
    s_wifipwd_open = false;
    s_wifipwd_connecting = false;
    lv_anim_delete(s_wifipwd, NULL);
    lv_obj_add_flag(s_wifipwd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(s_wifipwd, 0);
}

static void wifi_pwd_do_connect(void)
{
    if (s_wifipwd_connecting || s_wifipwd_ta == NULL) {
        return;
    }
    const char *pwd = lv_textarea_get_text(s_wifipwd_ta);
    if (s_wifipwd_secure && (pwd == NULL || pwd[0] == '\0')) {
        lv_label_set_text(s_wifipwd_hint, "请输入 Wi-Fi 密码");
        lv_obj_set_style_text_color(s_wifipwd_hint, COL_CORAL, 0);
        return;
    }

    s_wifipwd_connecting = true;
    lv_label_set_text(s_wifipwd_btn_label, "连接中…");
    lv_label_set_text(s_wifipwd_hint, "正在连接，请稍候…");
    lv_obj_set_style_text_color(s_wifipwd_hint, COL_AMBER, 0);

    esp_err_t err = wifi_connect_to(s_wifipwd_ssid, pwd != NULL ? pwd : "");
    if (err != ESP_OK) {
        s_wifipwd_connecting = false;
        lv_label_set_text(s_wifipwd_btn_label, "重试");
        lv_label_set_text(s_wifipwd_hint, err == ESP_ERR_INVALID_ARG
                                           ? "密码格式错误" : "连接失败，请重试");
        lv_obj_set_style_text_color(s_wifipwd_hint, COL_CORAL, 0);
    }
}

static void wifi_back_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    wifi_close();
}

static void wifi_pwd_back_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    wifi_pwd_close();
}

static void wifi_gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE || page_anim_busy()) {
        return;
    }
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev != NULL && lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) {
        wifi_close();
    }
}

static void wifi_pwd_gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE || page_anim_busy()) {
        return;
    }
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev != NULL && lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) {
        wifi_pwd_close();
    }
}

static void wifi_refresh_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    s_wifi_scan_seen = false;
    if (s_wifi_scan_hint != NULL) {
        lv_label_set_text(s_wifi_scan_hint, "正在扫描…");
        lv_obj_remove_flag(s_wifi_scan_hint, LV_OBJ_FLAG_HIDDEN);
    }
    wifi_scan_async();
}

static void wifi_row_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    lv_obj_t *row = lv_event_get_current_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);
    if (idx < 0 || idx >= s_wifi_ap_count) {
        return;
    }

    if (wifi_scan_busy()) {
        toast_show("正在扫描…");
        return;
    }
    epw_wifi_ap_t *ap = &s_wifi_aps[idx];
    if (!ap->secure) {
        /* 开放网络直接连接，连接期间保留列表展示进度 */
        s_wifipwd_connecting = true;
        wifi_connect_to(ap->ssid, "");
        wifi_status_refresh();
        return;
    }
    wifi_pwd_open(ap);
}

static void wifi_pwd_eye_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    s_wifipwd_hidden = !s_wifipwd_hidden;
    if (s_wifipwd_ta != NULL) {
        lv_textarea_set_password_mode(s_wifipwd_ta, s_wifipwd_hidden);
    }
    if (s_wifipwd_eye_label != NULL) {
        lv_label_set_text(s_wifipwd_eye_label, s_wifipwd_hidden ? "显示" : "隐藏");
    }
}

static void wifi_pwd_connect_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    wifi_pwd_do_connect();
}

/* ---- 密码键盘：4 行 ASCII 自绘键盘 -------------------------------------
 * 不用 lv_keyboard：它的构造函数把 align 设成 BOTTOM_MID，之后 set_pos
 * 只改 x/y 偏移，实际仍贴在屏幕底部（键盘被圆屏裁掉）。这里用
 * buttonmatrix 完全控制尺寸/位置，并只用 0x20-0x7E 字符，字体不会缺字。 */

static const char *const kp_map_lc[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    "Aa", "z", "x", "c", "v", "b", "n", "m", "Del", "\n",
    "1#", "space", ".", "OK", ""
};
static const lv_buttonmatrix_ctrl_t kp_ctrl_lc[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 4, 1, 2
};

static const char *const kp_map_uc[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    "abc", "Z", "X", "C", "V", "B", "N", "M", "Del", "\n",
    "1#", "space", ".", "OK", ""
};
static const lv_buttonmatrix_ctrl_t kp_ctrl_uc[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 4, 1, 2
};

static const char *const kp_map_num[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "-", "_", "=", "+", "/", ":", ";", "(", ")", "@", "\n",
    "#", "$", "%", "&", "*", "\"", "'", "!", "?", "Del", "\n",
    "2#", "abc", "space", ".", "OK", ""
};
static const lv_buttonmatrix_ctrl_t kp_ctrl_num[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 4, 1, 2
};

static const char *const kp_map_symbol[] = {
    "[", "]", "{", "}", "\n",
    "<", ">", "\\", "|", "\n",
    "~", "^", "`", ",", "Del", "\n",
    "abc", "1#", "space", "OK", ""
};
static const lv_buttonmatrix_ctrl_t kp_ctrl_symbol[] = {
    1, 1, 1, 1,
    1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 4, 2
};

static void wifi_pwd_kb_apply(void)
{
    if (s_wifipwd_kb == NULL) {
        return;
    }
    const char *const *map = kp_map_lc;
    const lv_buttonmatrix_ctrl_t *ctrl = kp_ctrl_lc;
    if (s_wifipwd_kb_mode == KP_UC) {
        map = kp_map_uc;
        ctrl = kp_ctrl_uc;
    } else if (s_wifipwd_kb_mode == KP_NUM) {
        map = kp_map_num;
        ctrl = kp_ctrl_num;
    } else if (s_wifipwd_kb_mode == KP_SYMBOL) {
        map = kp_map_symbol;
        ctrl = kp_ctrl_symbol;
    }
    lv_buttonmatrix_set_map(s_wifipwd_kb, map);
    lv_buttonmatrix_set_ctrl_map(s_wifipwd_kb, ctrl);
}

static void wifi_pwd_keypad_cb(lv_event_t *e)
{
    lv_obj_t *btnm = lv_event_get_current_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(btnm);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }
    const char *txt = lv_buttonmatrix_get_button_text(btnm, id);
    if (txt == NULL) {
        return;
    }

    if (strcmp(txt, "Aa") == 0) {
        s_wifipwd_kb_mode = KP_UC;
        wifi_pwd_kb_apply();
        return;
    }
    if (strcmp(txt, "abc") == 0) {
        s_wifipwd_kb_mode = KP_LC;
        wifi_pwd_kb_apply();
        return;
    }
    if (strcmp(txt, "1#") == 0) {
        s_wifipwd_kb_mode = KP_NUM;
        wifi_pwd_kb_apply();
        return;
    }
    if (strcmp(txt, "2#") == 0) {
        s_wifipwd_kb_mode = KP_SYMBOL;
        wifi_pwd_kb_apply();
        return;
    }
    if (strcmp(txt, "Del") == 0) {
        if (s_wifipwd_ta != NULL) {
            lv_textarea_delete_char(s_wifipwd_ta);
        }
        return;
    }
    if (strcmp(txt, "OK") == 0) {
        wifi_pwd_do_connect();
        return;
    }
    if (s_wifipwd_ta == NULL || s_wifipwd_connecting) {
        return;
    }
    lv_textarea_add_text(s_wifipwd_ta, strcmp(txt, "space") == 0 ? " " : txt);
}

/* -------------------------------------------------------------------------- */
/* 表盘构建                                                                    */
/* -------------------------------------------------------------------------- */

/* 递归清掉子控件的 CLICKABLE：装饰图形默认可点，会截走表盘的按压事件 */
static void clear_clickable_tree(lv_obj_t *o)
{
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(o, i);
        lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
        clear_clickable_tree(c);
    }
}

static void build_dial(lv_obj_t *page)
{
    /* 装饰层：外环 / 电量 / 天气 / Wi-Fi / 日期都放这里。
     * 睡眠模式整层隐藏，只留下单独挂在 page 上的时间；
     * 容器本身不可点、不可滚，触摸仍然落到 page 上做下拉。 */
    lv_obj_t *deco = lv_obj_create(page);
    lv_obj_remove_style_all(deco);
    lv_obj_remove_flag(deco, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(deco, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(deco, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(deco, 0, 0);
    s_dial_deco = deco;

    /* 外圈：亮黄 + 橙 + 天空蓝的儿童科技环，常驻。三段之间留 4° 呼吸口，
     * 不做刻度；只画实色弧，避免黑底上出现额外的灰色轮廓。 */
    mk_ring_seg(deco, 2, 118, COL_RING_YELLOW);
    mk_ring_seg(deco, 122, 238, COL_RING_ORANGE);
    mk_ring_seg(deco, 242, 358, COL_RING_SKY);

    /* 底部电量弧：贴着外圈向里收一圈，闪电 + 百分比就是电量语义。
     * 电量主色固定绿色，充电时额外叠一层天蓝辉光做区分。 */
    s_batt_glow = mk_arc_prog(deco, CENTER_X, CENTER_Y, BATT_SIZE, BATT_WIDTH * 2,
                              BATT_FROM_DEG, BATT_TO_DEG, COL_LIME, LV_OPA_20);
    lv_obj_add_flag(s_batt_glow, LV_OBJ_FLAG_HIDDEN);
    s_batt_track = mk_arc_seg(deco, CENTER_X, CENTER_Y, BATT_SIZE, BATT_WIDTH,
                              BATT_FROM_DEG, BATT_TO_DEG, COL_LINE, LV_OPA_60);
    s_batt_arc = mk_arc_prog(deco, CENTER_X, CENTER_Y, BATT_SIZE, BATT_WIDTH,
                             BATT_FROM_DEG, BATT_TO_DEG, COL_GREEN, LV_OPA_COVER);

    static const lv_point_precise_t bolt_pts[] = {
        {6, 0}, {1, 8}, {5, 8}, {2, 16}, {11, 6}, {6, 6}, {6, 0},
    };
    s_batt_bolt = lv_line_create(deco);
    lv_line_set_points(s_batt_bolt, bolt_pts, 7);
    lv_obj_set_pos(s_batt_bolt, 194, 371);
    lv_obj_set_style_line_width(s_batt_bolt, 2, 0);
    lv_obj_set_style_line_color(s_batt_bolt, COL_GREEN, 0);
    lv_obj_set_style_line_rounded(s_batt_bolt, true, 0);

    s_batt_pct = mk_label(deco, "", &epw_cjk_20, COL_GREEN,
                          203, 368, 60, LV_TEXT_ALIGN_CENTER);

    /* Wi-Fi 状态：只保留图标，用颜色表达（青=在线 / 琥珀=连接中 / 珊瑚=断开） */
    draw_wifi(deco, 372, 102, COL_TEAL);

    s_weather_icon = mk_box(deco, 154, 43, 52, 50, COL_BG, LV_OPA_TRANSP, 0);
    s_weather = mk_label(deco, "--°C", &epw_cjk_20, COL_TEXT,
                         216, 51, 110, LV_TEXT_ALIGN_LEFT);
    mk_label(deco, "萧山", &epw_cjk_20, COL_MUTED,
             0, 94, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    ui_update_weather(NAN, EPW_WEATHER_UNKNOWN);

    /* 中央：超大号圆润数字时间，垂直居中并整体下移，不再顶在上沿。
     * 时间不放进装饰层，睡眠模式下要单独保留。 */
    s_time = mk_label(page, "", &epw_time_150, COL_TEXT,
                      0, 176, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_letter_space(s_time, 1, 0);
    lv_obj_set_style_shadow_width(s_time, 0, 0);
    lv_obj_set_style_text_outline_stroke_width(s_time, 0, 0);
    lv_obj_set_style_bg_opa(s_time, LV_OPA_TRANSP, 0);

    /* 中文日期 + 星期：20px，位于时间下方留白里 */
    s_date = mk_label(deco, "等待校时", &epw_cjk_20,
                      lv_color_hex(0xD6E2F0), 0, 300, SCREEN_W, LV_TEXT_ALIGN_CENTER);

    /* 表盘装饰物不接收点击，所有按压都落到 page 上，才能跟手下拉控制中心 */
    clear_clickable_tree(page);

    /* 华为手表式跟手下拉：不依赖手势识别，手指拖多少露出多少。
     * 只接受顶部 1/12 起手，且要求竖向位移大于横向，避免横滑误触发。 */
    lv_obj_add_event_cb(page, home_pull_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(page, home_pull_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(page, home_pull_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(page, home_pull_cb, LV_EVENT_PRESS_LOST, NULL);
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
    /* 拖动中面板可能被快照图顶替/隐藏，锁定按下的对象，保证 PRESSING 不断流 */
    lv_obj_add_flag(s_center, LV_OBJ_FLAG_PRESS_LOCK);
    /* 跟手上滑收起：磁贴上的按压通过 EVENT_BUBBLE 冒泡到这里统一处理 */
    lv_obj_add_event_cb(s_center, center_drag_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_center, center_drag_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_center, center_drag_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_center, center_drag_cb, LV_EVENT_PRESS_LOST, NULL);

    /* 纯图标磁贴：没有标题、没有磁贴名、没有数值文字 */
    const int cols3[3] = {128, 233, 338};
    /* 整体比原来下移 18px：更居中，且仍完整落在圆屏安全区内 */
    const int row_top = 158;
    const int row_bottom = 290;

    /* 状态：青绿圆环 + 白色信息符号 */
    lv_obj_t *t_status = tile_create(s_center, cols3[0], row_top, COL_TEAL);
    lv_obj_add_event_cb(t_status, center_status_cb, LV_EVENT_CLICKED, NULL);
    draw_status_icon(t_status, TILE_D / 2, TILE_D / 2);

    /* 电量：绿色电池轮廓 + 白色闪电 */
    lv_obj_t *t_bat = tile_create(s_center, cols3[1], row_top, COL_GREEN);
    lv_obj_add_event_cb(t_bat, center_battery_cb, LV_EVENT_CLICKED, NULL);
    draw_battery_icon(t_bat, TILE_D / 2, TILE_D / 2);

    /* 手电：整屏纯白 + 最大亮度，双击白屏退出 */
    lv_obj_t *t_torch = tile_create(s_center, cols3[2], row_top, COL_RING_YELLOW);
    lv_obj_add_event_cb(t_torch, center_torch_cb, LV_EVENT_CLICKED, NULL);
    draw_torch(t_torch, TILE_D / 2, TILE_D / 2, COL_RING_YELLOW);

    /* 亮度：点开独立调节页，上下拖动 */
    lv_obj_t *t_bright = tile_create(s_center, cols3[0], row_bottom, COL_AMBER);
    lv_obj_add_event_cb(t_bright, center_bright_cb, LV_EVENT_CLICKED, NULL);
    draw_brightness_icon(t_bright, TILE_D / 2, TILE_D / 2, COL_AMBER);

    /* 音量：统一声波图标，调节儿歌播放音量。 */
    lv_obj_t *t_vol = tile_create(s_center, cols3[1], row_bottom, COL_PINK);
    lv_obj_add_event_cb(t_vol, center_volume_cb, LV_EVENT_CLICKED, NULL);
    draw_speaker(t_vol, TILE_D / 2, TILE_D / 2, COL_CORAL);

    /* 睡眠：点一下进入只显示时间黑白表盘，再点恢复 */
    s_cc_sleep = tile_create(s_center, cols3[2], row_bottom, COL_PURPLE);
    lv_obj_add_event_cb(s_cc_sleep, center_sleep_cb, LV_EVENT_CLICKED, NULL);
    s_cc_sleep_off = mk_box(s_cc_sleep, 0, 0, TILE_D, TILE_D, COL_BG, LV_OPA_TRANSP, 0);
    draw_sleep_icon(s_cc_sleep_off, false);
    s_cc_sleep_on = mk_box(s_cc_sleep, 0, 0, TILE_D, TILE_D, COL_BG, LV_OPA_TRANSP, 0);
    draw_sleep_icon(s_cc_sleep_on, true);
    lv_obj_add_flag(s_cc_sleep_on, LV_OBJ_FLAG_HIDDEN);

    /* 装饰子对象不参与命中，点击和拖动统一由磁贴接收。 */
    for (uint32_t i = 0; i < lv_obj_get_child_count(s_center); i++) {
        clear_clickable_tree(lv_obj_get_child(s_center, i));
    }

    /* 抽屉快照图层：与面板同尺寸，仅拖动/滑入滑出时显示，平时隐藏。
     * 纯位图不接收点击，触摸始终交给真实页面。 */
    s_center_snap_img = lv_image_create(s_screen);
    lv_obj_remove_flag(s_center_snap_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_center_snap_img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_center_snap_img, 0, -SCREEN_H);
    lv_obj_add_flag(s_center_snap_img, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(s_center, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(s_center, -SCREEN_H);
}

/* -------------------------------------------------------------------------- */
/* 通知抽屉构建                                                                */
/* -------------------------------------------------------------------------- */

/* 线性铃铛：44px 画布，与状态页 / 亮度图标同一套线条语言 */
static void draw_bell_icon(lv_obj_t *parent, int cx, int cy, lv_color_t color)
{
    static const lv_point_precise_t bell[] = {
        {13, 31}, {14, 21}, {16, 13}, {19, 8}, {22, 7},
        {25, 8}, {28, 13}, {30, 21}, {31, 31}, {13, 31},
    };
    icon_line(parent, bell, 10, cx, cy, color);
    mk_circle(parent, cx, cy + 14, 5, color, LV_OPA_COVER);
}

static void draw_chat_mini(lv_obj_t *parent, int cx, int cy, lv_color_t color)
{
    lv_obj_t *b = mk_box(parent, cx - 9, cy - 8, 18, 14, color, LV_OPA_TRANSP, 5);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, color, 0);
    mk_box(parent, cx - 6, cy + 5, 5, 3, color, LV_OPA_COVER, 1);
    mk_circle(parent, cx - 4, cy - 1, 2, color, LV_OPA_COVER);
    mk_circle(parent, cx, cy - 1, 2, color, LV_OPA_COVER);
    mk_circle(parent, cx + 4, cy - 1, 2, color, LV_OPA_COVER);
}

static void draw_calendar_mini(lv_obj_t *parent, int cx, int cy, lv_color_t color)
{
    lv_obj_t *b = mk_box(parent, cx - 8, cy - 7, 16, 15, color, LV_OPA_TRANSP, 4);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, color, 0);
    mk_box(parent, cx - 8, cy - 7, 16, 4, color, LV_OPA_COVER, 2);
    mk_circle(parent, cx - 3, cy + 2, 2, color, LV_OPA_COVER);
    mk_circle(parent, cx + 3, cy + 2, 2, color, LV_OPA_COVER);
}

/* 底部上拉通知抽屉：先做空状态 + 分类图标占位，后续在这里挂消息列表。 */
static void build_notice(void)
{
    lv_obj_t *sheet = lv_obj_create(s_screen);
    lv_obj_remove_style_all(sheet);
    lv_obj_remove_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_size(sheet, SCREEN_W, NOTICE_H);
    lv_obj_set_pos(sheet, 0, SCREEN_H);
    lv_obj_set_style_bg_color(sheet, COL_SURF, 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sheet, 40, 0);
    s_notice = sheet;

    lv_obj_add_event_cb(sheet, notice_drag_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(sheet, notice_drag_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(sheet, notice_drag_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(sheet, notice_drag_cb, LV_EVENT_PRESS_LOST, NULL);

    /* 顶部把手 + 标题 */
    mk_box(sheet, CENTER_X - 30, 12, 60, 5, COL_LINE, LV_OPA_COVER, 3);
    mk_label(sheet, "通知", &epw_cjk_20, COL_TEXT, 0, 28, SCREEN_W, LV_TEXT_ALIGN_CENTER);

    /* 分类图标占位：消息 / 日历 / 提醒 */
    const int chip_y = 88;
    const int chip_x[3] = {CENTER_X - 64, CENTER_X, CENTER_X + 64};
    for (int i = 0; i < 3; i++) {
        mk_circle(sheet, chip_x[i], chip_y, 36, COL_SURF_HI, LV_OPA_COVER);
    }
    draw_chat_mini(sheet, chip_x[0], chip_y, COL_TEAL);
    draw_calendar_mini(sheet, chip_x[1], chip_y, COL_AMBER);
    draw_bell_icon(sheet, chip_x[2], chip_y, COL_PURPLE);

    /* 空状态：柔光圆底 + 铃铛 + 文案 */
    lv_obj_t *halo = mk_circle(sheet, CENTER_X, 162, 92, COL_SURF_HI, LV_OPA_COVER);
    lv_obj_set_style_border_width(halo, 1, 0);
    lv_obj_set_style_border_color(halo, COL_LINE, 0);
    draw_bell_icon(sheet, CENTER_X, 158, COL_MUTED);
    mk_label(sheet, "没有通知", &epw_cjk_20, COL_MUTED,
             0, 216, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    mk_label(sheet, "新的提醒会出现在这里", &epw_cjk_16, lv_color_hex(0x5E6A7A),
             0, 248, SCREEN_W, LV_TEXT_ALIGN_CENTER);

    /* 装饰图形不接收点击，拖动统一由抽屉接收 */
    clear_clickable_tree(sheet);
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(sheet, SCREEN_H);
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
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(s_status, tool_page_drag_cb, LV_EVENT_ALL, NULL);

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
        "466 x 466", "CST9217", "--", "16k 单声道", "未连接", "--",
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

    clear_clickable_tree(s_status);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
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
    lv_obj_add_flag(s_slider, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(s_slider, tool_page_drag_cb, LV_EVENT_ALL, NULL);

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

    /* 只保留当前值：不写标题、说明、端点刻度和百分号 */
    s_slider_value = mk_label(s_slider, "80", &lv_font_montserrat_28, COL_TECH_GREEN,
                              0, 52, SCREEN_W, LV_TEXT_ALIGN_CENTER);

    /* 竖向粗轨道：底部最小、顶部最大，必须按在轨道上才能拖动 */
    s_slider_track = mk_box(s_slider, SLIDER_TRACK_X, SLIDER_TRACK_TOP,
                            SLIDER_TRACK_W, SLIDER_TRACK_H,
                            COL_LINE, LV_OPA_60, SLIDER_TRACK_W / 2);
    lv_obj_add_event_cb(s_slider_track, slider_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_slider_track, slider_press_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_slider_track, slider_press_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_slider_track, slider_press_cb, LV_EVENT_PRESS_LOST, NULL);

    s_slider_fill = mk_box(s_slider, SLIDER_TRACK_X, SLIDER_TRACK_TOP,
                           SLIDER_TRACK_W, SLIDER_TRACK_H,
                           COL_TECH_GREEN, LV_OPA_COVER, SLIDER_TRACK_W / 2);
    lv_obj_add_event_cb(s_slider_fill, slider_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_slider_fill, slider_press_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_slider_fill, slider_press_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_slider_fill, slider_press_cb, LV_EVENT_PRESS_LOST, NULL);

    clear_clickable_tree(s_slider);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(s_slider_track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_slider_fill, LV_OBJ_FLAG_CLICKABLE);
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
/* 应用桌面（紧凑九宫格）                                      */
/* -------------------------------------------------------------------------- */

/* 九个位置均为现有功能；快捷工具复用控制中心的页面。 */
enum {
    APP_PET = 0,
    APP_NURSERY,
    APP_STATUS,
    APP_WIFI,
    APP_SETTINGS,
    APP_TIMER,
    APP_BRIGHT,
    APP_VOLUME,
    APP_TORCH,
};

/* Mimi 固定居中，九个功能占满 3×3，不随拖动切换排列。 */
static lv_point_t app_icon_center(int i)
{
    static const lv_point_t positions[APP_COUNT] = {
        {0, 0}, {-APP_GRID_STEP, 0}, {APP_GRID_STEP, 0},
        {-APP_GRID_STEP, -APP_GRID_STEP}, {APP_GRID_STEP, APP_GRID_STEP}, {APP_GRID_STEP, -APP_GRID_STEP},
        {0, -APP_GRID_STEP}, {-APP_GRID_STEP, APP_GRID_STEP}, {0, APP_GRID_STEP},
    };
    return (lv_point_t){CENTER_X + positions[i].x + s_app_pan.x,
                        CENTER_Y + positions[i].y + s_app_pan.y};
}

static int app_icon_size(lv_point_t center)
{
    float dx = center.x - CENTER_X, dy = center.y - CENTER_Y;
    int size = (int)lroundf(APP_ICON_D - sqrtf(dx * dx + dy * dy) / 5.5f);
    return size < APP_ICON_MIN_D ? APP_ICON_MIN_D : size;
}

static uint16_t app_mix_rgb565(uint16_t a, uint16_t b, unsigned weight)
{
    unsigned inverse = 256 - weight;
    unsigned r = (((a >> 11) * inverse + (b >> 11) * weight) >> 8) << 11;
    unsigned g = (((((a >> 5) & 63) * inverse + ((b >> 5) & 63) * weight) >> 8)) << 5;
    unsigned blue = ((a & 31) * inverse + (b & 31) * weight) >> 8;
    return (uint16_t)(r | g | blue);
}

/* 目标缓冲启动时已分配；只在整数尺寸变化时重采样，绘制始终为 1:1。 */
static void app_resize_rgb565(const lv_draw_buf_t *src, lv_draw_buf_t *dst, int size)
{
    uint16_t sample[APP_ICON_D], fraction[APP_ICON_D];
    int width = src->header.w;
    for (int x = 0; x < size; x++) {
        unsigned position = x * (width - 1) * 256u / (size - 1);
        sample[x] = position >> 8;
        fraction[x] = position & 255;
    }
    for (int y = 0; y < size; y++) {
        uint16_t *out = (uint16_t *)(dst->data + y * dst->header.stride);
        if (size == width) {
            memcpy(out, src->data + y * src->header.stride, size * 2);
            continue;
        }
        int row = sample[y], next = row + (row + 1 < width);
        const uint16_t *top = (const uint16_t *)(src->data + row * src->header.stride);
        const uint16_t *bottom = (const uint16_t *)(src->data + next * src->header.stride);
        for (int x = 0; x < size; x++) {
            int col = sample[x], right = col + (col + 1 < width);
            uint16_t a = app_mix_rgb565(top[col], top[right], fraction[x]);
            uint16_t b = app_mix_rgb565(bottom[col], bottom[right], fraction[x]);
            out[x] = app_mix_rgb565(a, b, fraction[y]);
        }
    }
}

static void app_art_resize(int i, int size)
{
    if (!s_app_scaled[i] || s_app_pixel_size[i] == size) return;
    if (!lv_draw_buf_reshape(s_app_scaled[i], LV_COLOR_FORMAT_RGB565, size, size, LV_STRIDE_AUTO)) return;
    app_resize_rgb565(s_app_art[i], s_app_scaled[i], size);
    lv_image_cache_drop(s_app_scaled[i]);
    lv_image_set_src(s_app_icons[i], s_app_scaled[i]);
    s_app_pixel_size[i] = size;
}

static void apps_layout_icons(void)
{
    for (int i = 0; i < APP_COUNT; i++) {
        lv_obj_t *icon = s_app_icons[i];
        if (icon == NULL) continue;
        lv_point_t center = app_icon_center(i);
        int size = app_icon_size((lv_point_t){center.x - s_app_pan.x, center.y - s_app_pan.y});
        if (s_app_art[i]) {
            app_art_resize(i, size);
            lv_obj_set_size(icon, size, size);
            lv_obj_set_pos(icon, center.x - size / 2, center.y - size / 2);
        } else {
            /* 内存不足时保留可用的矢量图标。 */
            int scale = size * 256 / APP_ICON_D;
            lv_obj_set_style_transform_scale_x(icon, scale, 0);
            lv_obj_set_style_transform_scale_y(icon, scale, 0);
            lv_obj_set_pos(icon, center.x - APP_ICON_D / 2, center.y - APP_ICON_D / 2);
        }

    }

}

static void app_open_index(int idx)
{
    /* 页面滑入动画期间不接受第二次点按，避免同时拉起两个页面 */
    if (page_anim_busy()) {
        return;
    }
    switch (idx) {
    case APP_PET:
        ui_pet_set_state(UI_PET_HAPPY, NULL);
        break;
    case APP_NURSERY:
        nursery_open();
        break;
    case APP_STATUS:
        status_open();
        break;
    case APP_WIFI:
        wifi_open();
        break;
    case APP_SETTINGS:
        tools_open(0);
        break;
    case APP_TIMER:
        tools_open(1);
        break;
    case APP_BRIGHT:
        slider_open(SLIDER_BRIGHT);
        break;
    case APP_VOLUME:
        slider_open(SLIDER_VOLUME);
        break;
    case APP_TORCH:
        torch_open();
        break;
    default:
        break;
    }
}

/* 仅保留小范围弹性反馈；松手目标总为零，不能停在任意偏移。 */
static int app_pan_bound(int value, bool elastic)
{
    if (!elastic) return 0;
    return LV_CLAMP(-APP_PAN_OVERSHOOT, value / 4, APP_PAN_OVERSHOOT);
}

static void app_pan_anim_cb(void *var, int32_t value)
{
    LV_UNUSED(var);
    s_app_pan.x = s_app_settle_from.x +
        (s_app_settle_to.x - s_app_settle_from.x) * value / 256;
    s_app_pan.y = s_app_settle_from.y +
        (s_app_settle_to.y - s_app_settle_from.y) * value / 256;
    apps_layout_icons();
}

static void apps_settle_bounds(void)
{
    s_app_settle_from = s_app_pan;
    s_app_settle_to = (lv_point_t){app_pan_bound(s_app_pan.x, false),
                                  app_pan_bound(s_app_pan.y, false)};
    lv_anim_delete(s_apps, app_pan_anim_cb);
    if (s_app_settle_from.x == s_app_settle_to.x &&
        s_app_settle_from.y == s_app_settle_to.y) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_apps);
    lv_anim_set_values(&a, 0, 256);
    lv_anim_set_duration(&a, 180);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, app_pan_anim_cb);
    lv_anim_start(&a);
}

static void apps_open_at(lv_point_t p)
{
    int best = -1;
    float best_distance = 1e9f;
    for (int i = 0; i < APP_COUNT; i++) {
        lv_point_t center = app_icon_center(i);
        int size = app_icon_size((lv_point_t){center.x - s_app_pan.x, center.y - s_app_pan.y});
        float dx = (float)p.x - center.x;
        float dy = (float)p.y - center.y;
        float radius = size * 0.5f;
        float distance = dx * dx + dy * dy;
        if (distance <= radius * radius && distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    if (best >= 0) app_open_index(best);
}

/* 不锁定方向；拖动结束或失去触点均不打开应用。 */
static void apps_touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (code == LV_EVENT_PRESSED) {
        if (page_anim_busy()) return;
        lv_anim_delete(s_apps, app_pan_anim_cb);
        s_app_drag_active = true;
        s_app_drag_moved = false;
        s_app_press = p;
        s_app_pan_start = s_app_pan;
    } else if (code == LV_EVENT_PRESSING) {
        if (!s_app_drag_active) return;
        int dx = p.x - s_app_press.x;
        int dy = p.y - s_app_press.y;
        if (dx * dx + dy * dy > APP_GRID_DRAG_TH * APP_GRID_DRAG_TH)
            s_app_drag_moved = true;
        if (s_app_drag_moved) {
            s_app_pan.x = app_pan_bound(s_app_pan_start.x + dx, true);
            s_app_pan.y = app_pan_bound(s_app_pan_start.y + dy, true);
            apps_layout_icons();
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!s_app_drag_active) return;
        s_app_drag_active = false;
        int dx = p.x - s_app_press.x;
        int dy = p.y - s_app_press.y;
        if (dx * dx + dy * dy > APP_GRID_DRAG_TH * APP_GRID_DRAG_TH)
            s_app_drag_moved = true;
        if (s_app_drag_moved || code == LV_EVENT_PRESS_LOST) {
            apps_settle_bounds();
        } else {
            apps_open_at(p);
        }
    }
}

/* 图标只栅格化一次；拖动缩放单张 RGB565 位图，不变换控件树。 */
static void app_draw_glyph(lv_obj_t *root, int kind, lv_color_t color)
{
    const int c = APP_ICON_D / 2;
    if (kind == APP_PET) {
        static const lv_point_precise_t ears[] = {{4, 20}, {5, 3}, {19, 13}, {29, 13}, {40, 3}, {41, 20}};
        icon_line(root, ears, 6, c, c, COL_TEXT);
        mk_circle(root, c, c + 4, 42, COL_TEXT, LV_OPA_COVER);
        mk_circle(root, c - 9, c + 2, 5, color, LV_OPA_COVER);
        mk_circle(root, c + 9, c + 2, 5, color, LV_OPA_COVER);
        mk_box(root, c - 4, c + 10, 8, 4, color, LV_OPA_COVER, 2);
    } else if (kind == APP_NURSERY) draw_note_icon(root, c, c + 2, COL_TEXT);
    else if (kind == APP_STATUS) {
        icon_ring(root, c, c, 52, COL_TEXT);
        static const lv_point_precise_t pulse[] = {{3,22},{12,22},{17,12},{25,32},{30,22},{41,22}};
        icon_line(root, pulse, 6, c, c, COL_TEXT);
    } else if (kind == APP_WIFI) draw_net_icon(root, c, c - 5, COL_TEXT);
    else if (kind == APP_BRIGHT) draw_brightness_icon(root, c, c, COL_TEXT);
    else if (kind == APP_VOLUME) draw_speaker(root, c, c, COL_TEXT);
    else if (kind == APP_TORCH) draw_torch(root, c, c, COL_TEXT);
    else if (kind == APP_SETTINGS) {
        mk_circle(root, c, c, 40, COL_TEXT, LV_OPA_COVER);
        mk_circle(root, c, c, 18, color, LV_OPA_COVER);
        for (int j = 0; j < 8; j++) {
            float rad = j * (float)M_PI / 4;
            int x = c + (int)lroundf(23 * sinf(rad));
            int y = c - (int)lroundf(23 * cosf(rad));
            lv_obj_t *tooth = mk_box(root, x - 3, y - 6, 6, 12, COL_TEXT, LV_OPA_COVER, 2);
            lv_obj_set_style_transform_pivot_x(tooth, 3, 0);
            lv_obj_set_style_transform_pivot_y(tooth, 6, 0);
            lv_obj_set_style_transform_rotation(tooth, j * 450, 0);
        }
    } else {
        icon_ring(root, c, c + 3, 42, COL_TEXT);
        mk_box(root, c - 8, c - 28, 16, 5, COL_TEXT, LV_OPA_COVER, 2);
        mk_box(root, c - 2, c - 23, 4, 6, COL_TEXT, LV_OPA_COVER, 1);
        static const lv_point_precise_t hand[] = {{22, 11}, {22, 25}, {31, 25}};
        icon_line(root, hand, 3, c, c, COL_TEXT);
    }
}

static void *app_art_alloc(size_t size, lv_color_format_t cf)
{
    LV_UNUSED(cf);
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static lv_draw_buf_t *app_art_snapshot(lv_obj_t *root)
{
    static lv_draw_buf_handlers_t handlers;
    static bool initialized;
    if (!initialized) {
        lv_draw_buf_init_with_default_handlers(&handlers);
        handlers.buf_malloc_cb = app_art_alloc;
        handlers.buf_free_cb = heap_caps_free;
        initialized = true;
    }
    lv_obj_update_layout(root);
    lv_draw_buf_t *buffer = lv_draw_buf_create_ex(&handlers, lv_obj_get_width(root),
        lv_obj_get_height(root), LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if (!buffer) return NULL;
    if (lv_snapshot_take_to_draw_buf(root, LV_COLOR_FORMAT_RGB565, buffer) != LV_RESULT_OK) {
        lv_draw_buf_destroy(buffer);
        return NULL;
    }
    return buffer;
}

static lv_obj_t *app_icon_create(lv_obj_t *parent, int kind)
{
    static const uint32_t colors[APP_COUNT] = {
        0xF45B92, 0x00BEA4, 0x318DF5, 0x6659ED, 0x708398, 0xFA9037,
        0xF5BD36, 0xBA55EA, 0x37C594,
    };
    lv_obj_t *root = mk_box(parent, 0, 0, APP_ICON_D, APP_ICON_D,
                            COL_BG, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_pivot_x(root, APP_ICON_D / 2, 0);
    lv_obj_set_style_transform_pivot_y(root, APP_ICON_D / 2, 0);
    lv_color_t color = lv_color_hex(colors[kind]);
    lv_obj_t *disc = mk_circle(root, APP_ICON_D / 2, APP_ICON_D / 2, APP_ICON_D - 4, color, LV_OPA_COVER);
    lv_obj_set_style_border_width(disc, 1, 0);
    lv_obj_set_style_border_color(disc, COL_TEXT, 0);
    lv_obj_set_style_border_opa(disc, LV_OPA_20, 0);
    lv_obj_t *glyph = mk_box(root, 0, 0, APP_ICON_D, APP_ICON_D, COL_BG, LV_OPA_TRANSP, 0);
    lv_obj_set_style_transform_pivot_x(glyph, APP_ICON_D / 2, 0);
    lv_obj_set_style_transform_pivot_y(glyph, APP_ICON_D / 2, 0);
    lv_obj_set_style_transform_scale_x(glyph, 300, 0);
    lv_obj_set_style_transform_scale_y(glyph, 300, 0);
    app_draw_glyph(glyph, kind, color);
    clear_clickable_tree(root);
    s_app_art[kind] = app_art_snapshot(root);
    if (!s_app_art[kind]) return root;
    s_app_scaled[kind] = app_art_snapshot(root);
    s_app_pixel_size[kind] = APP_ICON_D;
    lv_obj_delete(root);
    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(image, s_app_scaled[kind] ? s_app_scaled[kind] : s_app_art[kind]);
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_STRETCH);
    return image;
}

static void build_apps(void)
{
    s_apps = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_apps);
    lv_obj_remove_flag(s_apps, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_apps, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_apps, 0, 0);
    lv_obj_set_style_bg_color(s_apps, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_apps, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_apps, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_apps, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_remove_flag(s_apps, LV_OBJ_FLAG_GESTURE_BUBBLE);
    /* 拖动仅作轻微弹性反馈，通过实体按键退出。 */
    lv_obj_add_event_cb(s_apps, apps_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_apps, apps_touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_apps, apps_touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_apps, apps_touch_cb, LV_EVENT_PRESS_LOST, NULL);

    for (int i = 0; i < APP_COUNT; i++) {
        s_app_icons[i] = app_icon_create(s_apps, i);

    }
    size_t bytes = 0;
    int cached = 0;
    for (int i = 0; i < APP_COUNT; i++) {
        if (s_app_art[i]) {bytes += s_app_art[i]->data_size; cached++;}
        if (s_app_scaled[i]) bytes += s_app_scaled[i]->data_size;
    }
    ESP_LOGI(TAG, "app icon cache: %d masters, %lu bytes", cached, (unsigned long)bytes);
    clear_clickable_tree(s_apps);
    lv_obj_add_flag(s_apps, LV_OBJ_FLAG_HIDDEN);
    s_app_pan = (lv_point_t){0, 0};
    apps_layout_icons();
}

/* 系统设置和计时器复用同一覆盖页，返回后保留完整应用九宫格。 */
static void tools_refresh(void)
{
    static uint32_t previous_timeout = UINT32_MAX;
    uint32_t timeout = watch_idle_timeout_ms();
    if (timeout != previous_timeout) {
        for (int i = 0; i < 6; i++) {
            lv_obj_set_style_bg_color(s_idle_options[i], timeout == s_idle_choices[i] ? COL_TEAL : COL_SURF_HI, 0);
        }
        previous_timeout = timeout;
    }
    watch_timer_state_t state = watch_timer_state();
    bool active = state != WATCH_TIMER_IDLE;
    if (active) {
        lv_obj_add_flag(s_timer_select, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_timer_custom, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_timer_run, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_timer_run, LV_OBJ_FLAG_HIDDEN);
        if (lv_obj_has_flag(s_timer_custom, LV_OBJ_FLAG_HIDDEN))
            lv_obj_remove_flag(s_timer_select, LV_OBJ_FLAG_HIDDEN);
    }
    uint32_t seconds = watch_timer_remaining();
    static uint32_t previous_seconds = UINT32_MAX;
    static int previous_state = -1;
    if (seconds != previous_seconds || (int)state != previous_state) {
        if (seconds >= 3600)
            lv_label_set_text_fmt(s_timer_value, "%02lu:%02lu:%02lu",
                                  (unsigned long)(seconds / 3600),
                                  (unsigned long)(seconds / 60 % 60), (unsigned long)(seconds % 60));
        else
            lv_label_set_text_fmt(s_timer_value, "%02lu:%02lu", (unsigned long)(seconds / 60),
                                  (unsigned long)(seconds % 60));
        lv_label_set_text(s_timer_hint, state == WATCH_TIMER_FINISHED ? "时间到" :
                         state == WATCH_TIMER_PAUSED ? "已暂停" : "倒计时");
        lv_label_set_text(s_timer_action, state == WATCH_TIMER_RUNNING ? "暂停" :
                         state == WATCH_TIMER_PAUSED ? "继续" : "再来一次");
        lv_label_set_text(s_timer_reset_label, state == WATCH_TIMER_FINISHED ? "完成" : "取消");
        lv_color_t color = state == WATCH_TIMER_FINISHED ? COL_AMBER : COL_TEAL;
        lv_obj_set_style_text_color(s_timer_hint, color, 0);
        lv_obj_set_style_arc_color(s_timer_arc, color, LV_PART_INDICATOR);
        lv_arc_set_angles(s_timer_arc, 0, (uint32_t)((uint64_t)seconds * 360 / s_timer_total));
        previous_seconds = seconds;
        previous_state = state;
    }
}

static void tools_close(void)
{
    if (!s_tools_open) return;
    if (watch_timer_state() == WATCH_TIMER_FINISHED) watch_timer_reset();
    lv_obj_add_flag(s_timer_custom, LV_OBJ_FLAG_HIDDEN);
    s_tools_open = false;
    lv_obj_add_flag(s_tools, LV_OBJ_FLAG_HIDDEN);
}

static void tools_open(int kind)
{
    center_close_now();
    status_close_now();
    wifi_close_now();
    slider_close_now();
    torch_close_now();
    pet_close_now();
    nursery_close_now();
    s_tools_kind = kind;
    s_tools_open = true;
    lv_obj_add_flag(kind ? s_settings_page : s_timer_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(kind ? s_timer_page : s_settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_tools, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_tools);
    tools_refresh();
}

static void tools_back_cb(lv_event_t *e)
{
    if (tap_ignore(e)) return;
    if (s_tools_kind == 1 && !lv_obj_has_flag(s_timer_custom, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(s_timer_custom, LV_OBJ_FLAG_HIDDEN);
        tools_refresh();
    } else tools_close();
}

static void idle_choice_cb(lv_event_t *e)
{
    if (tap_ignore(e)) return;
    unsigned i = (uintptr_t)lv_event_get_user_data(e);
    if (i >= 6) return;
    esp_err_t err = watch_set_idle_timeout_ms(s_idle_choices[i]);
    toast_show(err == ESP_OK ? "已保存" : "保存失败");
    ui_activity();
    tools_refresh();
}

static void timer_choice_cb(lv_event_t *e)
{
    if (tap_ignore(e)) return;
    unsigned i = (uintptr_t)lv_event_get_user_data(e);
    if (i < 6 && watch_timer_select(s_timer_choices[i])) {
        s_timer_total = s_timer_choices[i];
        watch_timer_start();
        tools_refresh();
    }
}

static void timer_custom_cb(lv_event_t *e)
{
    if (tap_ignore(e) || watch_timer_state() != WATCH_TIMER_IDLE) return;
    lv_obj_add_flag(s_timer_select, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_timer_custom, LV_OBJ_FLAG_HIDDEN);
}

static void timer_custom_start_cb(lv_event_t *e)
{
    if (tap_ignore(e)) return;
    uint32_t seconds = lv_roller_get_selected(s_timer_rollers[0]) * 3600u +
                       lv_roller_get_selected(s_timer_rollers[1]) * 60u +
                       lv_roller_get_selected(s_timer_rollers[2]);
    if (!seconds) {
        toast_show("请选择时长");
        return;
    }
    if (watch_timer_select(seconds)) {
        s_timer_total = seconds;
        watch_timer_start();
        tools_refresh();
    }
}

static void timer_action_cb(lv_event_t *e)
{
    if (tap_ignore(e)) return;
    if (watch_timer_state() == WATCH_TIMER_RUNNING) watch_timer_pause();
    else watch_timer_start();
    tools_refresh();
}

static void timer_reset_cb(lv_event_t *e)
{
    if (tap_ignore(e)) return;
    watch_timer_reset();
    tools_refresh();
}

static void tools_tick_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (watch_system_tick()) {
        ui_activity();
        lv_indev_t *input = bsp_display_get_input_dev();
        if (input) lv_indev_wait_release(input);
        s_app_drag_active = false;
        tools_open(1);
    }
    if (s_tools_open && s_tools_kind == 1) tools_refresh();
}

static lv_obj_t *tools_button(lv_obj_t *parent, int x, int y, int width,
                              const char *text, lv_event_cb_t cb, uintptr_t data)
{
    lv_obj_t *button = mk_box(parent, x, y, width, 46, COL_SURF_HI, LV_OPA_COVER, 18);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *label = mk_label(button, text, &epw_cjk_20, COL_TEXT, 0, 11, width, LV_TEXT_ALIGN_CENTER);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, (void *)data);
    lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
    return label;
}

static void build_tools(void)
{
    s_tools = mk_box(s_screen, 0, 0, SCREEN_W, SCREEN_H, COL_BG, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_tools, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_tools, LV_OBJ_FLAG_GESTURE_BUBBLE);
    s_settings_page = mk_box(s_tools, 0, 0, SCREEN_W, SCREEN_H, COL_BG, LV_OPA_COVER, 0);
    s_timer_page = mk_box(s_tools, 0, 0, SCREEN_W, SCREEN_H, COL_BG, LV_OPA_COVER, 0);
    mk_label(s_settings_page, "系统设置", &epw_cjk_20, COL_TEXT, 0, 52, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    mk_label(s_settings_page, "熄屏时间", &epw_cjk_20, COL_MUTED, 0, 110, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    const char *idle_labels[] = {"15 秒", "30 秒", "1 分钟", "2 分钟", "5 分钟", "常亮"};
    for (int i = 0; i < 6; i++) {
        lv_obj_t *label = tools_button(s_settings_page, 121 + (i % 2) * 116, 153 + (i / 2) * 60,
                                       108, idle_labels[i], idle_choice_cb, i);
        s_idle_options[i] = lv_obj_get_parent(label);
    }
    mk_label(s_settings_page, "到点提醒：亮屏", &epw_cjk_20, COL_MUTED, 0, 355, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    mk_label(s_timer_page, "计时器", &epw_cjk_20, COL_TEXT, 0, 52, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    s_timer_select = mk_box(s_timer_page, 0, 96, SCREEN_W, 320, COL_BG, LV_OPA_COVER, 0);
    const char *timer_labels[] = {"1 分钟", "3 分钟", "5 分钟", "10 分钟", "30 分钟", "1 小时"};
    for (int i = 0; i < 6; i++) {
        lv_obj_t *label = tools_button(s_timer_select, 89 + (i % 2) * 152, 12 + (i / 2) * 76,
                                       136, timer_labels[i], timer_choice_cb, i);
        lv_obj_t *button = lv_obj_get_parent(label);
        lv_obj_set_height(button, 64);
        lv_obj_set_y(label, 21);
    }
    tools_button(s_timer_select, 129, 250, 208, "自定义", timer_custom_cb, 0);

    s_timer_custom = mk_box(s_timer_page, 0, 96, SCREEN_W, 320, COL_BG, LV_OPA_COVER, 0);
    const char *units[] = {"时", "分", "秒"};
    char options[180];
    for (int i = 0; i < 3; i++) {
        mk_label(s_timer_custom, units[i], &epw_cjk_20, COL_MUTED,
                 95 + i * 100, 4, 76, LV_TEXT_ALIGN_CENTER);
        int length = 0;
        int count = i == 0 ? 24 : 60;
        for (int n = 0; n < count; n++)
            length += snprintf(options + length, sizeof(options) - length,
                               n == count - 1 ? "%02d" : "%02d\n", n);
        lv_obj_t *roller = lv_roller_create(s_timer_custom);
        s_timer_rollers[i] = roller;
        lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
        lv_obj_set_style_text_font(roller, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(roller, COL_MUTED, 0);
        lv_obj_set_style_bg_color(roller, COL_BG, 0);
        lv_obj_set_style_border_width(roller, 0, 0);
        lv_obj_set_style_bg_color(roller, COL_SURF_HI, LV_PART_SELECTED);
        lv_obj_set_style_text_color(roller, COL_TEAL, LV_PART_SELECTED);
        lv_obj_set_style_text_font(roller, &lv_font_montserrat_28, LV_PART_SELECTED);
        lv_roller_set_visible_row_count(roller, 3);
        lv_obj_set_pos(roller, 95 + i * 100, 45);
        lv_obj_set_width(roller, 76);
        lv_roller_set_selected(roller, i == 1 ? 1 : 0, LV_ANIM_OFF);
    }
    tools_button(s_timer_custom, 153, 250, 160, "开始", timer_custom_start_cb, 0);
    lv_obj_add_flag(s_timer_custom, LV_OBJ_FLAG_HIDDEN);

    s_timer_run = mk_box(s_timer_page, 0, 92, SCREEN_W, 324, COL_BG, LV_OPA_COVER, 0);
    mk_arc_seg(s_timer_run, 233, 138, 276, 9, 0, 360, COL_SURF_HI, LV_OPA_COVER);
    s_timer_arc = mk_arc_prog(s_timer_run, 233, 138, 276, 9, 0, 360, COL_TEAL, LV_OPA_COVER);
    s_timer_value = mk_label(s_timer_run, "01:00", &lv_font_montserrat_48, COL_TEXT,
                              0, 94, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    s_timer_hint = mk_label(s_timer_run, "倒计时", &epw_cjk_20, COL_TEAL,
                             0, 160, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    s_timer_reset_label = tools_button(s_timer_run, 109, 276, 116, "取消", timer_reset_cb, 0);
    s_timer_action = tools_button(s_timer_run, 241, 276, 116, "暂停", timer_action_cb, 0);
    lv_obj_set_style_bg_color(lv_obj_get_parent(s_timer_action), COL_TEAL, 0);
    lv_obj_set_style_text_color(s_timer_action, COL_BG, 0);
    lv_obj_add_flag(s_timer_run, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *back = mk_circle(s_tools, 100, 72, 40, COL_SURF_HI, LV_OPA_COVER);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, tools_back_cb, LV_EVENT_CLICKED, NULL);
    mk_label(back, "<", &epw_cjk_20, COL_TEXT, 0, 6, 40, LV_TEXT_ALIGN_CENTER);
    clear_clickable_tree(back);
    lv_obj_add_flag(s_tools, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(tools_tick_cb, 100, NULL);
}

/* -------------------------------------------------------------------------- */
/* 网络页构建                                                                  */
/* -------------------------------------------------------------------------- */

static void build_wifi(void)
{
    s_wifi = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_wifi);
    lv_obj_remove_flag(s_wifi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_wifi, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_wifi, 0, 0);
    lv_obj_set_style_bg_color(s_wifi, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_wifi, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_wifi, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_wifi, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wifi, wifi_gesture_cb, LV_EVENT_GESTURE, NULL);

    mk_label(s_wifi, "网络", &epw_cjk_20, COL_TEXT,
             0, 38, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    s_wifi_status = mk_label(s_wifi, "未连接", &epw_cjk_16, COL_MUTED,
                             0, 76, SCREEN_W, LV_TEXT_ALIGN_CENTER);

    /* 返回圆钮（主要关闭方式是从左往右滑） */
    lv_obj_t *back = mk_circle(s_wifi, 54, 60, 38, COL_SURF, LV_OPA_COVER);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_color(back, COL_LINE, 0);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, wifi_back_cb, LV_EVENT_CLICKED, NULL);
    static const lv_point_precise_t chev[] = {{21, 8}, {11, 19}, {21, 30}};
    lv_obj_t *l = lv_line_create(back);
    lv_line_set_points(l, chev, 3);
    lv_obj_set_style_line_width(l, 3, 0);
    lv_obj_set_style_line_color(l, COL_MUTED, 0);
    lv_obj_set_style_line_rounded(l, true, 0);

    /* 刷新圆钮 */
    s_wifi_refresh = mk_circle(s_wifi, 412, 60, 38, COL_SURF, LV_OPA_COVER);
    lv_obj_set_style_border_width(s_wifi_refresh, 1, 0);
    lv_obj_set_style_border_color(s_wifi_refresh, COL_LINE, 0);
    lv_obj_add_flag(s_wifi_refresh, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wifi_refresh, wifi_refresh_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ra = lv_arc_create(s_wifi_refresh);
    lv_obj_remove_flag(ra, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ra, 22, 22);
    lv_obj_center(ra);
    lv_obj_remove_style(ra, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(ra, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ra, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ra, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ra, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ra, COL_BLUE, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(ra, true, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(ra, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_arc_set_rotation(ra, 270);
    lv_arc_set_bg_angles(ra, 40, 320);
    lv_arc_set_angles(ra, 0, 0);

    static const lv_point_precise_t arrow[] = {{0, 0}, {7, 1}, {2, 7}, {0, 0}};
    lv_obj_t *al = lv_line_create(s_wifi_refresh);
    lv_line_set_points(al, arrow, 4);
    lv_obj_set_pos(al, 23, 12);
    lv_obj_set_style_line_width(al, 2, 0);
    lv_obj_set_style_line_color(al, COL_BLUE, 0);
    lv_obj_set_style_line_rounded(al, true, 0);

    /* 热点列表：整列可上下拖动 */
    s_wifi_list = lv_obj_create(s_wifi);
    lv_obj_set_pos(s_wifi_list, 57, 112);
    lv_obj_set_size(s_wifi_list, 352, 312);
    lv_obj_set_style_bg_opa(s_wifi_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_wifi_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_wifi_list, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_wifi_list, 0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_wifi_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_wifi_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_y(s_wifi_list, LV_SCROLL_SNAP_NONE);

    s_wifi_scan_hint = mk_label(s_wifi, "正在扫描…", &epw_cjk_16, COL_MUTED,
                                0, 250, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(s_wifi_scan_hint, LV_LABEL_LONG_WRAP);

    s_wifi_timer = lv_timer_create(wifi_poll_cb, 400, NULL);
    lv_obj_add_flag(s_wifi, LV_OBJ_FLAG_HIDDEN);
}

static void build_wifipwd(void)
{
    s_wifipwd = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_wifipwd);
    lv_obj_remove_flag(s_wifipwd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_wifipwd, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_wifipwd, 0, 0);
    lv_obj_set_style_bg_color(s_wifipwd, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_wifipwd, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_wifipwd, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_wifipwd, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wifipwd, wifi_pwd_gesture_cb, LV_EVENT_GESTURE, NULL);

    /* 顶栏：返回 + SSID + 连接，整体左右对称 */
    lv_obj_t *back = mk_circle(s_wifipwd, 42, 54, 34, COL_SURF, LV_OPA_COVER);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_color(back, COL_LINE, 0);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, wifi_pwd_back_cb, LV_EVENT_CLICKED, NULL);
    static const lv_point_precise_t chev[] = {{19, 7}, {10, 17}, {19, 27}};
    lv_obj_t *l = lv_line_create(back);
    lv_line_set_points(l, chev, 3);
    lv_obj_set_style_line_width(l, 3, 0);
    lv_obj_set_style_line_color(l, COL_MUTED, 0);
    lv_obj_set_style_line_rounded(l, true, 0);

    s_wifipwd_title = mk_label(s_wifipwd, "", &epw_cjk_16, COL_TEXT,
                               78, 40, 236, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(s_wifipwd_title, LV_LABEL_LONG_DOT);

    /* 连接按钮放在顶栏右侧，密码页不再额外占一行 */
    s_wifipwd_btn = mk_box(s_wifipwd, 346, 38, 84, 34,
                           COL_TECH_GREEN, LV_OPA_COVER, 17);
    lv_obj_add_flag(s_wifipwd_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wifipwd_btn, wifi_pwd_connect_cb, LV_EVENT_CLICKED, NULL);
    s_wifipwd_btn_label = mk_label(s_wifipwd_btn, "连接", &epw_cjk_16, COL_INK,
                                   0, 6, 84, LV_TEXT_ALIGN_CENTER);

    s_wifipwd_hint = mk_label(s_wifipwd, "请输入 Wi-Fi 密码", &epw_cjk_16, COL_MUTED,
                              0, 74, SCREEN_W, LV_TEXT_ALIGN_CENTER);

    /* 密码框 + 眼睛按钮：48+300 与 356+62 以屏幕中心对称。
     * 高度 60px，文字用 pad_top 垂直居中（CJK16 行高 20） */
    s_wifipwd_ta = lv_textarea_create(s_wifipwd);
    lv_obj_set_pos(s_wifipwd_ta, 48, 102);
    lv_obj_set_size(s_wifipwd_ta, 300, 60);
    lv_textarea_set_one_line(s_wifipwd_ta, true);
    lv_textarea_set_password_mode(s_wifipwd_ta, true);
    lv_textarea_set_password_bullet(s_wifipwd_ta, "*");
    lv_textarea_set_placeholder_text(s_wifipwd_ta, "输入密码");
    lv_textarea_set_max_length(s_wifipwd_ta, 64);
    lv_textarea_set_cursor_pos(s_wifipwd_ta, LV_TEXTAREA_CURSOR_LAST);
    lv_textarea_set_cursor_click_pos(s_wifipwd_ta, true);
    lv_obj_add_flag(s_wifipwd_ta, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_wifipwd_ta, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_bg_color(s_wifipwd_ta, COL_SURF, 0);
    lv_obj_set_style_bg_opa(s_wifipwd_ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_wifipwd_ta, 1, 0);
    lv_obj_set_style_border_color(s_wifipwd_ta, COL_LINE, 0);
    lv_obj_set_style_radius(s_wifipwd_ta, 30, 0);
    lv_obj_set_style_text_color(s_wifipwd_ta, COL_TEXT, 0);
    lv_obj_set_style_text_align(s_wifipwd_ta, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(s_wifipwd_ta, &epw_cjk_16, 0);
    lv_obj_set_style_pad_left(s_wifipwd_ta, 18, 0);
    lv_obj_set_style_pad_right(s_wifipwd_ta, 12, 0);
    lv_obj_set_style_pad_top(s_wifipwd_ta, 20, 0);
    lv_obj_set_style_pad_bottom(s_wifipwd_ta, 0, 0);
    /* 光标用绿色竖线，输入时看得见 */
    lv_obj_set_style_border_color(s_wifipwd_ta, COL_TECH_GREEN, LV_PART_CURSOR);
    lv_obj_set_style_border_width(s_wifipwd_ta, 2, LV_PART_CURSOR);
    lv_obj_set_style_border_side(s_wifipwd_ta, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(s_wifipwd_ta, LV_OPA_TRANSP, LV_PART_CURSOR);

    lv_obj_t *eye = mk_box(s_wifipwd, 356, 102, 62, 60,
                           COL_SURF, LV_OPA_COVER, 30);
    lv_obj_set_style_border_width(eye, 1, 0);
    lv_obj_set_style_border_color(eye, COL_LINE, 0);
    lv_obj_add_flag(eye, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(eye, wifi_pwd_eye_cb, LV_EVENT_CLICKED, NULL);
    s_wifipwd_eye_label = mk_label(eye, "显示", &epw_cjk_16, COL_MUTED,
                                   0, 20, 62, LV_TEXT_ALIGN_CENTER);

    /* 自绘密码键盘（buttonmatrix）：
     * 466 圆屏在 y=366 处可见宽度约 382px；键盘 360×200 放在 (53,166)，
     * 上沿和内切圆相差 70px、下沿相差 10px，四角都不会被圆形边框裁掉。
     * 4 行按键约 31×45px，密码输入够大、好点。 */
    lv_obj_t *kb = lv_buttonmatrix_create(s_wifipwd);
    lv_obj_set_size(kb, 360, 200);
    lv_obj_set_pos(kb, 53, 166);
    lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(kb, 6, 0);
    lv_obj_set_style_pad_column(kb, 5, 0);
    lv_obj_set_style_bg_color(kb, COL_SURF, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, COL_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, &epw_cjk_16, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 10, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, COL_SURF_HI, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(kb, wifi_pwd_keypad_cb, LV_EVENT_VALUE_CHANGED, NULL);
    s_wifipwd_kb = kb;
    s_wifipwd_kb_mode = KP_LC;
    wifi_pwd_kb_apply();

    lv_obj_add_flag(s_wifipwd, LV_OBJ_FLAG_HIDDEN);
}

/* -------------------------------------------------------------------------- */
/* 宠物页（Mimi Cat · 3D 正脸猫）                                                   */
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
        return "喵～ 你好呀！我是 Mimi。";
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

        if (cp == '\n' || cp == '\r' || cp == '\t') {
            out[o++] = ' ';
            p += n;
            continue;
        }
        lv_font_glyph_dsc_t dsc;
        bool supported = lv_font_get_glyph_dsc(&epw_cjk_16, &dsc, cp, 0)
                         && !dsc.is_placeholder;
        if (supported) {
            memcpy(out + o, p, (size_t)n);
            o += (size_t)n;
        }
        p += n;
    }
    out[o] = '\0';
    return out;
}

static void pet_text_set(const char *text)
{
    if (strcmp(s_pet_text, text) == 0) return;
    /* 连续识别或回复追加文字时，保留已显示的进度。 */
    if (strncmp(text, s_pet_text, strlen(s_pet_text)) != 0) {
        s_pet_text_start = s_pet_text_end = 0;
        lv_label_set_text(s_pet_bubble_label, "");
    }
    snprintf(s_pet_text, sizeof(s_pet_text), "%s", text);
}

static void pet_text_step(void)
{
    if (s_pet_text[s_pet_text_end] == '\0') return;
    do { s_pet_text_end++; }
    while ((s_pet_text[s_pet_text_end] & 0xC0) == 0x80);

    char next = s_pet_text[s_pet_text_end];
    s_pet_text[s_pet_text_end] = '\0';
    lv_point_t size;
    int width = lv_obj_get_content_width(s_pet_bubble_label);
    while (s_pet_text_start < s_pet_text_end) {
        lv_text_get_size(&size, s_pet_text + s_pet_text_start, &epw_cjk_16,
                         0, 0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
        if (size.x <= width) break;
        do { s_pet_text_start++; }
        while ((s_pet_text[s_pet_text_start] & 0xC0) == 0x80);
    }
    lv_label_set_text(s_pet_bubble_label, s_pet_text + s_pet_text_start);
    s_pet_text[s_pet_text_end] = next;
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
        line = "喵～ 你好呀！我是 Mimi。";
    }
    pet_text_set(line);
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

    pet_text_step();
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
        int y = 212 + (int)lroundf((float)r * sinf(ang));
        lv_obj_set_pos(sp, x - 5, y - 5);
        lv_obj_set_style_bg_color(sp,
                                  s_pet_state == UI_PET_LOVE ? COL_PINK : COL_AMBER, 0);
    }
}

static void pet_touch_cb(lv_event_t *e)
{
    if (voice_client_is_active()) return;
    /* 长按触发后 LVGL 还会补发 CLICKED，这里吞掉，避免“想”立刻变“摸头” */
    if (s_pet_long_pressed) {
        s_pet_long_pressed = false;
        return;
    }
    if (tap_ignore(e)) {
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
    if (voice_client_is_active()) return;
    if (tap_ignore(e)) {
        return;
    }
    s_pet_long_pressed = true;
    pet_apply(UI_PET_THINKING, "让我想一想…");
}



/* 左滑请求真实语音；没有服务端配置时只给出提示。 */
static void pet_begin_chat(void)
{
    if (voice_client_is_active()) {
        return;
    }
    if (wifi_get_state() != EPW_WIFI_CONNECTED) {
        pet_apply(UI_PET_IDLE, "网络未连接，请先连接 Wi-Fi。");
        return;
    }
    if (!voice_client_set_active(true)) {
        pet_apply(UI_PET_IDLE, "语音暂不可用，请检查配置或重启。");
        return;
    }
    pet_apply(UI_PET_THINKING, "连接语音…");
}

/* 空白区与猫咪本体都支持左滑聊天，右滑退出。 */
static void pet_page_gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
        return;
    }
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev != NULL && lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) {
        pet_close();
    } else if (indev != NULL && lv_indev_get_gesture_dir(indev) == LV_DIR_LEFT) {
        pet_begin_chat();
    }
}

/* 宠物区手势：右滑退出，左滑聊天，上滑兴奋，下滑困了。
 * 右滑优先返回，避免退出动作顺带触发摸头/聊天。 */
static void pet_area_gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
        return;
    }
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        return;
    }

    switch (lv_indev_get_gesture_dir(indev)) {
    case LV_DIR_RIGHT:
        pet_close();
        break;
    case LV_DIR_LEFT:
        pet_begin_chat();
        break;
    case LV_DIR_TOP:
        pet_apply(UI_PET_EXCITED, "哇！我飞起来啦！");
        break;
    case LV_DIR_BOTTOM:
        voice_client_set_active(false);
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
    if (s_pet == NULL || !s_pet_open || voice_client_is_active() || s_screen_off) {
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
        UI_PET_IDLE, UI_PET_HAPPY, UI_PET_IDLE, UI_PET_HAPPY,
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
    /* 空白区同样支持左滑聊天与右滑退出。 */
    lv_obj_add_event_cb(s_pet, pet_page_gesture_cb, LV_EVENT_GESTURE, NULL);

    mk_label(s_pet, "Mimi Cat", &epw_cjk_20, COL_TEXT,
             0, 52, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    s_pet_mood = mk_label(s_pet, "发呆", &epw_cjk_16, COL_TEAL,
                          0, 82, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    mk_label(s_pet, "左滑聊天 · 右滑退出", &epw_cjk_16, COL_MUTED,
             0, 418, SCREEN_W, LV_TEXT_ALIGN_CENTER);

    /* 宠物背后的柔光：两层同心圆纯色填充。
     * 不用 shadow（SW 模糊每帧重算，是宠物页卡顿的主因）。 */
    s_pet_halo = mk_circle(s_pet, CENTER_X, 212, 300, COL_TEAL, LV_OPA_10);
    s_pet_glow = mk_circle(s_pet, CENTER_X, 212, 246, COL_TEAL, LV_OPA_20);

    /* 3D 正脸猫：素材本身就是 208px，1:1 直绘（scale=256，无运行时变换） */
    s_pet_img = lv_image_create(s_pet);
    lv_image_set_src(s_pet_img, &pip_face_idle);
    lv_obj_set_pos(s_pet_img, PET_IMG_X, PET_IMG_Y);
    lv_obj_remove_flag(s_pet_img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_pet_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_pet_img, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_pet_img, pet_area_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s_pet_img, pet_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_pet_img, pet_touch_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_pet_img, pet_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);

    /* 兴奋 / 喜欢时绕着头顶转的小光点 */
    for (int i = 0; i < 8; i++) {
        s_pet_spark[i] = mk_circle(s_pet, 0, 0, 10, COL_AMBER, LV_OPA_COVER);
        lv_obj_add_flag(s_pet_spark[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* 对话气泡（去掉旋转小尾巴，避免 transform layer 合成） */
    s_pet_bubble = mk_box(s_pet, 83, PET_BUBBLE_Y, 300, 48,
                          COL_SURF_HI, LV_OPA_COVER, 26);
    lv_obj_set_style_border_width(s_pet_bubble, 1, 0);
    lv_obj_set_style_border_color(s_pet_bubble, COL_TEAL, 0);
    lv_obj_set_style_border_opa(s_pet_bubble, LV_OPA_60, 0);
    s_pet_bubble_label = mk_label(s_pet_bubble, "喵～ 你好呀！我是 Mimi。",
                                  &epw_cjk_16, COL_TEXT,
                                  16, 13, 268, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(s_pet_bubble_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_height(s_pet_bubble_label, 22);

    /* 装饰层不拦截触摸，点按交给 s_pet / 猫咪本体处理 */
    lv_obj_remove_flag(s_pet_halo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_pet_glow, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_pet_bubble, LV_OBJ_FLAG_CLICKABLE);

    /* 全部交互走手势：宠物区点按/长按/滑，空白区右滑退出 */

    lv_obj_add_flag(s_pet, LV_OBJ_FLAG_HIDDEN);
    pet_apply(UI_PET_IDLE, NULL);
}

/* -------------------------------------------------------------------------- */
/* 3S 英语儿歌页                                                                */
/* -------------------------------------------------------------------------- */

static const char *nur_mode_text(void)
{
    switch (nursery_get_mode()) {
    case EPW_NURSERY_MODE_SINGLE:
        return "单曲循环";
    case EPW_NURSERY_MODE_SHUFFLE:
        return "随机播放";
    case EPW_NURSERY_MODE_LIST:
    default:
        return "列表循环";
    }
}

static void nur_format_time(uint32_t ms, char *out, size_t size)
{
    uint32_t sec = ms / 1000u;
    snprintf(out, size, "%u:%02u", (unsigned)(sec / 60u), (unsigned)(sec % 60u));
}

/* 播放器 / 本周曲库：两张卡同页上下切换，不做第二层导航 */
static void nursery_card_set(int card, bool animate)
{
    if (s_nur_player_card == NULL || s_nur_week_card == NULL) {
        return;
    }
    s_nur_card = card == 1 ? 1 : 0;
    lv_label_set_text(s_nur_library_label, s_nur_card ? "返回播放" : "本周曲库");
    int player_to = s_nur_card == 0 ? NUR_CARD_PLAYER_Y
                                    : NUR_CARD_PLAYER_Y - NUR_CARD_SHIFT;
    int week_to = s_nur_card == 0 ? NUR_CARD_WEEK_Y : NUR_CARD_PLAYER_Y;

    lv_anim_delete(s_nur_player_card, anim_y_cb);
    lv_anim_delete(s_nur_week_card, anim_y_cb);
    if (!animate) {
        lv_obj_set_y(s_nur_player_card, player_to);
        lv_obj_set_y(s_nur_week_card, week_to);
        nursery_update_ui();
        return;
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_nur_player_card);
    lv_anim_set_values(&a, lv_obj_get_y(s_nur_player_card), player_to);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_nur_week_card);
    lv_anim_set_values(&a, lv_obj_get_y(s_nur_week_card), week_to);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_start(&a);

    s_nur_ui_ms = 0;
    nursery_update_ui();
}

/* 本周星星 / 曲库列表 / 愿望卡：只在打开页面、换歌和定时慢刷新时整体重建 */
static void nursery_refresh_week(void)
{
    if (s_nur_week_card == NULL) {
        return;
    }

    int stars = nursery_checkin_week_stars();
    int goal = nursery_checkin_star_goal();
    for (int i = 0; i < 5; i++) {
        if (s_nur_stars[i] == NULL) {
            continue;
        }
        bool on = i < stars;
        lv_obj_set_style_bg_color(s_nur_stars[i], on ? COL_AMBER : COL_SURF, 0);
        lv_obj_set_style_border_color(s_nur_stars[i], on ? COL_AMBER : COL_LINE, 0);
    }
    if (s_nur_week_stars != NULL) {
        lv_label_set_text_fmt(s_nur_week_stars, "%d/%d 星", stars, goal);
        lv_obj_set_style_text_color(s_nur_week_stars,
                                    stars >= goal ? COL_AMBER : COL_MUTED, 0);
    }

    int count = nursery_weekly_count();
    for (int i = 0; i < EPW_NURSERY_WEEKLY_MAX; i++) {
        if (s_nur_week_row[i] == NULL) {
            continue;
        }
        const epw_nursery_song_t *song = NULL;
        if (i < count) {
            int idx = nursery_weekly_index(i);
            song = idx >= 0 ? nursery_song(idx) : NULL;
        }
        if (song == NULL) {
            lv_obj_add_flag(s_nur_week_row[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_nur_week_row[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_nur_week_title[i], song->title);
        char meta[16];
        nur_format_time((uint32_t)song->duration * 1000u, meta, sizeof(meta));
        lv_label_set_text(s_nur_week_meta[i], meta);
    }

    if (s_nur_wish_btn != NULL) {
        bool ready = nursery_checkin_wish_ready();
        bool redeemed = nursery_checkin_wish_redeemed();
        lv_obj_set_style_bg_color(s_nur_wish_btn,
                                  ready ? COL_AMBER : COL_SURF, 0);
        lv_obj_set_style_border_color(s_nur_wish_btn,
                                      ready ? COL_AMBER : COL_LINE, 0);
        lv_obj_set_style_text_color(s_nur_wish_label,
                                    ready ? COL_INK : COL_MUTED, 0);
        if (redeemed) {
            lv_label_set_text(s_nur_wish_label, "愿望卡已兑换");
        } else if (ready) {
            lv_label_set_text(s_nur_wish_label, "兑换愿望卡");
        } else {
            lv_label_set_text_fmt(s_nur_wish_label, "集满 %d 星换愿望卡", goal);
        }
    }
    s_nur_last_stars = stars;
    s_nur_last_done = nursery_checkin_today_done();
}

static void nursery_update_ui(void)
{
    if (s_nur_drag_engaged || page_anim_busy() ||
        (!s_nursery_open && !s_nur_force_update) || s_nur_player_card == NULL) {
        return;
    }

    epw_nursery_state_t state = nursery_get_state();
    int idx = nursery_current_index();
    const epw_nursery_song_t *song = idx >= 0 ? nursery_song(idx) : NULL;

    if (s_nur_volume != NULL) {
        int volume = nursery_get_volume();
        if (volume == 0) lv_label_set_text(s_nur_volume, "静音 · 调节");
        else lv_label_set_text_fmt(s_nur_volume, "音量 %d%% · 调节", volume);
    }
    if (s_nur_mode_label != NULL) {
        lv_label_set_text(s_nur_mode_label, nur_mode_text());
    }
    if (s_nur_title != NULL) {
        const char *title = song != NULL ? song->title : NULL;
        if (title == NULL) {
            title = nursery_catalog_loading() ? "曲库加载中…" : "选一首儿歌";
        }
        lv_label_set_text(s_nur_title, title);
    }

    if (s_nur_lyric != NULL) {
        char lyric[EPW_NURSERY_LYRIC_TEXT];
        nursery_get_lyric(lyric, sizeof(lyric));
        const char *text = lyric;
        if (text[0] == '\0') {
            const char *st = nursery_status_text();
            text = nursery_catalog_loading() && song == NULL ? "" :
                   (st[0] != '\0' ? st : (song != NULL ? "跟着一起唱吧" : "点播放开始"));
        }
        lv_label_set_text(s_nur_lyric, text);
        lv_obj_set_style_text_color(s_nur_lyric,
                                    state == EPW_NURSERY_ERROR ? COL_CORAL : COL_MUTED, 0);
    }

    uint32_t elapsed = nursery_elapsed_ms();
    uint32_t duration = nursery_duration_s();
    if (duration == 0 && song != NULL) {
        duration = song->duration;
    }
    if (s_nur_time != NULL) {
        if (duration > 0) {
            char a[16], b[16];
            nur_format_time(elapsed, a, sizeof(a));
            nur_format_time(duration * 1000u, b, sizeof(b));
            lv_label_set_text_fmt(s_nur_time, "%s / %s", a, b);
        } else {
            lv_label_set_text(s_nur_time, "");
        }
    }
    if (s_nur_arc != NULL) {
        int pct = duration > 0 ? (int)(elapsed * 100u / (duration * 1000u)) : 0;
        if (pct < 0) {
            pct = 0;
        }
        if (pct > 100) {
            pct = 100;
        }
        lv_arc_set_angles(s_nur_arc, 0, pct > 0 ? pct * 359 / 100 : 0);
    }
    if (s_nur_icon_play != NULL && s_nur_icon_pause != NULL) {
        bool playing = state == EPW_NURSERY_PLAYING;
        if (playing) {
            lv_obj_add_flag(s_nur_icon_play, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_nur_icon_pause, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(s_nur_icon_play, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_nur_icon_pause, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* 慢刷新：星星 / 打卡 / 愿望卡都要读 NVS，2 秒一次足够 */
    uint32_t now = lv_tick_get();
    bool heavy = s_nur_ui_ms == 0 || (uint32_t)(now - s_nur_ui_ms) >= 2000u;
    if (heavy) {
        s_nur_ui_ms = now;
        nursery_refresh_week();
        for (int i = 0; i < 3; i++) {
            if (s_nur_slot[i] == NULL || s_nur_slot_label[i] == NULL) {
                continue;
            }
            epw_nursery_session_t sess;
            nursery_checkin_session(i, &sess);
            static const char *const short_cn[3] = {"早", "午", "晚"};
            char text[24];
            lv_color_t accent = COL_MUTED;
            lv_color_t bg = COL_SURF;
            lv_color_t border = COL_LINE;
            if (sess.done) {
                snprintf(text, sizeof(text), "%s 完成", short_cn[i]);
                accent = COL_AMBER;
                bg = COL_SURF_HI;
                border = COL_AMBER;
            } else if (sess.active) {
                if (sess.paused) {
                    snprintf(text, sizeof(text), "%s 暂停", short_cn[i]);
                } else {
                    snprintf(text, sizeof(text), "%s %d/%d", short_cn[i],
                             sess.elapsed_seconds / 60, sess.target_minutes);
                }
                accent = COL_TEAL;
                bg = COL_SURF_HI;
                border = COL_TEAL;
            } else {
                snprintf(text, sizeof(text), "%s %d", short_cn[i], sess.target_minutes);
            }
            lv_label_set_text(s_nur_slot_label[i], text);
            lv_obj_set_style_text_color(s_nur_slot_label[i], accent, 0);
            lv_obj_set_style_bg_color(s_nur_slot[i], bg, 0);
            lv_obj_set_style_border_color(s_nur_slot[i], border, 0);
        }
    }

    if (s_nur_wish_btn != NULL) {
        if (s_nur_card == 1) {
            lv_obj_remove_flag(s_nur_wish_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_nur_wish_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* 儿歌页 500ms 心跳：推进播放进度 / 歌词 / 打卡。
 * 打卡按真实播放时间累计，离开页面但歌曲仍在播时也要继续计时。 */
static void nursery_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    nursery_checkin_tick();
    if (!s_nur_drag_engaged && !page_anim_busy()) {
        nursery_update_ui();
    }
    /* 拖动/打开期间不动快照，避免拖到一半图源被换掉 */
    if (s_nursery_open || s_nur_dragging) {
        return;
    }
    /* 曲库从「加载中」变成「就绪」时重拍快照，避免一直停在空壳画面 */
    bool ready = nursery_catalog_ready();
    if (ready != s_nur_catalog_seen) {
        s_nur_catalog_seen = ready;
        s_nur_snap_valid = false;
        s_nur_snap_ext = 0;
    }
    /* 表盘空闲时预渲染：下一次右滑只搬这张位图，和控制中心下拉同一套做法 */
    if (!s_nursery_open && !s_nur_dragging && !s_nur_snap_valid &&
        !s_center_open && !s_center_dragging && !page_anim_busy() &&
        !pages_busy_except_center()) {
        nursery_snap_refresh();
    }
}

/* ---- 交互 ----------------------------------------------------------------- */

static void nursery_page_gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE || page_anim_busy()) {
        return;
    }
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        return;
    }
    switch (lv_indev_get_gesture_dir(indev)) {
    case LV_DIR_TOP:
        if (s_nur_card == 0) {
            nursery_card_set(1, true);
        }
        break;
    case LV_DIR_BOTTOM:
        if (s_nur_card == 1) {
            nursery_card_set(0, true);
        }
        break;
    case LV_DIR_LEFT:
        /* 跟手拖动已接管时由 PRESSING 收尾；这里只兜底没产生 PRESSING 的慢滑 */
        if (!s_nur_drag_engaged) {
            nursery_close();
        }
        break;
    default:
        break;
    }
}

static void nur_library_cb(lv_event_t *e)
{
    if (!tap_ignore(e)) {
        nursery_card_set(s_nur_card == 0 ? 1 : 0, true);
    }
}

static void nur_play_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    epw_nursery_state_t state = nursery_get_state();
    if (state == EPW_NURSERY_PLAYING || state == EPW_NURSERY_PAUSED) {
        nursery_toggle();
    } else if (nursery_current_index() >= 0) {
        nursery_play_index(nursery_current_index());
    } else if (nursery_weekly_count() > 0) {
        nursery_play_weekly(0);
    }
    if (nursery_get_state() == EPW_NURSERY_IDLE) {
        const char *st = nursery_status_text();
        toast_show(st[0] != '\0' ? st : "曲库加载中");
    }
    s_nur_ui_ms = 0;
    nursery_update_ui();
}

static void nur_prev_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    if (nursery_get_state() == EPW_NURSERY_IDLE) {
        nur_play_cb(e);
        return;
    }
    nursery_prev();
    nursery_update_ui();
}

static void nur_next_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    if (nursery_get_state() == EPW_NURSERY_IDLE) {
        nur_play_cb(e);
        return;
    }
    nursery_next();
    nursery_update_ui();
}

static void nur_mode_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    nursery_cycle_mode();
    nursery_update_ui();
}

static void nur_slot_cb(lv_event_t *e)
{
    if (page_anim_busy()) {
        return;
    }
    /* 长按结束打卡后会补发 CLICKED，这里吞掉 */
    if (s_nur_slot_long) {
        s_nur_slot_long = false;
        return;
    }
    if (tap_ignore(e)) {
        return;
    }
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (!nursery_checkin_enabled()) {
        toast_show("等待校时");
        return;
    }
    epw_nursery_session_t sess;
    nursery_checkin_session(slot, &sess);
    if (sess.done) {
        toast_show("这场已打卡");
        return;
    }
    if (sess.active) {
        nursery_checkin_pause_toggle();
    } else if (nursery_checkin_start(slot)) {
        nursery_card_set(0, true);
        toast_show("开始打卡");
    } else {
        const char *st = nursery_status_text();
        toast_show(st[0] != '\0' ? st : "暂时不能打卡");
    }
    s_nur_ui_ms = 0;
    nursery_update_ui();
}

static void nur_slot_long_cb(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    epw_nursery_session_t sess;
    nursery_checkin_session(slot, &sess);
    if (!sess.active) {
        return;
    }
    s_nur_slot_long = true;
    nursery_checkin_end();
    toast_show("已结束打卡");
    s_nur_ui_ms = 0;
    nursery_update_ui();
}

static void nur_week_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    nursery_card_set(0, true);
    nursery_play_weekly(slot);
    nursery_update_ui();
}

static void nur_week_long_cb(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (nursery_weekly_replace(slot)) {
        toast_show("换了一首");
        nursery_refresh_week();
    } else {
        const char *st = nursery_status_text();
        toast_show(st[0] != '\0' ? st : "现在不能换歌");
    }
}

static void nur_wish_cb(lv_event_t *e)
{
    if (tap_ignore(e)) {
        return;
    }
    if (nursery_checkin_redeem_wish()) {
        toast_show("愿望卡已兑换");
    } else if (nursery_checkin_wish_redeemed()) {
        toast_show("本周已兑换");
    } else {
        toast_show("还差几颗星");
    }
    nursery_refresh_week();
}

/* ---- 页面构建 -------------------------------------------------------------- */

static lv_obj_t *nur_card_create(lv_obj_t *parent, int y)
{
    lv_obj_t *card = mk_box(parent, NUR_CARD_X, y, NUR_CARD_W, NUR_CARD_H,
                            COL_SURF, LV_OPA_COVER, 34);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, COL_LINE, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_60, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static void nur_volume_cb(lv_event_t *e)
{
    if (!tap_ignore(e)) slider_open(SLIDER_VOLUME);
}

static void build_nursery_player(lv_obj_t *card)
{
    s_nur_title = mk_label(card, "选一首儿歌", &epw_cjk_20, COL_TEXT,
                           16, 18, NUR_CARD_W - 32, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(s_nur_title, LV_LABEL_LONG_DOT);
    lv_obj_set_height(s_nur_title, 28);

    s_nur_lyric = mk_label(card, "", &epw_cjk_16, COL_MUTED,
                           16, 60, NUR_CARD_W - 32, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(s_nur_lyric, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(s_nur_lyric, 42);

    /* 环形进度：只做视觉反馈，不接收点击 */
    s_nur_arc = lv_arc_create(card);
    lv_obj_remove_flag(s_nur_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_nur_arc, 125, 110);
    lv_obj_set_size(s_nur_arc, 128, 128);
    lv_obj_remove_style(s_nur_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_nur_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_nur_arc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_nur_arc, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_nur_arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_nur_arc, COL_LINE, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_nur_arc, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_nur_arc, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_nur_arc, COL_TEAL, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_nur_arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_nur_arc, true, LV_PART_INDICATOR);
    lv_arc_set_rotation(s_nur_arc, 270);
    lv_arc_set_bg_angles(s_nur_arc, 0, 359);
    lv_arc_set_angles(s_nur_arc, 0, 0);

    /* 播放 / 暂停大按钮 */
    s_nur_play_btn = mk_circle(card, NUR_CARD_W / 2, 174, 104,
                               COL_TEAL, LV_OPA_COVER);
    lv_obj_add_flag(s_nur_play_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_nur_play_btn, nur_play_cb, LV_EVENT_CLICKED, NULL);

    static const lv_point_precise_t tri_play[] = {
        {39, 30}, {39, 74}, {74, 52}, {39, 30},
    };
    s_nur_icon_play = lv_line_create(s_nur_play_btn);
    lv_line_set_points(s_nur_icon_play, tri_play, 4);
    lv_obj_set_style_line_width(s_nur_icon_play, 7, 0);
    lv_obj_set_style_line_color(s_nur_icon_play, COL_INK, 0);
    lv_obj_set_style_line_rounded(s_nur_icon_play, true, 0);

    s_nur_icon_pause = mk_box(s_nur_play_btn, 36, 34, 32, 36,
                              COL_BG, LV_OPA_TRANSP, 0);
    mk_box(s_nur_icon_pause, 0, 0, 10, 36, COL_INK, LV_OPA_COVER, 2);
    mk_box(s_nur_icon_pause, 22, 0, 10, 36, COL_INK, LV_OPA_COVER, 2);
    lv_obj_add_flag(s_nur_icon_pause, LV_OBJ_FLAG_HIDDEN);

    /* 上一首 / 下一首 */
    lv_obj_t *prev = mk_circle(card, NUR_CARD_W / 2 - 130, 174, 72,
                               COL_SURF_HI, LV_OPA_COVER);
    s_nur_prev_btn = prev;
    lv_obj_set_style_border_width(prev, 1, 0);
    lv_obj_set_style_border_color(prev, COL_LINE, 0);
    lv_obj_add_flag(prev, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(prev, nur_prev_cb, LV_EVENT_CLICKED, NULL);
    static const lv_point_precise_t tri_prev[] = {{48, 20}, {48, 52}, {28, 36}, {48, 20}};
    lv_obj_t *pl = lv_line_create(prev);
    lv_line_set_points(pl, tri_prev, 4);
    lv_obj_set_style_line_width(pl, 4, 0);
    lv_obj_set_style_line_color(pl, COL_MUTED, 0);
    lv_obj_set_style_line_rounded(pl, true, 0);
    mk_box(prev, 19, 20, 5, 32, COL_MUTED, LV_OPA_COVER, 2);

    lv_obj_t *next = mk_circle(card, NUR_CARD_W / 2 + 130, 174, 72,
                               COL_SURF_HI, LV_OPA_COVER);
    s_nur_next_btn = next;
    lv_obj_set_style_border_width(next, 1, 0);
    lv_obj_set_style_border_color(next, COL_LINE, 0);
    lv_obj_add_flag(next, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(next, nur_next_cb, LV_EVENT_CLICKED, NULL);
    static const lv_point_precise_t tri_next[] = {{24, 20}, {24, 52}, {44, 36}, {24, 20}};
    lv_obj_t *nl = lv_line_create(next);
    lv_line_set_points(nl, tri_next, 4);
    lv_obj_set_style_line_width(nl, 4, 0);
    lv_obj_set_style_line_color(nl, COL_MUTED, 0);
    lv_obj_set_style_line_rounded(nl, true, 0);
    mk_box(next, 49, 20, 5, 32, COL_MUTED, LV_OPA_COVER, 2);

    s_nur_time = mk_label(card, "", &epw_cjk_16, COL_MUTED,
                          8, 248, 174, LV_TEXT_ALIGN_CENTER);
    s_nur_volume = mk_label(card, "", &epw_cjk_16, COL_TEAL,
                            184, 240, 186, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_height(s_nur_volume, 44);
    lv_obj_add_event_cb(s_nur_volume, nur_volume_cb, LV_EVENT_CLICKED, NULL);
}

static void build_nursery_week(lv_obj_t *card)
{
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_scroll_dir(card, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);
    mk_label(card, "本周曲库", &epw_cjk_20, COL_TEXT,
             18, 14, 170, LV_TEXT_ALIGN_LEFT);
    s_nur_week_stars = mk_label(card, "", &epw_cjk_16, COL_MUTED,
                                200, 20, 160, LV_TEXT_ALIGN_RIGHT);

    for (int i = 0; i < EPW_NURSERY_WEEKLY_MAX; i++) {
        lv_obj_t *row = mk_box(card, 16, 52 + i * 56, NUR_CARD_W - 32, 50,
                               COL_SURF_HI, LV_OPA_COVER, 19);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, COL_LINE, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_40, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, nur_week_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, nur_week_long_cb, LV_EVENT_LONG_PRESSED,
                            (void *)(intptr_t)i);
        s_nur_week_row[i] = row;

        char no[4];
        snprintf(no, sizeof(no), "%d", i + 1);
        mk_label(row, no, &epw_cjk_16, COL_MUTED, 10, 15, 22, LV_TEXT_ALIGN_CENTER);
        s_nur_week_title[i] = mk_label(row, "", &epw_cjk_16, COL_TEXT,
                                       38, 15, 198, LV_TEXT_ALIGN_LEFT);
        lv_label_set_long_mode(s_nur_week_title[i], LV_LABEL_LONG_DOT);
        lv_obj_set_height(s_nur_week_title[i], 22);
        s_nur_week_meta[i] = mk_label(row, "", &epw_cjk_16, COL_MUTED,
                                      244, 15, 86, LV_TEXT_ALIGN_RIGHT);
    }
    /* 模式胶囊：点一下切换 列表 / 单曲 / 随机 */
    lv_obj_t *mode = mk_box(card, 18, 342, 150, 44, COL_SURF_HI, LV_OPA_COVER, 15);
    s_nur_mode_btn = mode;
    lv_obj_set_style_border_width(mode, 1, 0);
    lv_obj_set_style_border_color(mode, COL_TEAL, 0);
    lv_obj_set_style_border_opa(mode, LV_OPA_40, 0);
    lv_obj_add_flag(mode, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mode, nur_mode_cb, LV_EVENT_CLICKED, NULL);
    s_nur_mode_label = mk_label(mode, nur_mode_text(), &epw_cjk_16, COL_TEAL,
                                0, 12, 150, LV_TEXT_ALIGN_CENTER);

}

static void build_nursery(void)
{
    s_nursery = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_nursery);
    lv_obj_remove_flag(s_nursery, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_nursery, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_nursery, 0, 0);
    lv_obj_set_style_bg_color(s_nursery, lv_color_hex(0x05070C), 0);
    lv_obj_set_style_bg_grad_color(s_nursery, lv_color_hex(0x101A26), 0);
    lv_obj_set_style_bg_grad_dir(s_nursery, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_nursery, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_nursery, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_nursery, LV_OBJ_FLAG_CLICKABLE);
    /* 拖动中页面可能被快照图顶替/隐藏，锁定按下的对象，保证 PRESSING 不断流 */
    lv_obj_add_flag(s_nursery, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(s_nursery, nursery_page_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s_nursery, nursery_page_drag_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_nursery, nursery_page_drag_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_nursery, nursery_page_drag_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_nursery, nursery_page_drag_cb, LV_EVENT_PRESS_LOST, NULL);

    mk_label(s_nursery, "3S 英语儿歌", &epw_cjk_20, COL_TEXT,
             0, 50, SCREEN_W, LV_TEXT_ALIGN_CENTER);

    s_nur_player_card = nur_card_create(s_nursery, NUR_CARD_PLAYER_Y);
    build_nursery_player(s_nur_player_card);

    s_nur_week_card = nur_card_create(s_nursery, NUR_CARD_WEEK_Y);
    build_nursery_week(s_nur_week_card);

    /* 本周星星：一个圆点代表一颗星，右侧用 Y/N 星补足语义 */
    mk_label(s_nur_week_card, "本周", &epw_cjk_16, COL_MUTED,
             18, 405, 60, LV_TEXT_ALIGN_RIGHT);
    for (int i = 0; i < 5; i++) {
        s_nur_stars[i] = mk_circle(s_nur_week_card, 112 + i * 32, 414, 14,
                                   COL_SURF, LV_OPA_COVER);
        lv_obj_set_style_border_width(s_nur_stars[i], 1, 0);
        lv_obj_set_style_border_color(s_nur_stars[i], COL_LINE, 0);
    }

    /* 早 / 午 / 晚三场打卡：点按开始或暂停，长按结束 */
    static const int slot_x[3] = {18, 132, 246};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *chip = mk_box(s_nur_week_card, slot_x[i], 454, 104, 44,
                                COL_SURF, LV_OPA_COVER, 16);
        lv_obj_set_style_border_width(chip, 1, 0);
        lv_obj_set_style_border_color(chip, COL_LINE, 0);
        lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(chip, nur_slot_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(chip, nur_slot_long_cb, LV_EVENT_LONG_PRESSED,
                            (void *)(intptr_t)i);
        s_nur_slot[i] = chip;
        s_nur_slot_label[i] = mk_label(chip, "", &epw_cjk_16, COL_MUTED,
                                       0, 12, 104, LV_TEXT_ALIGN_CENTER);
    }

    s_nur_wish_btn = mk_box(s_nur_week_card, 74, 518, 230, 48,
                            COL_SURF, LV_OPA_COVER, 18);
    lv_obj_set_style_border_width(s_nur_wish_btn, 1, 0);
    lv_obj_set_style_border_color(s_nur_wish_btn, COL_LINE, 0);
    lv_obj_add_flag(s_nur_wish_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_nur_wish_btn, nur_wish_cb, LV_EVENT_CLICKED, NULL);
    s_nur_wish_label = mk_label(s_nur_wish_btn, "集满 5 星换愿望卡", &epw_cjk_16,
                                COL_MUTED, 0, 14, 230, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_flag(s_nur_wish_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *library = mk_box(s_nursery, 139, 403, 188, 44,
                               COL_SURF_HI, LV_OPA_COVER, 22);
    lv_obj_add_event_cb(library, nur_library_cb, LV_EVENT_CLICKED, NULL);
    s_nur_library_label = mk_label(library, "本周曲库", &epw_cjk_20, COL_TEXT,
                                    0, 10, 188, LV_TEXT_ALIGN_CENTER);

    /* 装饰容器不拦触摸，让滑动落到整页做翻卡/返回；只保留按钮的点击，
     * 并让按钮上的手势继续冒泡到页面。 */
    clear_clickable_tree(s_nursery);
    lv_obj_t *interactive[] = {
        library, s_nur_volume, s_nur_mode_btn, s_nur_prev_btn, s_nur_play_btn, s_nur_next_btn,
        s_nur_slot[0], s_nur_slot[1], s_nur_slot[2],
        s_nur_week_row[0], s_nur_week_row[1], s_nur_week_row[2],
        s_nur_week_row[3], s_nur_week_row[4], s_nur_wish_btn,
    };
    for (size_t i = 0; i < sizeof(interactive) / sizeof(interactive[0]); i++) {
        if (interactive[i] != NULL) {
            lv_obj_add_flag(interactive[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
        }
    }
    lv_obj_add_flag(s_nur_week_card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(s_nursery, LV_OBJ_FLAG_HIDDEN);

    /* 滑入时只搬运这张快照，和下拉控制中心同一套做法 */
    s_nur_snap_img = lv_image_create(s_screen);
    lv_obj_remove_flag(s_nur_snap_img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_nur_snap_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_nur_snap_img, -SCREEN_W, 0);
}

/* 电源确认放在顶层，不能穿透到播放器、计时器或桌面。 */
static lv_obj_t *s_boot_logo;
static lv_obj_t *s_power_panel, *s_power_knob, *s_power_slide_text, *s_power_title;
static bool s_power_pending, s_power_dragging;
static lv_point_t s_power_press;
#define POWER_KNOB_MIN 4
#define POWER_KNOB_MAX 224

void ui_boot_logo_show(void)
{
    lv_obj_set_style_bg_color(lv_screen_active(), COL_BG, 0);
    s_boot_logo = mk_box(lv_layer_top(), 0, 0, SCREEN_W, SCREEN_H,
                         COL_BG, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_boot_logo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *logo = lv_image_create(s_boot_logo);
    lv_image_set_src(logo, &pip_face_idle);
    lv_obj_set_pos(logo, 129, 112);
    mk_label(s_boot_logo, "Mimi", &epw_cjk_20, COL_TEXT,
             0, 336, SCREEN_W, LV_TEXT_ALIGN_CENTER);
    /* 先送出 Logo，再构建桌面缓存，避免首次绘制被初始化挡住。 */
    lv_refr_now(NULL);
}

void ui_boot_logo_hide(void)
{
    if (s_boot_logo != NULL) {
        lv_obj_delete(s_boot_logo);
        s_boot_logo = NULL;
    }
    ui_activity();
    lv_indev_t *input = bsp_display_get_input_dev();
    if (input != NULL) lv_indev_wait_release(input);
}

static void power_knob_x_cb(void *obj, int32_t x)
{
    lv_obj_set_x(obj, x);
    lv_obj_set_style_opa(s_power_slide_text,
        LV_OPA_COVER * (POWER_KNOB_MAX - x) / (POWER_KNOB_MAX - POWER_KNOB_MIN), 0);
}

bool ui_power_menu_cancel(void)
{
    if (s_power_panel == NULL || lv_obj_has_flag(s_power_panel, LV_OBJ_FLAG_HIDDEN)) {
        return false;
    }
    if (!s_power_pending) {
        s_power_dragging = false;
        lv_anim_delete(s_power_knob, power_knob_x_cb);
        power_knob_x_cb(s_power_knob, POWER_KNOB_MIN);
        lv_obj_add_flag(s_power_panel, LV_OBJ_FLAG_HIDDEN);
        ui_activity();
    }
    return true;
}

static void power_cancel_cb(lv_event_t *e)
{
    if (!tap_ignore(e)) ui_power_menu_cancel();
}

static void power_slide_cb(lv_event_t *e)
{
    lv_indev_t *input = lv_event_get_indev(e);
    if (input == NULL || s_power_pending) return;
    lv_point_t p;
    lv_indev_get_point(input, &p);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_power_dragging = false;
        if (ui_activity()) {
            lv_indev_wait_release(input);
            return;
        }
        lv_anim_delete(s_power_knob, power_knob_x_cb);
        power_knob_x_cb(s_power_knob, POWER_KNOB_MIN);
        s_power_press = p;
        s_power_dragging = true;
    } else if (code == LV_EVENT_PRESSING && s_power_dragging) {
        ui_activity();
        int x = POWER_KNOB_MIN + p.x - s_power_press.x;
        if (LV_ABS(p.y - s_power_press.y) > 48) {
            s_power_dragging = false;
            x = POWER_KNOB_MIN;
        }
        if (x < POWER_KNOB_MIN) x = POWER_KNOB_MIN;
        if (x > POWER_KNOB_MAX) x = POWER_KNOB_MAX;
        power_knob_x_cb(s_power_knob, x);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        bool confirmed = s_power_dragging && code == LV_EVENT_RELEASED &&
            LV_ABS(p.y - s_power_press.y) <= 48 &&
            lv_obj_get_x(s_power_knob) >= POWER_KNOB_MAX - 8;
        s_power_dragging = false;
        if (confirmed) {
            s_power_pending = true;
            lv_obj_set_style_opa(s_power_slide_text, LV_OPA_COVER, 0);
            lv_label_set_text(s_power_slide_text, "正在关机…");
            lv_obj_add_flag(s_power_knob, LV_OBJ_FLAG_HIDDEN);
            watch_timer_reset();
            nursery_checkin_pause();
            nursery_stop();
            button_request_shutdown();
        } else {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, s_power_knob);
            lv_anim_set_values(&a, lv_obj_get_x(s_power_knob), POWER_KNOB_MIN);
            lv_anim_set_duration(&a, 180);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_set_exec_cb(&a, power_knob_x_cb);
            lv_anim_start(&a);
        }
    }
}

void ui_power_menu_open(void)
{
    if (s_boot_logo != NULL || s_power_pending) return;
    ui_activity();
    voice_client_set_active(false);
    nursery_pause();
    if (s_power_panel == NULL) {
        s_power_panel = mk_box(lv_layer_top(), 0, 0, SCREEN_W, SCREEN_H,
                               COL_BG, LV_OPA_COVER, 0);
        lv_obj_add_flag(s_power_panel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(s_power_panel, LV_OBJ_FLAG_GESTURE_BUBBLE);
        s_power_title = mk_label(s_power_panel, "关机", &epw_cjk_20, COL_TEXT,
                 0, 112, SCREEN_W, LV_TEXT_ALIGN_CENTER);
        lv_obj_t *track = mk_box(s_power_panel, 83, 194, 300, 80,
                                 COL_SURF_HI, LV_OPA_COVER, 40);
        s_power_slide_text = mk_label(track, "滑动来关机", &epw_cjk_20, COL_MUTED,
                                       0, 28, 300, LV_TEXT_ALIGN_CENTER);
        s_power_knob = mk_circle(track, 40, 40, 72, COL_TEXT, LV_OPA_COVER);
        lv_obj_add_flag(s_power_knob, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_remove_flag(s_power_knob, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_t *arc = mk_arc_seg(s_power_knob, 36, 38, 30, 4, 40, 320, COL_INK, LV_OPA_COVER);
        mk_box(s_power_knob, 34, 17, 4, 23, COL_INK, LV_OPA_COVER, 2);
        (void)arc;
        tools_button(s_power_panel, 173, 344, 120, "取消", power_cancel_cb, 0);
        clear_clickable_tree(s_power_knob);
        lv_obj_add_event_cb(s_power_knob, power_slide_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(s_power_knob, power_slide_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(s_power_knob, power_slide_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(s_power_knob, power_slide_cb, LV_EVENT_PRESS_LOST, NULL);
    }
    s_power_dragging = false;
    lv_anim_delete(s_power_knob, power_knob_x_cb);
    power_knob_x_cb(s_power_knob, POWER_KNOB_MIN);
    lv_label_set_text(s_power_title, "关机");
    lv_label_set_text(s_power_slide_text, "滑动来关机");
    lv_obj_remove_flag(s_power_knob, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_power_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_power_panel);
    lv_indev_t *input = bsp_display_get_input_dev();
    if (input != NULL) lv_indev_wait_release(input);
}

void ui_power_shutdown_failed(void)
{
    s_power_pending = false;
    ui_power_menu_open();
    lv_label_set_text(s_power_title, "关机失败，请检查供电");
}

/* -------------------------------------------------------------------------- */
/* 熄屏 / 亮屏                                                                  */
/* -------------------------------------------------------------------------- */

static void screen_wake(void)
{
    if (!s_screen_off) {
        return;
    }
    s_screen_off = false;
    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev != NULL && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
        lv_indev_wait_release(indev);
    }
    if (s_wake_layer != NULL) {
        lv_obj_add_flag(s_wake_layer, LV_OBJ_FLAG_HIDDEN);
    }
    brightness_apply();
    home_snap_refresh();
    s_last_activity = lv_tick_get();
    ESP_LOGI(TAG, "display wake");
}

static void screen_sleep(void)
{
    if (s_screen_off) {
        return;
    }
    s_screen_off = true;
    /* 关亮度即可；透明触摸层保留屏内画面，唤醒不会出现逐行揭开黑幕。 */
    if (s_wake_layer != NULL) {
        lv_obj_remove_flag(s_wake_layer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_wake_layer);
    }
    brightness_apply();
    /* 熄屏即收起儿歌页并暂停外放，醒来保留当前曲目 */
    nursery_close_now();
    nursery_pause();
    /* 熄屏即退出 Mimi Cat：不在黑屏下保留对话状态，也不再回答 */
    pet_close_now();
    ESP_LOGI(TAG, "display sleep (idle %lu ms)", (unsigned long)UI_IDLE_TIMEOUT_MS);
}

bool ui_activity(void)
{
    s_last_activity = lv_tick_get();
    if (!s_screen_off) {
        return false;
    }
    screen_wake();
    return true;
}

static void wake_layer_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_activity();
}

/* Mimi 保持常亮；其他页面按用户设置的空闲时长熄屏。 */
static void idle_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (s_pet_open) {
        s_last_activity = lv_tick_get();
        return;
    }
    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev != NULL && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
        s_last_activity = lv_tick_get();
        if (s_screen_off) {
            screen_wake();
        }
        return;
    }
    if (!s_screen_off && UI_IDLE_TIMEOUT_MS != 0 && lv_tick_elaps(s_last_activity) >= UI_IDLE_TIMEOUT_MS) {
        screen_sleep();
    } else if (!s_screen_off && !s_center_dragging &&
               !pages_busy_except_center() && !page_anim_busy()) {
        home_snap_refresh();
    }
}

/* -------------------------------------------------------------------------- */
/* 初始化                                                                      */
/* -------------------------------------------------------------------------- */

void ui_init(void)
{
    watch_system_init();
    s_screen = lv_screen_active();
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    /* 关键：screen 默认可滚动，手指拖动子页面时会把整屏内容一起滚走，
     * 导致退出音量页后下层控制中心也停在右移状态。关掉后手势只属于当前页 */
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev != NULL) {
        lv_indev_set_long_press_time(indev, 500);
        /* 圆屏上 50px 手势门槛偏低且要求每帧 3px 速度，慢滑常常识别不到；
         * 收到 30px / 1px 后下拉控制中心、右滑关闭等手势更跟手 */
        lv_indev_set_gesture_min_distance(indev, 30);
        lv_indev_set_gesture_min_velocity(indev, 1);
    }

    /* 表盘（唯一的常驻页面） */
    s_home = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_home);
    lv_obj_remove_flag(s_home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_home, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_home, 0, 0);
    lv_obj_add_flag(s_home, LV_OBJ_FLAG_CLICKABLE);
    /* 下拉过程中上层快照图不拦截命中，锁定表盘为按下对象，手势永不断流 */
    lv_obj_add_flag(s_home, LV_OBJ_FLAG_PRESS_LOCK);
    /* 手势不向上冒泡：表盘自己处理下拉，避免事件跑到 screen */
    lv_obj_remove_flag(s_home, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* 表盘右滑跟手拉出 3S 英语儿歌；左滑在儿歌页内跟手返回表盘。
     * 必须在 build_dial 之前注册：LVGL 按注册顺序分发事件，横滑要先判定，
     * 否则斜着下滑时 home_pull_cb 会先抢走手势，右滑永远打不开儿歌页。 */
    lv_obj_add_event_cb(s_home, home_nursery_drag_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_home, home_nursery_drag_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_home, home_nursery_drag_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_home, home_nursery_drag_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(s_home, home_nursery_gesture_cb, LV_EVENT_GESTURE, NULL);

    /* 底部上拉通知抽屉：与顶部下拉对称，同样在 build_dial 前注册 */
    lv_obj_add_event_cb(s_home, home_notice_pull_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_home, home_notice_pull_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_home, home_notice_pull_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_home, home_notice_pull_cb, LV_EVENT_PRESS_LOST, NULL);

    build_dial(s_home);
    s_home_snap_img = lv_image_create(s_screen);
    lv_obj_remove_flag(s_home_snap_img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_home_snap_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_home_snap_img, 0, 0);
    build_center();
    build_notice();
    build_status();
    build_slider();
    build_torch();
    build_apps();
    build_wifi();
    build_wifipwd();
    build_pet();
    build_nursery();
    build_tools();

    /* 全局提示条 */
    s_toast = mk_box(s_screen, 83, 210, 300, 44, COL_TEXT, LV_OPA_COVER, 22);
    lv_obj_set_style_shadow_width(s_toast, 22, 0);
    lv_obj_set_style_shadow_color(s_toast, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(s_toast, LV_OPA_40, 0);
    s_toast_label = mk_label(s_toast, "", &epw_cjk_16, COL_INK,
                             0, 11, 300, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    /* 透明唤醒层：仅熄屏时接收触摸，吞掉点亮屏幕的第一下按压。 */
    s_wake_layer = mk_box(s_screen, 0, 0, SCREEN_W, SCREEN_H,
                          lv_color_black(), LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_wake_layer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(s_wake_layer, wake_layer_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_flag(s_wake_layer, LV_OBJ_FLAG_HIDDEN);

    /* 开机一次性预渲染控制中心抽屉快照（约 434KB PSRAM），
     * 之后每次下拉都只移动这一张图，不会在第一次拖动时临时卡一下。 */
    center_snap_ensure();
    s_home_snap = lv_snapshot_create_draw_buf(s_home, LV_COLOR_FORMAT_RGB565);

    s_last_activity = lv_tick_get();

    time_tick_cb(NULL);
    ui_update_battery(power_battery_percent(), power_is_charging(), power_is_vbus());
    brightness_apply();
    home_snap_refresh();

    lv_timer_create(time_tick_cb, 1000, NULL);
    lv_timer_create(battery_timer_cb, 3000, NULL);
    lv_timer_create(network_timer_cb, 2000, NULL);
    lv_timer_create(pet_anim_timer_cb, 80, NULL);
    lv_timer_create(pet_scene_timer_cb, 9000, NULL);
    lv_timer_create(nursery_timer_cb, 500, NULL);
    lv_timer_create(idle_timer_cb, UI_IDLE_POLL_MS, NULL);
}

/* -------------------------------------------------------------------------- */
/* 演示 / 调试接口                                                             */
/* -------------------------------------------------------------------------- */

/* 演示/调试：直接打开指定热点的密码页（不依赖真实扫描），用于真机截图检查布局 */
void ui_demo_open_wifipwd(const char *ssid, const char *pwd)
{
    epw_wifi_ap_t ap = {0};
    if (ssid != NULL) {
        strncpy(ap.ssid, ssid, sizeof(ap.ssid) - 1);
    }
    ap.secure = true;
    ap.rssi = -48;
    wifi_pwd_open(&ap);
    if (pwd != NULL && s_wifipwd_ta != NULL) {
        lv_textarea_set_text(s_wifipwd_ta, pwd);
        lv_textarea_set_cursor_pos(s_wifipwd_ta, LV_TEXTAREA_CURSOR_LAST);
    }
}

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
    ui_update_battery(power_battery_percent(), power_is_charging(), power_is_vbus());
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
    if (page_anim_busy()) {
        return;
    }
    if (s_tools_open) {
        tools_close();
    } else if (s_wifipwd_open) {
        wifi_pwd_close();
    } else if (s_wifi_open) {
        wifi_close();
    } else if (s_torch_open) {
        torch_close();
    } else if (s_slider_open) {
        slider_close();
    } else if (s_status_open) {
        status_close();
    } else if (s_pet_open) {
        pet_close();
    } else if (s_nursery_open) {
        nursery_close();
    } else if (s_center_open) {
        center_close();
    } else if (s_apps_open) {
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

void ui_demo_open_nursery(void)
{
    nursery_open();
}

void ui_demo_close_nursery(void)
{
    nursery_close();
}

/* 截图任务在 UI 线程外调用，这里自行加锁，避免触碰 LVGL 对象时崩溃 */
void ui_debug_sleep_check(bool on)
{
    if (bsp_display_lock(2000) != ESP_OK) {
        return;
    }
    sleep_mode_apply(on);
    bsp_display_unlock();
}

void ui_demo_toggle_pet(void)
{
    /* 熄屏时本函数不负责点亮屏幕，也不进入 Mimi Cat */
    if (s_screen_off) {
        return;
    }
    if (s_pet_open) {
        pet_close();
    } else {
        ui_pet_set_state(UI_PET_HAPPY, NULL);
    }
}

void ui_pet_set_state(ui_pet_state_t state, const char *text)
{
    /* 熄屏状态：不唤醒、不更新台词、不回答 */
    if (s_screen_off) {
        return;
    }
    pet_apply(state, text);
    pet_open();
}

void ui_pet_set_state_by_name(const char *emotion, const char *text)
{
    /* 熄屏状态：忽略服务端回复，不唤醒、不发声、不更新表情 */
    if (s_screen_off || !s_pet_open) {
        return;
    }

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
    if ((text == NULL || text[0] == '\0') && s_pet_bubble_label != NULL) {
        text = s_pet_text;
    }
    pet_apply(state, text);
}

static void draw_weather_cloud(lv_obj_t *parent, lv_color_t color)
{
    mk_circle(parent, 16, 25, 20, color, LV_OPA_COVER);
    mk_circle(parent, 27, 20, 25, color, LV_OPA_COVER);
    mk_circle(parent, 39, 27, 18, color, LV_OPA_COVER);
    mk_box(parent, 7, 26, 40, 10, color, LV_OPA_COVER, 5);
}

void ui_update_weather(float temperature, epw_weather_kind_t kind)
{
    if (!s_weather || !s_weather_icon) return;
    s_home_snap_dirty = true;
    if (isfinite(temperature)) lv_label_set_text_fmt(s_weather, "%.1f°C", temperature);
    else lv_label_set_text(s_weather, "--°C");
    static int previous_kind = -1;
    if ((int)kind == previous_kind) return;
    previous_kind = kind;
    lv_obj_clean(s_weather_icon);
    switch (kind) {
    case EPW_WEATHER_SUNNY:
        draw_sun_duo(s_weather_icon, 26, 24, 9, COL_AMBER, COL_RING_YELLOW);
        break;
    case EPW_WEATHER_PARTLY_CLOUDY: {
        lv_obj_t *back = mk_box(s_weather_icon, 5, -5, 52, 50, COL_BG, LV_OPA_TRANSP, 0);
        draw_weather_cloud(back, COL_MUTED);
        draw_weather_cloud(s_weather_icon, COL_TEXT);
        break;
    }
    case EPW_WEATHER_CLEARING:
        draw_sun_duo(s_weather_icon, 35, 14, 6, COL_AMBER, COL_RING_YELLOW);
        draw_weather_cloud(s_weather_icon, COL_TEXT);
        break;
    case EPW_WEATHER_CLOUDY:
    case EPW_WEATHER_RAIN:
    case EPW_WEATHER_STORM:
    case EPW_WEATHER_SNOW:
    case EPW_WEATHER_FOG:
        draw_weather_cloud(s_weather_icon, kind == EPW_WEATHER_CLOUDY ? COL_MUTED : COL_TEXT);
        if (kind == EPW_WEATHER_RAIN) {
            for (int i = 0; i < 3; i++) {
                lv_obj_t *drop = mk_box(s_weather_icon, 13 + i * 11, 39, 3, 8, COL_BLUE, LV_OPA_COVER, 2);
                lv_obj_set_style_transform_rotation(drop, 200, 0);
            }
        } else if (kind == EPW_WEATHER_STORM) {
            static const lv_point_precise_t bolt[] = {{28, 33}, {21, 41}, {28, 41}, {23, 49}};
            lv_obj_t *line = lv_line_create(s_weather_icon);
            lv_line_set_points(line, bolt, 4);
            lv_obj_set_style_line_width(line, 3, 0);
            lv_obj_set_style_line_color(line, COL_AMBER, 0);
        } else if (kind == EPW_WEATHER_SNOW) {
            for (int i = 0; i < 3; i++) {
                mk_box(s_weather_icon, 14 + i * 11, 39, 2, 8, COL_BLUE, LV_OPA_COVER, 1);
                mk_box(s_weather_icon, 11 + i * 11, 42, 8, 2, COL_BLUE, LV_OPA_COVER, 1);
            }
        } else if (kind == EPW_WEATHER_FOG) {
            mk_box(s_weather_icon, 8, 39, 34, 2, COL_MUTED, LV_OPA_COVER, 1);
            mk_box(s_weather_icon, 13, 45, 31, 2, COL_MUTED, LV_OPA_COVER, 1);
        }
        break;
    default:
        mk_label(s_weather_icon, "?", &epw_cjk_20, COL_MUTED, 0, 12, 52, LV_TEXT_ALIGN_CENTER);
        break;
    }
    clear_clickable_tree(s_weather_icon);
}
