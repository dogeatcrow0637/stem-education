# 烧录参数速查

> 适用版本：上游 `f328ed3`　|　最后更新：2026-11-05

## 板型

| 硬件 | Board | Upload Speed | USB CDC On Boot | Partition |
|---|---|---|---|---|
| Lolin S2 Mini | LOLIN S2 Mini | 921600 | Enabled | Default 4MB with spiffs |
| Distro V3.1 | ESP32S3 Dev Module | 默认/921600 | Enabled | Default 4MB with spiffs |
| Distro V2 | ESP32S3 Dev Module | 默认 | Enabled | Default 4MB with spiffs |
| Distro V1 | ESP32 Dev Module | 默认 | 不适用 | Default 4MB with spiffs |

V3.1 额外：Flash Mode `QIO 80MHz`。

## 库版本

| 库 | 版本 |
|---|---|
| ESP32Servo | **3.0.9**（锁定） |
| Adafruit SSD1306 | 最新稳定版 |
| Adafruit GFX Library | 最新稳定版 |

## 串口

- 波特率：**115200**
- 电机测试固件：输入 `0,90`（舵机号,角度），`stop` 停止
- 启动日志应包含网络信息与 IP

## 常见失败

| 现象 | 处理 |
|---|---|
| 找不到端口 | 换数据线；装 CP210x/CH340 驱动；S3 开 USB CDC |
| 上传卡住 | 按住 BOOT 再点 Upload；降 Upload Speed |
| 上传成功但重启循环 | 断舵机；换 5V/3A；确认不是 V3 初版 |
| Linux 权限 | `sudo usermod -a -G dialout $USER` 后重新登录 |

## 官方步骤

https://github.com/dorianborian/sesame-robot/blob/main/firmware/README.md
