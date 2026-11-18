#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 AXP2101（复用 BSP 已建好的 I2C 总线）。 */
esp_err_t power_init(void);

/** 电池电量百分比 0–100；无电池或读取失败返回 -1。 */
int power_battery_percent(void);

/** 电池电压（mV）；读取失败返回 0。 */
int power_battery_mv(void);

/** 是否正在充电。 */
bool power_is_charging(void);

/** 是否插着 USB / 有 VBUS。 */
bool power_is_vbus(void);

enum {
    POWER_KEY_RELEASE = 0x01,
    POWER_KEY_PRESS = 0x02,
    POWER_KEY_LONG = 0x04,
    POWER_KEY_SHORT = 0x08,
};

/** 读取并清除本次按键事件；读取失败不伪造松手或持续按下。 */
esp_err_t power_key_read(uint8_t *events);
/** 配置软件确认关机、128ms 硬件唤醒与按键边沿 IRQ。 */
esp_err_t power_key_configure(void);
/** 冷启动由电源键触发时，黑屏等待本次连续长按满约 3 秒。 */
esp_err_t power_boot_wait(void);
/** AXP2101 关闭电源输出；不擦除 NVS。 */
esp_err_t power_shutdown(void);

#ifdef __cplusplus
}
#endif
