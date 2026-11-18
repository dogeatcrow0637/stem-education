#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化板载物理按键（BOOT / PWR）：
 *   BOOT（小按钮）短按 → 宠物页
 *   PWR（大按钮）短按 → 应用桌面，长按 3 秒 → 滑动确认关机
 * 在 ui_init() 之后调用。
 */
void button_init(void);

/** 仅由滑动确认调用，按钮任务在显示锁之外停止服务并关电。 */
void button_request_shutdown(void);

#ifdef __cplusplus
}
#endif
