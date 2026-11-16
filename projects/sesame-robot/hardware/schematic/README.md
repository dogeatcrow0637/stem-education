# schematic · 接线与 PCB

| 文件 | 内容 |
|---|---|
| [`wiring_s2mini_v1.0.svg`](wiring_s2mini_v1.0.svg) | S2 Mini 手接线示意图（信号 + 电源 + OLED） |
| [`wiring_s2mini_v1.0.md`](wiring_s2mini_v1.0.md) | 端子表、线色、V3.1 差异、上电检查 |
| 上游 PCB | https://github.com/dorianborian/sesame-robot/tree/main/hardware/pcb |

## 说明

- 本仓库**不复制**上游 Gerber/BOM/Pick-and-Place，避免与上游版本漂移；按 `docs/99-参考资源.md` 的 commit `f328ed3` 获取。
- V3.1 是当前版（ESP32-S3），V2/V1 为 legacy；V2 电池供电不稳，只能 USB。
- 接线图是**教学示意**，实际走线以打印件和你的线材长度为准；端子表是唯一编号依据。
- 图与表不一致时，**以固件 `servoPins` 数组和端子表为准**，并在 `CHANGELOG.md` 记录修正。

## 命名

- `wiring_<方案>_<版本>.svg` / `.md`
- `schematic_<版本>.pdf`（上游文件或本地重绘）
