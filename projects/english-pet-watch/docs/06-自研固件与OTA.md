# 06 · 自研固件与 OTA

> 适用版本：自研固件 V0.1（ESP-IDF 6.0.1+）　|　最后更新：2026-11-13
> 结论：**固件自己写，不基于 xiaozhi-esp32 固件改；服务端继续用 xiaozhi-esp32-server，并把它当作 OTA 固件仓库。**

## 一、为什么自研固件

| 维度 | 直接用 xiaozhi 固件 | 自研固件（本项目） |
|---|---|---|
| 代码可控 | 只能改 menuconfig 和局部源码，升级要跟着上游走 | 每个模块自己掌握，功能按怀表需求裁剪 |
| 体积/启动 | 含大量用不到的功能与板级代码 | V0.1 只做 Wi-Fi + OTA，镜像小、启动快 |
| 屏幕 | 上游 UI 固定 | 后面可以画自己的怀表表盘 |
| 语音链路 | 只能用上游的唤醒/AFE/协议实现 | 可以逐步实现，先跑通再接 VAD/ASR/TTS |
| 风险 | 上游改协议要跟着改 | 需要自己维护 WebSocket 协议与音频驱动 |

**边界**：

- 不复制 xiaozhi-esp32 固件源码；
- 只参考它的**板级引脚定义**与**设备端协议文档**；
- 服务端继续复用 xiaozhi-esp32-server（MIT），不改它的 OTA 逻辑。

## 二、总体架构

```text
┌──────────────────────────────┐
│ 英语宠物怀表（ESP32-S3）       │
│                              │
│  V0.1  main.c                │
│   ├─ Wi-Fi STA               │
│   └─ OTA Client              │
│                              │
│  V0.2+  音频 / WS / 屏幕       │
└──────────────┬───────────────┘
               │ HTTP POST /xiaozhi/ota/
               │ HTTP GET  /xiaozhi/ota/download/*.bin
               ▼
┌──────────────────────────────┐
│ vls · xiaozhi-esp32-server   │
│  :8003 OTA / 固件下载         │
│  :8000 WebSocket（V0.2 用）   │
│  data/bin/  ← 固件仓库        │
└──────────────────────────────┘
               │
               ▼
        VAD → ASR → LLM(豆包) → TTS
        （全部在服务端，V0.1 不使用）
```

**分工**：怀表只负责录音、放音、屏幕和联网；VAD/ASR/TTS 在服务端；LLM 走豆包 API。

## 三、服务端 OTA 契约（已核对源码）

服务端实现：`main/xiaozhi-server/core/api/ota_handler.py`。

### 3.1 检查版本

```http
POST http://<服务器>:<端口>/xiaozhi/ota/
Content-Type: application/json
device-id: 00:11:22:33:44:55
client-id: 00:11:22:33:44:55
device-model: epw-watch-175
device-version: 0.1.0
```

请求体（本固件发送，兼容服务端的 body 回退逻辑）：

```json
{
  "application": { "name": "english-pet-watch", "version": "0.1.0" },
  "board": { "type": "epw-watch-175", "name": "English Pet Watch" },
  "uuid": "00:11:22:33:44:55"
}
```

响应：

```json
{
  "server_time": { "timestamp": 1763000000000, "timezone_offset": 480 },
  "firmware": {
    "version": "0.2.0",
    "url": "http://<服务器IP>:5801/xiaozhi/ota/download/epw-watch-175_0.2.0.bin"
  },
  "websocket": {
    "url": "ws://<服务器IP>:5801/xiaozhi/v1/",
    "token": ""
  }
}
```

- `device-id` / `client-id` 缺一不可，否则服务端返回 500；
- `device-model` 优先取请求头，也可以从 body 的 `board.type` 回退；
- 没有新版本时 `firmware.version` 等于设备当前版本、`firmware.url` 为空；
- `timezone_offset` 单位是分钟（`+8` 时区 = 480），本固件用它设置系统时间。

### 3.2 固件文件命名

```text
data/bin/{device-model}_{version}.bin
```

服务端正则：`^(.+?)_([0-9][A-Za-z0-9\.\-_]*)\.bin$`

- 型号建议固定 `epw-watch-175`，与 menuconfig 的 `Device model` 完全一致；
- 版本必须是数字开头，例如 `0.1.0`、`0.2.0`；
- **型号里不要出现下划线**：服务端用第一个“下划线 + 数字”来切分文件名，型号带下划线会被截断。短横线可以正常使用。

### 3.3 版本比较规则

服务端用正则提取所有数字段，按元组逐位比较：

```text
0.10.0 > 0.9.0     （数字比较，不是字符串比较）
0.2.0  > 0.1.9
```

所以每次发版只改 `CMakeLists.txt` 的 `PROJECT_VER`，并保证新版本数字更大。

### 3.4 下载地址的来源（最容易踩坑）

服务端不是自己拼下载地址，而是拿 `server.vision_explain` 做字符串替换：

```text
http://<host>:8003/mcp/vision/explain
        ↓ 替换路径
http://<host>:8003/xiaozhi/ota/download/epw-watch-175_0.2.0.bin
```

Docker 部署时如果 `vision_explain` 留默认占位符，服务端会用**容器内网 IP**，设备下载必定失败。必须在 `data/.config.yaml` 写死设备能访问的地址：

```yaml
server:
  port: 8000
  http_port: 8003
  vision_explain: http://<服务器IP>:5801/mcp/vision/explain
```

公开部署时把协议换成 `https://`。

## 四、固件实现

### 4.1 分区

`partitions.csv`（32MB Flash，实板实测）：

| 分区 | 大小 | 用途 |
|---|---|---|
| `ota_0` | 8MB | 应用槽 A |
| `ota_1` | 8MB | 应用槽 B |
| `storage` | 15.8MB | 后续放字体/表盘资源 |

首次 USB 烧录写入 `ota_0`；OTA 写入另一槽并更新 `otadata`，重启后由引导程序切换。

### 4.2 启动流程

```text
上电
 → 初始化 NVS / netif / event loop
 → 连接 Wi-Fi（失败每 3 秒重连）
 → 拿到 IP
 → POST /xiaozhi/ota/ 上报 device-id/model/version
 → 有 firmware.url ？
     是 → 下载 → 校验镜像格式 → 写入备用分区 → 重启
     否 → 打印最新版本 → 等待 300 秒再查
```

### 4.3 代码边界

| 文件 | 职责 |
|---|---|
| `main/main.c` | 启动顺序、定时轮询、升级后重启 |
| `main/wifi_sta.c` | STA 连接、断线重连、事件组通知 |
| `main/ota_client.c` | 组 JSON、带自定义头上报、解析响应、`esp_https_ota` 下载 |
| `main/Kconfig.projbuild` | Wi-Fi / 型号 / OTA 地址 / 轮询间隔 |

V0.1 **不包含**：屏幕、音频 codec、唤醒词、WebSocket、Opus。这些不是“漏了”，是下一阶段按顺序加。

## 五、发布与验证流程

### 5.1 电脑侧先验证服务端

板子没到也能测：

```bash
cd projects/english-pet-watch/firmware
./tools/ota_probe.sh http://<服务器IP>:5801/xiaozhi/ota/ 00:11:22:33:44:55 0.1.0 epw-watch-175
```

- 返回 200 且 `websocket.url` 正确 → 服务端契约通；
- 发布一个更高版本 bin 后，再跑一次，`firmware.url` 应非空。

### 5.2 发布固件

```bash
# 改 CMakeLists.txt：set(PROJECT_VER "0.2.0")
idf.py build
./tools/publish_firmware.sh build/english_pet_watch.bin 0.2.0
```

脚本会 `scp` 到 `vls:~/english-pet-watch/data/bin/epw-watch-175_0.2.0.bin`，服务端缓存默认 30 秒刷新。

### 5.3 设备侧验证

1. 串口出现 `获得 IP`；
2. 串口出现 `服务端 WebSocket 地址`；
3. 设备打印 `发现新固件: 0.2.0`；
4. 出现 `OTA 进度: 10% ... 100%`；
5. 重启后版本变为 `0.2.0`，`分区: ota_1`（或从 `ota_1` 回到 `ota_0`）。

## 六、后续版本路线

| 版本 | 目标 | 关键工作 |
|---|---|---|
| **V0.1** | 联网 + OTA | Wi-Fi、OTA 检查/下载/重启（本次） |
| **V0.2** | 能听能说 | ES8311/ES7210 I2S、Opus 编解码、WebSocket 握手、`listen start/stop`、二进制音频帧 |
| **V0.3** | 怀表界面 | CO5300 QSPI 屏幕、LVGL 表盘、电量/时间/状态 |
| **V0.4** | 英语老师体验 | 触摸说话、VAD 打断、单词本 Memory、错误温和纠正 |
| **V0.5** | 稳定与安全 | 软配网、HTTPS、固件签名、自动回滚、续航优化 |

> 注：`firmware/ui_demo/` 里的 **UI Demo V0.6** 是界面/交互里程碑（单表盘 + 应用桌面 + 宠物页 + 控制中心 + 状态页），与上表固件业务版本 V0.4/V0.5（语音与稳定性）不是同一编号。

V0.2 的协议细节以 xiaozhi-esp32 的 `docs/websocket_zh.md` 为准；服务端已支持，不需要改服务端。

### 6.1 V0.2 需要从上游板级配置抄的引脚（只抄引脚，不抄代码）

参考文件：`main/boards/waveshare/esp32-s3-touch-amoled-1.75/config.h`

| 项 | 值 |
|---|---|
| 音频采样率 | 24000 |
| ES8311（DAC）地址 | 见上游 `config.h` |
| ES7210（ADC）地址 | 见上游 `config.h` |
| 功放使能 | GPIO46 |
| 屏幕 | CO5300，466×466，QSPI |
| 触摸 | CST9217，I2C |

> ⚠️ 1.75 与 1.75C 的 MCLK、LCD_RST、触摸复位不同；买的是哪个版本就抄哪个版本的 `config.h`。

## 七、风险与待办

| 风险 | 现状 | 处理 |
|---|---|---|
| OTA 无签名 | 只依赖传输通道 | 公网前加签名或 Secure Boot；局域网先接受 |
| HTTP 明文 | 局域网联调可用 | 公网改 HTTPS/WSS |
| 配网写死 | 每次换 Wi-Fi 要重烧 | V0.2 加 SoftAP 网页配网 |
| 无自动回滚 | 新固件启动失败需 USB 救回 | 需要时开 `BOOTLOADER_APP_ROLLBACK_ENABLE` |
| 服务端内存余量小 | 主机可用约 2.9 GiB | 不要再叠加本地大模型/TTS |
| `vision_explain` 配错 | 下载 URL 指向容器内网 | 部署检查清单固定核对这一项 |

## 八、回滚

```bash
# 设备端救砖：USB 重新烧录上一版
idf.py -p /dev/tty.usbmodem* flash

# 服务端撤版
ssh vls "rm ~/english-pet-watch/data/bin/epw-watch-175_0.2.0.bin"
```

> 服务端只下发比设备版本更高的固件，撤掉新 bin 后设备会停在当前版本。
