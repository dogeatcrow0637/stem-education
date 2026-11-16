# enclosure · 结构件

| 子目录 | 内容 | 说明 |
|---|---|---|
| `step/` | 可编辑源文件（Fusion 360 / STEP） | 本仓库不复制上游大文件；按 `docs/99-参考资源.md` 获取 |
| `stl/` | 可直接切片的 STL | 同上；打印前先看 [`print-notes.md`](print-notes.md) |
| `print-notes.md` | 材料、参数、支撑、装配顺序、易错点 | [打开](print-notes.md) |

## 上游结构

- 上游 `hardware/printing/`：STL、打印说明、支撑位置图。
- 上游 `hardware/cad/`：Fusion 360 源文件（如提供）。
- 上游建议顶盖使用 **Enclosed v91**（带磁吸帽、遮线、多色细节）。

## 本地策略

- 不复制上游 STL/CAD（体积大、版本易漂移）；在 `print-notes.md` 记录**文件清单 + 上游 commit**。
- 代打时把上游 STL 直接发给工厂，不要用本仓库的截图代替。
- 打印件到货验收：关键孔位、左右镜像、顶盖与框架配合、支撑残留。
- 任何结构改动（扩孔、加厚、改电池仓）在 `CHANGELOG.md` 记录，并标注需要重新打印的件。
