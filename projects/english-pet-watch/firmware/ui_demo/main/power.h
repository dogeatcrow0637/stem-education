#pragma once

#include <stdbool.h>
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

/** 使能并清空 PWR 键短按 IRQ（AXP2101 REG41/REG49）。 */
void power_pwrkey_irq_init(void);

/** PWR 键短按事件：有则清除标志并返回 true。 */
bool power_pwrkey_short_pressed(void);

#ifdef __cplusplus
}
#endif
