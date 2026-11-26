# datasheet · 英语宠物怀表

> 只登记本项目实际用到的器件；整份 PDF 体积大时保留外链，关键参数抄成表格。

## 关键器件

| 器件 | 作用 | 关键参数 | 资料 |
|---|---|---|---|
| ESP32-S3 | 主控 | 双核 240MHz，**32MB Flash / 8MB 内置 Octal PSRAM**（实板实测），Wi-Fi 2.4G + BLE5 | [ESP32-S3 数据手册](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf) |
| CO5300 | AMOLED 驱动 | 466×466，QSPI | Waveshare Wiki 原理图 |
| CST9217 | 电容触摸 | I2C，多点 | Waveshare Wiki |
| ES8311 | 音频 DAC | I2S，单声道 DAC + PA | [ES8311](https://www.everest-semi.com/pdf/ES8311%20PB.pdf) |
| ES7210 | 音频 ADC | I2S，双麦 / 4 麦 | [ES7210](https://www.everest-semi.com/pdf/ES7210%20PB.pdf) |
| AXP2101 | 电源管理 | 锂电池充电、电量计、多路 LDO | [AXP2101](https://www.x-powers.com/en.php/Info/product_detail/article_id/51) |
| QMI8658 | 6 轴 IMU | 加速度 + 陀螺仪 | Waveshare Wiki |
| PCF85063 | RTC | I2C 实时时钟 | NXP 数据手册 |

## 本项目实际用到的参数

| 参数 | 值 | 来源 |
|---|---|---|
| 音频采样率 | 24kHz（板级配置） | xiaozhi `config.h` |
| 扬声器阻抗 / 功率 | 8Ω / 2W | 选型约定 |
| 电池 | 3.7V LiPo，702530（7×25×30mm）500mAh，带保护板，MX1.25 2P | BOM / Waveshare 产品图 |
| 充电 | USB-C 5V/1A | AXP2101 规格 |
| 屏幕分辨率 | 466×466 | Waveshare 产品页 |
| 触摸接口 | I2C（CST9217） | Waveshare 产品页 |

> 未实测的续航、发热、充电时间标 `待验证`；不要用厂商宣传值替代实测值。
