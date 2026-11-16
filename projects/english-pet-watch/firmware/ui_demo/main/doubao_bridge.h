#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 启动豆包宠物状态桥。
 *
 * 设备定时 GET `CONFIG_EPW_PET_BRIDGE_URL`，期望返回：
 *   {"emotion":"happy","text":"Hello！跟我学英语吧！"}
 * emotion 取 idle/happy/excited/thinking/speaking/sleepy/sad/love/listening。
 *
 * 真实豆包调用（火山方舟 / xiaozhi-server）放在服务端，设备端只消费这个
 * 轻量 JSON；未配置 URL 时函数直接返回，不创建任务。
 */
void doubao_bridge_init(void);

#ifdef __cplusplus
}
#endif
