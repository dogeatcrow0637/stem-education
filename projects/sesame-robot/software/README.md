# software · Sesame Robot

运行在电脑/手机上的软件。本项目**不需要自己写上位机**：机器人固件自带网页控制页，另有官方/社区工具。

| 子目录 | 内容 | 本项目状态 |
|---|---|---|
| [`host/`](host/) | Web 控制页（固件内置）与使用说明 | 可用，无需安装 |
| [`sdk/`](sdk/) | Python API 封装与示例 | 提供最小示例 |
| [`tools/`](tools/) | 环境检查、烧录参数速查 | 提供脚本/文档 |

## 推荐工作流

1. 手机/电脑连机器人热点 `Sesame-Controller-BETA`。
2. 浏览器打开 `http://192.168.4.1/`，用网页按钮测试站立/走路/表情。
3. 需要自动化时，用 `sdk/` 的 Python 示例或 `curl` 调 `/api/command`。
4. 想做动作序列时，用 [Sesame Studio](https://github.com/dorianborian/sesame-studio)。
5. 没实物时，用 [Sesame Simulator](https://one-for-all.github.io/sesame-robot-sim/)。

## 约定

1. 依赖版本锁定；Python 示例只用标准库 `urllib` 或 `requests`（需安装时写明）。
2. 不提交账号、内网 IP、WiFi 密码；示例里用 `192.168.4.1` 或 `sesame-robot.local`。
3. 设备通信协议以 `firmware/README.md` 的 API 表为准。
4. 虚拟机/容器可能看不到 USB 开发板；烧录建议物理机。

## 已验证环境

| 系统 | 状态 |
|---|---|
| macOS 26.x | 网页/HTTP API 可用（本仓库整理时验证网络请求格式） |
| Windows 10/11 | `待验证` |
| Linux | `待验证`（串口权限见 `docs/04-软件环境.md`） |
