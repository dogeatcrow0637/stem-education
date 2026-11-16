# hardware · Sesame Robot 迷你四足机器人

| 子目录 | 放什么 | 入口 |
|---|---|---|
| `bom/` | 国内采购清单（CSV + 说明） | [bom/README.md](bom/README.md) ／ [bom_sesame_v1.0.csv](bom/bom_sesame_v1.0.csv) |
| `schematic/` | 接线图、端子表、上游 PCB 入口 | [schematic/README.md](schematic/README.md) ／ [wiring_s2mini_v1.0.md](schematic/wiring_s2mini_v1.0.md) |
| `datasheet/` | 关键器件参数摘录与手册外链 | [datasheet/README.md](datasheet/README.md) |

## 电子方案一句话

- **主控**：Lolin S2 Mini（ESP32-S2）或 Sesame Distro Board V3.1（ESP32-S3）。
- **执行器**：8× MG90S **180°** 金属齿舵机，5V 供电，PWM 50Hz。
- **显示**：SSD1306 0.96" I2C OLED（S2 Mini：SDA GPIO33 / SCL GPIO35）。
- **电源**：5V/3A 以上；USB-C PD 或 2×14500 7.4V + 5V 降压。
- **关键约束**：所有舵机共地；电源线 22AWG、信号线 30AWG；上电前必须查 5V–GND 短路。

## 约定

1. 接线资料**图 + 表**成对：图看走向，表看端子（编号 / 信号 / 线色 / 对端 / 备注）。
2. BOM 价格注明采集日期与币种；未实测的到手价标 `待验证`。
3. 上游 Gerber、BOM、Pick-and-Place 不复制进本仓库，按 [docs/99-参考资源.md](../docs/99-参考资源.md) 的链接与 commit 获取。
4. 第三方图纸标注来源与许可，不移除原作者版权信息。
