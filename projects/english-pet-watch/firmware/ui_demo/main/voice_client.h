#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 创建控制任务；未配置语音地址时不创建任务、不初始化音频。 */
void voice_client_init(void);

/** 用户启动/停止聊天；无可用语音配置时启动返回 false，不阻塞 UI。 */
bool voice_client_set_active(bool active);

/** 是否有用户请求的语音会话（包含等待网络的状态）。 */
bool voice_client_is_active(void);

#ifdef __cplusplus
}
#endif
