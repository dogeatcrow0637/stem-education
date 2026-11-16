# enclosure · {{PROJECT_NAME}}

外壳、支架、转接件等**结构件**资料。

| 子目录 | 内容 | 格式 |
|---|---|---|
| `step/` | 可编辑源文件 | STEP、STP、F3D、SLDPRT、FreeCAD |
| `stl/` | 可直接切片打印 | STL、3MF |

命名规则：`件名_版本_数量.stl`（例：`base_plate_v1.1_x1.stl`）；左右对称件加 `_L` / `_R`。

- 打印参数与装配顺序统一写在 [`print-notes.md`](print-notes.md)。
- 金属加工件（CNC/钣金）图纸也放这里，在 `print-notes.md` 中区分「打印件 / 加工件」。
- 切片产物 `.gcode` 不入库（见仓库 `.gitignore`），参数记录在文档里即可复现。
- 大尺寸装配体参考 `../../../docs/大文件与Git-LFS.md`（相对路径以**复制到 `projects/<项目名>/` 之后**为准）。
