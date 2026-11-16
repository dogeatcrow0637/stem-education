#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** 抓取当前屏幕，按行输出 base64（SHOTMETA / SHOT / SHOTEND 标记）。 */
void shot_dump(const char *label);

/** 演示用：依次截图表盘 / 单词卡正面 / 单词卡释义 / 状态页，然后回到表盘。 */
void shot_capture_demo_pages(void *arg);

#ifdef __cplusplus
}
#endif
