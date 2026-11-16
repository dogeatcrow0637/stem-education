# software · reBot-DevArm

| 子目录 | 内容 | 状态 |
|---|---|---|
| [`host/`](host/) | 上位机：关节使能、状态监控、示教/回放 | 待开发 |
| [`sdk/`](sdk/) | 基于官方 Python SDK 的封装（连接 / 读状态 / 发指令） | 待开发 |
| [`tools/`](tools/) | CAN 自检、零位标定、udev 规则、环境一键脚本 | 待开发 |

## 技术栈基线

| 项 | 选择 | 备注 |
|---|---|---|
| 操作系统 | **Ubuntu 物理机** | ⚠️ 官方明确不建议虚拟机（性能不足 + 配置问题） |
| 语言 | Python 为主 | 与官方 SDK、LeRobot 生态一致 |
| 通信 | USB-CAN → CAN 总线；SocketCAN | 建议固定 udev 规则 |
| 上位机 GUI | 待定（PySide6 / Web） | 教学场景优先易部署 |
| 机器人框架 | ROS1/2（含 MoveIt）、Pinocchio、Isaac Sim | 按课程深度选用 |
| 学习框架 | LeRobot（+ GR00T N1.7） | 见 `../docs/04-软件环境.md` |

## 开发顺序建议

1. `tools/` 先做起来：`check-env.sh`（找到 CAN 设备）+ `can-scan.py`（电机在线探测）
2. `sdk/` 封装「连接 → 使能 → 读关节状态 → 发一条关节指令」的最小闭环
3. `host/` 在 SDK 之上做界面：状态可视化、单关节点动、示教回放
4. 再往上接 LeRobot 采数与策略部署

> ⚠️ 机械臂**无刹车**：软件里不要提供「断电/断使能即安全」的默认行为，界面需要有急停与支撑提示。
