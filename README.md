# STEM Education Projects 
todo: HomeAssistant HomeAssistant

面向 STEM / 创客教育的**项目合集仓库**。这里按“一个项目一个目录”的方式持续收录可复刻、可教学、可二次开发的软硬件项目。

每个项目都尽量自成闭环，覆盖从**硬件 → 外壳（3D 打印）→ 固件 → 上位机/开发机软件 → 算法代码 → 教学文档**的完整链路。

---

## 目录总览

```
stem-education/
├── README.md               # 本文件：仓库总览与使用说明
├── docs/                   # 跨项目通用规范（目录约定、新增项目、大文件管理）
├── projects/               # 所有项目，一个项目一个目录
│   ├── README.md           # 项目索引（新增项目后记得登记）
│   ├── rebot-devarm/       # 项目 01：reBot-DevArm 开源六轴机械臂
│   ├── line-follower-car/  # 项目 02：循迹小车（￥100 入门练手）
│   ├── cyberbrick/         # 项目 03：拓竹 CyberBrick 模块化遥控套件
│   ├── nano-reef-40/       # 项目 04：40cm 背滤海缸（小丑鱼练手版，首缸约 ￥1100）
│   ├── sesame-robot/       # 项目 05：Sesame Robot 迷你四足机器人（￥400–700 进阶）
│   └── english-pet-watch/  # 项目 06：英语宠物怀表（ESP32-S3 + 1.75" AMOLED + 云端大模型）
├── templates/
│   └── project-template/   # 新项目骨架模板
└── scripts/
    └── new-project.sh      # 一键从模板创建新项目
```

## 单个项目的标准结构

```
<项目名>/
├── README.md        # 项目门面：是什么 / 目标 / 当前进度 / 快速开始 / 资料入口
├── project.yaml     # 结构化元数据（编号、状态、标签、负责方向），供索引与脚本读取
├── CHANGELOG.md     # 项目级变更与进展记录
├── docs/            # 教学与工程文档：简介、选型与 BOM、组装调试、踩坑、参考资料
├── hardware/        # 硬件：原理图/PCB、BOM、接线图、数据手册
│   ├── bom/         #   采购清单（含规格、数量、参考价、采购链接）
│   ├── schematic/   #   原理图 / PCB / 接线图
│   └── datasheet/   #   芯片、电机、传感器数据手册
├── enclosure/       # 外壳与结构件（3D 打印）
│   ├── step/        #   可编辑源文件（STEP/STP/F3D/SLDPRT）
│   ├── stl/         #   可直接切片打印的网格文件（STL/3MF）
│   └── print-notes.md  # 材料、层高、填充、支撑、后处理、装配注意
├── firmware/        # 固件代码（MCU / 主控板 / 驱动板）
├── software/        # 运行在 PC 或开发机上的软件
│   ├── host/        #   上位机（GUI 控制台、监控面板）
│   ├── sdk/         #   对外二次开发 SDK / Python 包
│   └── tools/       #   烧录、标定、仿真、一键环境脚本等工具
├── code/            # 算法与实验代码（视觉、轨迹规划、VLA、数据处理）
└── resources/       # 图片、视频、演示素材、外部资料存档（大文件见下方约定）
```

> 该结构是**约定而非强制**：某个项目不涉及固件时，保留 `firmware/README.md` 写明“本项目无固件”即可，目录骨架保持一致便于检索。

## 新增一个项目

```bash
# 方式一：脚本（推荐）
./scripts/new-project.sh <项目目录名> "<项目中文名>"

# 方式二：手动复制模板
cp -r templates/project-template projects/<项目目录名>
```

创建后需要做四件事：

1. 填写 `projects/<项目名>/README.md` 与 `project.yaml`（状态、标签、负责人）；
2. 在 `projects/README.md` 的索引表里登记一行；
3. 在 `docs/新增项目指南.md` 的约定下补齐各子目录说明；
4. 用项目内 skill `.trae/skills/stem-project-onboarding` 做六维拆解，生成「家长实现」手册与按课时分目录的「儿童课程」（课件 + 配图 + 知识卡），见 `docs/新增项目指南.md` 第六节。

## 通用约定

| 约定 | 说明 |
|---|---|
| 命名 | 目录用小写短横线（`rebot-devarm`）；文档用中文名 + 数字前缀排序（`01-项目简介.md`） |
| 项目编号 | 项目在索引表中的顺序号，两位数字（`01`、`02`…），在 `project.yaml` 中登记 |
| 语言 | 面向教学，文档以中文为主；代码注释中文，标识符英文 |
| 大文件 | 单一文件 > 50 MB 的 STEP/STL/视频走 Git LFS 或网盘外链，详见 `docs/大文件与Git-LFS.md` |
| 外部资料 | 引用的官方仓库、教程、视频统一登记在项目 `docs/99-参考资源.md`，并在 `resources/` 备份关键内容 |
| 许可 | 复刻类项目的硬件/软件许可以上游为准，登记在 `project.yaml` 的 `license` 字段，商业使用前逐一核验 |

## 项目索引

见 [`projects/README.md`](projects/README.md)。

## 说明

本仓库内所有第三方项目资料均整理自公开网络，**版权与许可归原作者所有**，仅用于学习与教学交流；资源开放状态、链接有效性以官方公告为准。
