#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化板载物理按键（BOOT / PWR）：
 *   BOOT（小按钮）短按 → 宠物页
 *   PWR（大按钮）短按 → 应用桌面
 * 在 ui_init() 之后调用。
 */
void button_init(void);

#ifdef __cplusplus
}
#endif
