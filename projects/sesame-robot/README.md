# Sesame Robot 迷你四足机器人

> 一句话定位：￥400–700、8 个舵机、ESP32 + 3D 打印的迷你四足机器人；作为循迹小车之后的**进阶项目**，用 6 课时从仿真观看到自己发 API 指令。

| 字段 | 值 |
|---|---|
| 项目编号 | 05 |
| 目录 | `projects/sesame-robot` |
| 状态 | 资料整理 |
| 方向 | 机器人 / 四足机器人 / 入门教学 / 3D 打印 |
| 建立日期 | 2026-11-05 |
| 上游 | [dorianborian/sesame-robot](https://github.com/dorianborian/sesame-robot)（Apache-2.0） |
| 定位 | 不是整个合集的第一课；建议排在 `line-follower-car`、`cyberbrick` 之后 |

## 这是什么

Sesame Robot 是一台全开源的迷你四足机器人：ESP32 主控、每条腿 2 个 MG90S 舵机（共 8 个）、128×64 OLED 表情屏、WiFi Web 控制页和 RESTful JSON API，外壳全部 3D 打印。它把“多舵机协同、步态、电源管理、网络控制”这条足式机器人入门链路压到了百元级成本。

教学价值在于：孩子先看到“四条腿怎么配合走”，再亲手完成零件分类、腿部装配、舵机编号、校准和步态调参，最后用一条 HTTP 请求让机器人做动作。它比循迹小车难一个台阶，但比六轴机械臂便宜、风险低，适合作为**进阶课**而不是第一课。

## 能力与参数

| 参数 | 值 |
|---|---|
| 主控 / 平台 | Lolin S2 Mini（ESP32-S2）或 Sesame Distro Board V3.1（ESP32-S3，预烧录） |
| 执行器 | 8× MG90S 金属齿 **180°** 舵机（髋关节 + 膝关节，每条腿 2 个） |
| 显示 | 0.96" SSD1306 I2C OLED，128×64，像素表情 |
| 通信 | WiFi AP / STA 双模；Web 控制页；`GET /api/status`、`POST /api/command` |
| 供电 | 5V/3A 以上；USB-C PD 或 2×14500 7.4V 锂电池 + 5V 降压 |
| 结构 | 11 个 PLA 打印件（内部框架、上下盖、R1–R4/L1–L4 关节） |
| 整机尺寸 / 重量 | `待验证`（上游未给出统一实测值；以打印与电池方案为准） |
| 续航 | `待验证`（与电池、步态、舵机质量强相关） |
| 关键限制 | 纯开环舵机控制，无位置/速度反馈；地面打滑或负载变化不能自动纠正 |

## 当前进度

- [x] 资料收集与整理（上游 commit `f328ed3`，2026-10-15）
- [x] 硬件选型确认与国内 BOM 初稿
- [x] 六维工程文档与家长实现手册
- [x] 6 课时儿童课程、课件 HTML 与知识卡
- [ ] 实物采购与试装（家庭自行进行）
- [ ] 3D 打印件试装与公差回填
- [ ] 实测续航、整机重量、步态参数回填

## 快速开始

```bash
# 1. 看选型与采购：docs/02-选型与BOM.md + hardware/bom/bom_sesame_v1.0.csv
# 2. 打印 11 个结构件：enclosure/print-notes.md
# 3. 照接线图做线束：hardware/schematic/wiring_s2mini_v1.0.md
# 4. 烧录或使用预烧录固件：firmware/README.md
# 5. 上课：docs/儿童课程/README.md
python3 .trae/skills/stem-project-onboarding/scripts/build_slides.py projects/sesame-robot/docs/儿童课程/*/课件.md --strict
```

> 如果家里没有 3D 打印机和电烙铁：按 `docs/家长实现/05-安全与工具采购清单.md` 的“外包路径”走——打印代做 + PCB 贴片/手接线代焊，家长只负责装配、校准和陪玩。

## 目录导航

| 目录 | 内容 | 入口 |
|---|---|---|
| `docs/` | 六维工程文档 + 家长实现 + 儿童课程 | [docs/](docs/) |
| `docs/家长实现/` | 零基础家长带娃路线、任务卡、安全采购 | [00-总纲](docs/家长实现/00-总纲与路线图.md) |
| `docs/儿童课程/` | 6 课时，一课一个自包含目录 | [课程总览](docs/儿童课程/README.md) |
| `hardware/` | 国内 BOM、接线图与端子表、数据手册入口 | [hardware/README.md](hardware/README.md) |
| `enclosure/` | 打印件清单、参数、装配顺序 | [enclosure/print-notes.md](enclosure/print-notes.md) |
| `firmware/` | 上游固件结构、烧录参数、API 速查 | [firmware/README.md](firmware/README.md) |
| `software/` | Web 控制页、Sesame Studio、仿真器 | [software/README.md](software/README.md) |
| `code/` | Python API 示例与步态实验 | [code/README.md](code/README.md) |
| `resources/` | 图片、视频、外部资料外链 | [resources/README.md](resources/README.md) |

## 已知问题与踩坑

| 现象 | 原因 | 规避方式 |
|---|---|---|
| 一插舵机就重启 | 8 个舵机同时启动，5V 轨瞬间跌压 | 用 5V/3A 以上电源；保持固件 `motorCurrentDelay` 默认 20ms；电池方案加足够电容 |
| 舵机转不到 180° | 买成了 90° 或 270° 版本 | 必须买 **180° MG90S**，优先金属齿；到货先用舵机测试架确认 |
| 手机连上热点就掉电 | 廉价 ESP32-S2 克隆板 AP 模式射频功耗突增 | 换正规 Lolin/WEMOS 板；或用 ESP-IDF 降低 WiFi 发射功率（通信距离会变短） |
| 走路像“抽筋” | 舵机中位没校准、关节装偏、左右腿镜像装反 | 先跑电机测试固件，用 `REST`/`STAND` 对齐 45°，再锁螺丝 |
| OLED 不亮 | SDA/SCL 接反、I2C 地址不对、供电不足 | 对照接线表核对 GPIO33/35（S2 Mini）；先只接 OLED 上电 |
| 网页控制页打不开 | 没连 `Sesame-Controller-BETA`，或手机走了流量 | 关闭移动数据；浏览器访问 `192.168.4.1` |
| 电池供电一动就崩 | V2 板电池降压不稳；电池放电能力不足 | 用 V3.1 或 USB-C PD；不要用普通 9V 方块电池 |
| 3D 打印件装不上 | 公差、支撑残留、镜像件装错 | 清支撑；M2 自攻螺丝不要过拧；先干装再锁死 |

## 许可与致谢

- 上游硬件与软件：**Apache-2.0**，作者 Dorian Todd（GitHub [@dorianborian](https://github.com/dorianborian)）。
- 本项目内的中文文档、BOM 整理、课程与示意图：可自由用于教学，引用上游图片/代码时保留原作者署名与许可。
- 上游项目与社区资源：见 [docs/99-参考资源.md](docs/99-参考资源.md)。

## 参考资料

见 [docs/99-参考资源.md](docs/99-参考资源.md)。
