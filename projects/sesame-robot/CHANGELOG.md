# 变更记录 · Sesame Robot 迷你四足机器人

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号用 `主版本.次版本.修订号`。
硬件改动请在「影响」里写清**需要重新打印/重新采购**的具体文件。

## [0.1.0] - 2026-11-05

### 新增
- 项目建档（05 号），登记到 `projects/README.md`。
- 六维工程文档：`01-项目简介` ~ `05-教学与课程`、`99-参考资源`。
- 国内采购 BOM `hardware/bom/bom_sesame_v1.0.csv`（S2 Mini 手接线 MVP 口径）。
- 接线图与端子表 `hardware/schematic/wiring_s2mini_v1.0.md`。
- 打印件清单与参数 `enclosure/print-notes.md`。
- 家长实现手册 6 份（总纲、六维总表、里程碑任务卡、话术、验收、安全采购）。
- 儿童课程 6 课时：每课 `课件.md` + 可放映 `课件.html` + `图/` + `知识卡/` + 课时 README；共 65 页课件、39 张示意图（含 2 张跨课复用）、17 张知识卡。
- 课件已用 `build_slides.py --strict` 构建通过（每页有图位与讲者备注，无告警）。
- Python API 示例 `code/api-examples/`。

### 变更
- 定位为进阶项目：排在 `line-follower-car`、`cyberbrick` 之后，不作为整个合集第一课。

### 修复
- 无。

### 影响
- 首次建档，无历史版本需要迁移；所有实物件均为首次采购/打印。
