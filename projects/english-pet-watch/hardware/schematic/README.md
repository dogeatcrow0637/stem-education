# schematic · 英语宠物怀表

> 本项目使用**成品开发板**，不自绘 PCB、不产出 Gerber。
> 这里放厂商原理图、接线图与端子表，方便排障时对照。

## 该放什么

| 文件 | 内容 | 状态 |
|---|---|---|
| `esp32-s3-touch-amoled-1.75_sch.pdf` | Waveshare 官方原理图（从 Wiki 下载） | 待归档 |
| `wiring_power_v1.0.png` | 电池 / 喇叭 / USB 三处接线示意 | 待绘制 |
| `wiring_table_v1.0.md` | 端子号 / 信号 / 线色 / 对端 | 见 `../README.md` 第 2 节 |

## 官方资料

- 产品页：https://www.waveshare.com/esp32-s3-touch-amoled-1.75c.htm
- Wiki：https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75C
- 固件板级配置：https://github.com/78/xiaozhi-esp32/tree/main/main/boards/waveshare/esp32-s3-touch-amoled-1.75c

## 约定

- 图 + 表成对提供；表里写清端子号、信号名、线色、对端、备注。
- 厂商图纸保留版权信息，不改原图；自己画的接线图标注版本与日期。
- 实板已确认是 **1.75C**（2026-11-16 esptool 读取 SKU），引脚与 BSP 一律按 1.75C 处理。
