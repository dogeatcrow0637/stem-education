# reBot-DevArm 开源六轴机械臂

> Seeed Studio 推出的 **6+1 自由度桌面级开源机械臂**：硬件蓝图、BOM、结构源文件、算法栈全部公开，可作为具身智能 / VLA 算法的真机验证平台。

| 字段 | 值 |
|---|---|
| 项目编号 | 01 |
| 目录 | `projects/rebot-devarm` |
| 状态 | 资料整理（尚未开始实物复刻） |
| 方向 | 机器人 / 机械臂 / 具身智能 / VLA |
| 上游 | [Seeed-Projects/reBot-DevArm](https://github.com/Seeed-Projects/reBot-DevArm) |
| 硬件许可 | CERN-OHL-W-2.0（2026-05-11 起，此前为 CC BY-SA-NC） |
| 软件许可 | Apache-2.0 |
| 参考成本 | 整套散件约 ￥6000；官方套件早鸟价 $1499（不含电源适配器与 C 型夹） |

## 这是什么

一台可以**从零复刻**的六轴机械臂（第七个电机驱动夹爪）。它的价值不在“又一个开源机械臂”，而在于：

1. **真·开源**：`hardware/` 目录约 275 个文件（含 93 个 STEP、10 个 STP），BOM 精细到每颗螺丝的规格与采购链接；`reBot_Arm_description/` 另提供自包含的 URDF/STL 包（RS、DM 两版）。
2. **生态成熟**：原生接入 NVIDIA Isaac Teleop + GR00T N1.7（30 亿参数、Apache 2.0 开源 VLA 基础模型）+ Hugging Face LeRobot，是这代 Sim-to-Real 范式的首批物理硬件平台之一；同时适配 ROS1/2（含 MoveIt）、Pinocchio、Isaac Sim 与 Python SDK。
3. **教学闭环完整**：VR 遥操作采数 → 仿真训练 → 真机部署，全链路有官方文档与社区教程支撑。

## 能力与参数

| 参数 | B601-DM（达妙电机） | B601-RS（RobStride 电机） |
|---|---|---|
| 自由度 | 6+1 | 6+1 |
| 最大臂展 | 767 mm | 754 mm |
| 推荐负载 | 1.5 kg | 2.5 kg |
| 重复定位精度 | < 0.2 mm（社区建议以实测为准） | < 0.2 mm |
| 供电 | DC 24V | DC 48V |
| 自重 | 约 4.5 kg | 约 6.7 kg |
| 电机配置 | 4× DM4310(V4) + 3× DM4340P(V4) | 4× RobStride RS00 + 3× RS06 |

> ⚠️ **该机械臂没有刹车**，断电后关节会自由下落，**不要用于搬运不能掉落的物品**，首次调试必须做好支撑与限位。

## 当前进度

- [x] 资料收集（官方仓库、Wiki、社区复刻帖）
- [ ] 选型定稿（DM vs RS，成本/负载/供电权衡）
- [ ] BOM 明细整理与采购
- [ ] 结构件打印/加工（含金属件 30+）
- [ ] 装配、标定、通电自检
- [ ] 上位机 + SDK 打通（CAN 通信、关节使能、示教）
- [ ] LeRobot / GR00T 链路验证
- [x] 教案与课程整理（家长实现手册 6 份；儿童课程 7 课、13 张知识卡随课时分发，第 1 课课件已出，见 `docs/儿童课程/README.md`）

## 快速开始（规划路径）

1. **选版本**：看 [`docs/02-选型与BOM.md`](docs/02-选型与BOM.md)
   - 想省钱、24V 供电（教学工位友好）→ B601-DM
   - 要更大负载、48V 供电 → B601-RS
2. **采购**：按 `hardware/bom/` 清单；官方套件可整包买（Arm Body Motor Kit / Structural Kit / Gripper Kit / Full Kit / 成品）
3. **打印与加工**：见 `enclosure/print-notes.md`（打印件 + 金属件分开处理）
4. **装配**：⚠️ 螺丝规格相近件多，务必用电批并把扭力调到 **3–6 kgf·cm**，滑丝容易导致部件报废
5. **软件**：**用 Ubuntu 物理机**，官方明确不建议虚拟机（性能不足且有配置问题）
6. **跑通验证**：先跑官方 LeRobot / Python SDK 示例，再做 VR 遥操作采数

## 目录导航

| 目录 | 内容 | 入口 |
|---|---|---|
| `docs/` | 选型、复刻注意事项、VLA 链路、资源链接 | [docs/](docs/) |
| `hardware/` | BOM、STEP/STP 归档说明、接线、手册 | [hardware/README.md](hardware/README.md) |
| `enclosure/` | 结构件与打印/加工说明 | [enclosure/README.md](enclosure/README.md) |
| `firmware/` | 电机通信与主控固件（待补） | [firmware/README.md](firmware/README.md) |
| `software/` | 上位机、Python SDK、标定与工具 | [software/README.md](software/README.md) |
| `code/` | 视觉抓取、轨迹、VLA 微调实验 | [code/README.md](code/README.md) |
| `resources/` | 装配照片、截图、视频外链 | [resources/README.md](resources/README.md) |

## 已知问题与踩坑

| 现象 / 风险 | 说明 | 规避方式 |
|---|---|---|
| 官方开源文件与官方散件有差异 | 官方声明公布 BOM 非出货最终版（金属镭雕、3D 打印件替换、线束定制均有优化） | 建议等官方软件与周边开源齐全后再完整复刻；采购前逐项比对 |
| 没有刹车 | 断电关节自由下落 | 调试加支撑；不搬运易碎/不可掉落物品 |
| 重复定位精度 0.2 mm | 社区对该指标持保留态度 | 以实测为准，教学场合避免宣传为精密加工级 |
| 打印机与金属件成本被低估 | 官方给的打印件 ~￥350、金属件 ~￥1500 只是**局部制造成本** | 电机、线束、电源、轴承、工时另计；电源适配器与 C 型夹套件不含，需另购 `24V 4.5A XT30` |
| 不可用虚拟机 | 官方 Wiki 明确提示虚拟机性能不足、配置有问题 | 必须用 Ubuntu 物理机 |
| 许可变更 | 2026-05-11 由 CC BY-SA-NC 切换为 CERN-OHL-W-2.0 | 商业分发前逐项核验条款（见 `project.yaml`） |

## 许可与致谢

- 硬件：CERN-OHL-W-2.0；软件：Apache-2.0（以上游仓库声明为准）。
- 上游项目：[Seeed-Projects/reBot-DevArm](https://github.com/Seeed-Projects/reBot-DevArm)（国内镜像 [Gitee](https://gitee.com/seeed-projects/reBot-DevArm)）
- 本目录内容整理自公开网络，版权归原作者，仅用于学习与教学交流。

## 参考资料

见 [`docs/99-参考资源.md`](docs/99-参考资源.md)（含官方仓库、Wiki 教程、GR00T 微调指南、视觉抓取 Demo 仓库、社区视频）。
