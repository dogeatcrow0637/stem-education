#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 宠物情绪（与豆包桥接协议的 emotion 字段一一对应）。 */
typedef enum {
    UI_PET_IDLE = 0,
    UI_PET_HAPPY,
    UI_PET_EXCITED,
    UI_PET_THINKING,
    UI_PET_SPEAKING,
    UI_PET_SLEEPY,
    UI_PET_SAD,
    UI_PET_LOVE,
    UI_PET_LISTENING,
    UI_PET_STATE_COUNT,
} ui_pet_state_t;

/** 创建表盘、应用桌面、宠物页、控制中心与状态页。调用前需持有 LVGL 锁。 */
void ui_init(void);

/**
 * 刷新电池显示。percent < 0 表示未知；
 * usb_powered 为 true 时进入充电模式（外圈点亮为电量环 + 彗星）。
 */
void ui_update_battery(int percent, bool usb_powered);

/** 兼容接口：按层叠关系切换（状态页 > 控制中心 > 表盘）。调用前需持有 LVGL 锁。 */
void ui_demo_toggle_menu(void);

/** 演示/调试用：从上方滑出控制中心。调用前需持有 LVGL 锁。 */
void ui_demo_open_center(void);

/** 演示/调试用：收起控制中心。调用前需持有 LVGL 锁。 */
void ui_demo_close_center(void);

/** 演示/调试用：从右侧滑入状态页。调用前需持有 LVGL 锁。 */
void ui_demo_open_status(void);

/** 演示/调试用：关闭状态页（滑回右侧）。调用前需持有 LVGL 锁。 */
void ui_demo_close_status(void);

/** 演示/调试用：打开亮度调节页（上下拖动）。调用前需持有 LVGL 锁。 */
void ui_demo_open_bright(void);

/** 演示/调试用：关闭亮度调节页。调用前需持有 LVGL 锁。 */
void ui_demo_close_bright(void);

/** 演示/调试用：打开音量调节页（上下拖动）。调用前需持有 LVGL 锁。 */
void ui_demo_open_volume(void);

/** 演示/调试用：关闭音量调节页。调用前需持有 LVGL 锁。 */
void ui_demo_close_volume(void);

/** 演示/调试用：打开手电（白屏满亮度）。调用前需持有 LVGL 锁。 */
void ui_demo_open_torch(void);

/** 演示/调试用：关闭手电并恢复亮度。调用前需持有 LVGL 锁。 */
void ui_demo_close_torch(void);

/**
 * 演示/调试用：强制电源模式做对比截图。
 * mode: 0 = 普通（拔线），1 = 充电（插线），其它 = 跟随 AXP2101。
 */
void ui_demo_power_mode(int mode);

/** 物理大按钮 / 演示用：打开应用桌面（Mac Launchpad 风格圆形图标）。 */
void ui_demo_open_apps(void);

/** 关闭应用桌面。 */
void ui_demo_close_apps(void);

/** 大按钮切换应用桌面。 */
void ui_demo_toggle_apps(void);

/** 小按钮 / 演示用：打开宠物页。 */
void ui_demo_open_pet(void);

/** 关闭宠物页。 */
void ui_demo_close_pet(void);

/** 小按钮切换宠物页。 */
void ui_demo_toggle_pet(void);

/**
 * 设置宠物状态并显示一句台词（text 为 NULL 时用默认台词）。
 * 若宠物页未打开会先打开；调用前需持有 LVGL 锁。
 */
void ui_pet_set_state(ui_pet_state_t state, const char *text);

/**
 * 豆包桥接入口：emotion 取 idle/happy/excited/thinking/speaking/sleepy/sad/love/listening。
 * 只更新宠物状态和台词，不主动打开宠物页；未知值按 idle 处理，text 可为 NULL。
 * 调用前需持有 LVGL 锁。
 */
void ui_pet_set_state_by_name(const char *emotion, const char *text);

#ifdef __cplusplus
}
#endif
