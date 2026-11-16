# sdk · 二次开发接口

> 适用版本：v0.1　|　最后更新：2026-11-13
> 当前**没有自研 SDK**。设备与云端之间用 xiaozhi 协议；扩展能力优先用 MCP，不要fork 一套私有协议。

## 1. 现有接口

| 接口 | 用途 | 文档 |
|---|---|---|
| WebSocket | 设备 ↔ 服务端音频/事件 | https://github.com/78/xiaozhi-esp32/blob/main/docs/websocket_zh.md |
| MQTT + UDP | 低功耗/弱网传输 | https://github.com/78/xiaozhi-esp32/blob/main/docs/mqtt-udp_zh.md |
| OTA | 设备激活、配置、固件升级 | https://github.com/78/xiaozhi-esp32/blob/main/docs/ota_zh.md |
| MCP | 设备端/云端工具调用 | https://github.com/78/xiaozhi-esp32/blob/main/docs/mcp-usage_zh.md |

## 2. 未来可加的 SDK（按需，不预先造）

| 场景 | 建议 |
|---|---|
| 家长查看单词本 | 调服务端 API + 一个只读网页，不做独立 App |
| 课堂批量管理 | 用智控台多智能体，不写新 SDK |
| 自定义教具 | 走 MCP 工具，工具描述写清“什么时候调用” |

## 3. 约定

- 不把 API Key 写进客户端；客户端只连自家服务端。
- 协议变更先改文档与兼容矩阵，再改代码。
- 示例代码必须能直接跑；跑不通的标 `待验证`。
