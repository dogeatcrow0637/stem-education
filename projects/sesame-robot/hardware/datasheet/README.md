# datasheet · 关键器件参数

> 最后更新：2026-11-05　|　说明：本仓库不复制第三方 PDF；下表是项目实际用到的参数摘录与官方外链。

## 1. 关键参数速查

| 器件 | 参数 | 值 | 来源 |
|---|---|---|---|
| MG90S | 工作电压 | 4.8–6V | 卖家页面/常见规格书 |
| MG90S | 堵转电流（单只） | 约 0.7–1.5A（批次差异大） | `待验证` |
| MG90S | 角度 | **180°**（本项目必须） | 上游 BOM |
| MG90S | 齿轮 | 金属齿（推荐） | 上游 BOM |
| MG90S | 线色 | 棕 GND / 红 VCC / 橙信号 | 常见规格 |
| ESP32-S2 Mini | 逻辑电平 | 3.3V | Espressif |
| ESP32-S2 Mini | 供电 | 5V（USB-C） | 开发板说明 |
| SSD1306 | 分辨率 | 128×64 | 常见规格 |
| SSD1306 | 接口 | I2C，地址常见 0x3C | 常见规格 |
| 5V 降压 | 输出 | 5V/3A 以上 | 上游要求 |
| 14500 电池 | 标称 | 7.4V 800mAh（2S） | 上游 BOM |

> 以上为教学用摘录，**不是设计保证**；实际以你买到的器件手册为准。MG90S 克隆品参数离散很大，必须实测。

## 2. 官方手册与外链

| 器件 | 链接 |
|---|---|
| ESP32-S2 系列 | https://www.espressif.com/en/products/socs/esp32-s2 |
| ESP32-S2 技术参考 | https://www.espressif.com/sites/default/files/documentation/esp32-s2_technical_reference_manual_en.pdf |
| SSD1306 | https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf |
| Adafruit SSD1306 库 | https://github.com/adafruit/Adafruit_SSD1306 |
| ESP32Servo 库 | https://github.com/madhephaestus/ESP32Servo |
| MG90S | 以卖家提供的规格页为准；不同厂家无统一数据手册 |
| 上游 BOM | https://github.com/dorianborian/sesame-robot/blob/main/hardware/bom/README.md |

## 3. 实测记录（待回填）

| 项 | 方法 | 结果 |
|---|---|---|
| 单舵机堵转电流 | 稳压 5V，堵转 1 秒读电流 | `待验证` |
| 8 舵机同时启动峰值 | 示波器/电流表 | `待验证` |
| 整机重量 | 电子秤 | `待验证` |
| 连续行走时间 | 满电到低压保护 | `待验证` |
| 舵机回差 | 角度计/量角器 | `待验证` |

## 4. 安全

- 锂电参数以电池标签为准；不要用无保护板电芯。
- 舵机堵转电流可能远超额定，电源必须留余量。
- 任何实测过流/发烫，先断电再记录，不要边跑边测。
