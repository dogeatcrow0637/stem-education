# hardware · reBot-DevArm

> 状态：待归档。官方硬件源文件在 [上游仓库 `hardware/`](https://github.com/Seeed-Projects/reBot-DevArm) 目录（约 275 个文件，含 93 STEP + 10 STP）。

## 目录内容

| 子目录 | 本项目放什么 | 当前状态 |
|---|---|---|
| `bom/` | 采购清单（`bom_b601-dm_v1.1-draft.csv` 草稿，需与官方 BOM 逐项核对） | 草稿已建 |
| `schematic/` | 接线图、端子表（**官方以结构件为主，本目录用于自研控制盒/转接板**） | 待补 |
| `datasheet/` | DM4310 / DM4340P / RS00 / RS06 电机手册、CAN 协议说明 | 待补 |

## BOM 来源（不要凭记忆下单）

| 版本 | 官方 BOM 位置 |
|---|---|
| B601-DM | `hardware/reBot_B601_DM/readme_zh.md` |
| B601-RS | `hardware/reBot_B601_RS/README.md` |

官方 BOM 精细到每颗螺丝的规格与采购链接，并给出打印件与金属件的**参考均价**。

## 关键件速查

| 版本 | 电机配置 | 供电 |
|---|---|---|
| B601-DM | 4× DM4310(V4) + 3× DM4340P(V4) | DC 24V |
| B601-RS | 4× RobStride RS00 + 3× RS06 | DC 48V |

> 第七颗电机用于驱动夹爪。B601-DM **v1.1** 起第一关节型号由 4310 修正为 **4340P**，末端关节新增线束约束件、底座新增加强件。

## 归档约定

1. 大文件（整套装配 STEP、大 PDF）走 LFS 或外链，见 [`../../../docs/大文件与Git-LFS.md`](../../../docs/大文件与Git-LFS.md)。
2. 接线类资料必须**图 + 表**成对给出（端子号 / 信号名 / 线色 / 对端）。
3. 官方文件保留原始目录结构与版权信息，本地改动另开 `local-mod/` 并在 README 说明改了什么、为什么。
