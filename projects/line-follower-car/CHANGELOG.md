# 变更记录 · 循迹小车

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号用 `主版本.次版本.修订号`。
硬件改动请在「影响」里写清**需要重新打印/重新采购**的具体文件。

## [未发布]

### 新增
- 工程文档 `docs/01-项目简介.md` ~ `05-教学与课程.md`、`99-参考资源.md`
- 采购清单 `hardware/bom/bom_lfc_v1.0.csv`（MVP ￥65–100，含扩展件）
- 接线表 15 条与通电自检流程（`docs/03-组装与调试.md`）
- 固件示例 `firmware/line_follower/line_follower.ino`（UNO + L298N + 2 路循迹，逐行中文注释）
- 家长手册 6 份（`docs/家长实现/`）
- 课件 4 课时（`docs/儿童课程/*/课件.md --strict` → 可放映 HTML，含 8 张 SVG 配图）
- 知识卡 7 张（随课时分发到 `docs/儿童课程/第NN课-课题/知识卡/`）
- 结构件说明（可选打印，含嘉立创 3D 打印下单参数）

### 变更
- 项目 `README.md` 补全参数、导航、快速开始与已知问题
- `software/`、`code/`、`hardware/schematic` 等子目录 README 精简为本项目实际情况（去掉与本项目无关的模板内容）
- 按 skill 约定取消「备课文案」产出：删除 `docs/备课文案/`，逐字话术并入课件讲者备注与 `docs/家长实现/03-家长话术与提问卡.md`

### 修复
- 脚手架脚本从 `project.yaml` 读 `name`/`id` 时未去掉行内注释，导致生成文件名带注释（已修复 `scaffold_family_guide.sh`、`scaffold_teaching_materials.sh`）

### 影响
- 无需重新采购或重新打印；全部为文档与示例代码新增
- `待验证`：循迹阈值、续航、最高稳定速度，需实物实测后回填 `docs/01-项目简介.md`
