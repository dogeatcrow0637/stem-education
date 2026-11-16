# hardware · {{PROJECT_NAME}}

| 子目录 | 放什么 | 命名示例 |
|---|---|---|
| `schematic/` | 原理图、PCB 工程、接线图 | `schematic_v1.1.pdf`、`wiring_power_v1.1.pdf` |
| `bom/` | 采购清单 | `bom_v1.1.csv` |
| `datasheet/` | 芯片/电机/传感器手册 | `DM4310_datasheet.pdf` |

约定：

1. 接线资料必须**图 + 表**成对提供；表里写清端子号、信号名、线色、对端、备注。
2. BOM 字段固定（见 `bom/README.md`），价格注明**采集日期与币种**。
3. 大文件（整套装配 STEP、超大 PDF）参考 `../../../docs/大文件与Git-LFS.md`（相对路径以**复制到 `projects/<项目名>/` 之后**为准）。
4. 第三方图纸标注来源与许可，不要移除原作者版权信息。
