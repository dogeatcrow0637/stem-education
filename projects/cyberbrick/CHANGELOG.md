# 变更记录 · 拓竹 CyberBrick

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号用 `主版本.次版本.修订号`。
硬件改动请在「影响」里写清**需要重新打印/重新采购**的具体文件。

## [0.1.0] - 2026-10-20

### 新增
- 建立项目骨架（docs / hardware / enclosure / firmware / software / code / resources）
- `project.yaml`：编号 03、方向（遥控模型 / 3D 打印 / 入门教学）、平台（XA003 核心板 ESP32-C3 + CyberBrick App）
- `docs/01-项目简介.md`：定位、能力参数、教学价值、适龄段与安全边界
- `docs/02-选型与BOM.md`：ZK003/004/006/007 四档套件对比、MVP 采购清单、替代与增购项
- `docs/03-组装与调试.md`：装配顺序、通电自检、灯语对照表、故障排查表
- `docs/04-软件环境.md`：CyberBrick App 配置链路 + MicroPython 开发环境（Thonny / VS Code）
- `docs/05-教学与课程.md`：6 课时安排（前 4 课时为 MVP）、考核点、教学材料索引
- `docs/99-参考资源.md`：Wiki / 商店 / MakerWorld / 官方代码库索引（含访问日期）
- `docs/家长实现/`：六个文件（实现导向，MVP = 入门套件 ZK004 + 一个 MakerWorld 免费车模）
- `docs/儿童课程/`：MVP 第 1–4 课按课时分目录（`第01课-认识我们的套件`／`第02课-让打印机吐出我的零件`／`第03课-第一次通电`／`第04课-我推一下它就跑`，各含 `课件.md` + 同名可放映 HTML + `图/` + `知识卡/`）+ 8 张矢量配图
- `docs/儿童课程/`：12 张可打印知识卡（核心板 / 免焊模块 / 2.4G / 蓝牙 / 电池 / 有刷电机 / 舵机 / PWM / 差速器 / 3D 打印 / WS2812 / MicroPython），覆盖六维中出现的全部术语
- `hardware/bom/bom_cyberbrick_zk004.csv`：入门套件 BOM 与增购项
- `enclosure/print-notes.md`：从 MakerWorld 下载模型后的打印参数与选材建议
- 子目录 README：`hardware/`（含 `bom/`、`schematic/`、`datasheet/`）、`enclosure/`、`firmware/`、`software/`（含 `host/`、`sdk/`、`tools/`）、`code/`、`resources/`

### 说明
- 本项目为**资料整理**阶段，尚未实物采购；所有参数标来源，未实测项标 `待验证`
- 课件第 5、6 课（加餐线）待补；待办（谁补、补到哪）见 `docs/05-教学与课程.md` 第六节
- 索引已登记 `projects/README.md`（编号 03）
