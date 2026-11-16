# firmware · 英语宠物怀表（自研固件）

> 适用版本：自研固件 V0.1 · UI Demo V0.11（ESP-IDF 6.0.2）　|　最后更新：2026-11-16
> 目标板：Waveshare ESP32-S3-Touch-AMOLED-1.75C（实测 ESP32-S3 rev v0.2 / **32MB Flash** / 8MB PSRAM）
>
> **本项目不使用 xiaozhi-esp32 自带固件。** 固件源码在本目录，自行实现 Wi-Fi、OTA 与后续语音链路；
> xiaozhi-esp32 只作为**板级引脚与协议参考**，不复制其代码。服务端复用 xiaozhi-esp32-server 的 OTA 与 WebSocket 能力。

## 0. 当前可用：真机 UI Demo（2026-11-16）

`ui_demo/` 是独立可编译的 ESP-IDF 工程，已在 **Waveshare ESP32-S3-Touch-AMOLED-1.75C** 上跑通：

| 项 | 值 |
|---|---|
| 屏幕 | 466×466 圆屏，CO5300，QSPI，亮度可调 |
| 触摸 | CST9217，I2C，单点；点按 / 滑动 / 长按逻辑已实现（现场操作时串口打印 `gesture/tap/long press`） |
| 页面 | **单表盘**（无刻度 / Fredoka 150px 大号时间 / 中文日期 / 顶部天气 / 三色装饰环 / 底部绿色圆弧电量）；表盘下滑 → **控制中心**（4+3 手绘图标磁贴），控制中心点状态/电量 → **状态页**；点亮度/音量 → 独立调节页；手电 → 白屏；`PWR` 短按仍可进应用桌面（备用），`BOOT` 短按进宠物页。**无每日单词页、无页码点、无点击换屏** |
| 宠物 | 独立宠物页 **Mimi Cat · 3D 正脸猫**：8 张 Fluent Emoji 3D（MIT）+ 9 种情绪、对话气泡、纯手势互动；本地情景引擎按电量/充电/网络/时间换状态 |
| 物理键 | `BOOT`（GPIO0）短按 → 宠物页；`PWR`（AXP2101）短按 → 应用桌面；长按 PWR 仍由 PMU 关机 |
| 豆包桥接 | 设备轮询 `EPW_PET_BRIDGE_URL` 的轻量 JSON（`emotion` + `text`），云 Key 只在服务端；未配置时只用本地情景引擎 |
| 音频 | **不初始化 I2S/ES8311，音量固定 0**；界面只显示「静音」，不出现聆听/说话假状态 |
| 网络 | 编译期写入 2.4GHz 热点，上电自动连接；联网后 SNTP 同步北京时间，失败时用内置时钟兜底 |
| 构建 | ESP-IDF **6.0.2**，LVGL 9.5.0，Waveshare BSP 3.0.0 |

```bash
cd projects/english-pet-watch/firmware/ui_demo
. <ESP-IDF>/export.sh                # 本机 /Users/eatcrow/esp/esp-idf-v6.0.2

# 首次配置热点：把 SSID/密码写进 sdkconfig.local（不入库），再叠加构建
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.local" idf.py build
# 后续直接增量编译
idf.py build                         # 产物 build/epw_ui_demo.bin
idf.py -p /dev/cu.usbmodem* flash monitor
```

热点配置放本地 `sdkconfig.local`（`CONFIG_EPW_WIFI_SSID` / `CONFIG_EPW_WIFI_PASSWORD`），**不要提交**。
也可以 `idf.py menuconfig` → `English Pet Watch UI Demo` 里填写。

2026-11-16 真机验证（`/dev/cu.usbmodem1101`，UI Demo V0.11）：

- 单表盘：150px Fredoka SemiBold 圆润数字时间（垂直居中、整体下移）+ 20px 中文日期；顶部太阳 / 24° / 晴贴着上沿弧线；右上 Wi-Fi 图标只用颜色表达状态；**无刻度环、无电子宠物**。
- 外圈：直径 446、线宽 8 的亮黄 → 橙 → 天蓝三色装饰环常驻；底部再收一圈 120° 电量弧（⚡ + 百分比），主色固定绿色 `#2EE66B`，插 USB 时只多一圈天蓝柔光。两种模式截图见 `resources/screenshots/home*.png`。
- 控制中心：表盘下滑进入；4+3 小磁贴（状态 / 天气 / 电量 / 手电 + 亮度 / 音量 / Mimi Cat），每块一个独立手绘图标，无进度弧与端点圆点；上滑或点「上滑收起」返回表盘。
- 状态页：控制中心点「状态 / 电量」进入；运行时读取显示 / 触摸 / 电池 / 音频 / 网络 / IP / 芯片 / 闪存 / 内存；**从左往右滑**或点返回圆钮回到控制中心。
- 亮度 / 音量页：点对应磁贴进入；居中 44×280 科技浅绿粗轨道，只显示当前值与 100/0 刻度；必须按在轨道上拖动，左→右滑关闭。
- 手电页：点「手电」进入，纯白满亮度，双击退出并恢复原亮度。
- 宠物页：控制中心 Mimi Cat 磁贴 / 应用桌面「宠物」/ `BOOT` 短按进入；3D 正脸猫 Mimi 的 9 种情绪、气泡台词与纯手势互动。
- 应用桌面：`PWR` 短按进入（备用入口）；2×3 图标可长按拖动换位，布局写 NVS。
- 已删除每日单词页、页码点与左右点击翻页；表盘点击不再换屏。
- Mimi Cat 磁贴与宠物页均使用 Microsoft Fluent Emoji 3D（MIT）素材，宠物页为 8 张正脸猫。
- 当前热点 `<你的热点>` 已连接成功；若热点不可见，串口会打印 `reason=201` 并列出扫描到的 2.4GHz AP。
- 2026-11-16 全量编译通过（`idf.py build`，零警告），镜像 `build/epw_ui_demo.bin` 约 4.7MB（app 分区剩余 44%）。

### 真机截图工具

演示构建打开 `CONFIG_EPW_BOOT_SHOTS=y` 后，开机 8s 自动巡页并把 LVGL 快照按行 base64 打到串口（当前截图标签：`home` / `home_normal` / `center` / `status` / `bright` / `vol`；应用桌面与宠物页待补抓）：

```bash
# 同一次抓取产生多份日志时可以一起传入；解码器会挑出校验通过的行
python3 tools/decode_shots.py /tmp/epw-monitor*.log -o ../../resources/screenshots
```

`tools/decode_shots.py` 会把 RGB565 行数据还原成 24-bit BMP，无需额外 Python 依赖。

### UI 素材重新生成

中文字体只打包界面用到的汉字（脚本只从字符串字面量取字，注释里的中文不会进子集；当前 159 字形，含宠物页台词）；表情来自 Microsoft Fluent Emoji 3D（MIT）。
改动界面文案或换表情后执行：

```bash
cd projects/english-pet-watch/firmware/ui_demo
./tools/gen_ui_assets.sh      # 需要 curl / sips / node / python3
```

素材许可与上游链接见 [`../../resources/ATTRIBUTION-fluent-emoji.md`](../../resources/ATTRIBUTION-fluent-emoji.md)。

### 豆包宠物状态桥（可选）

宠物页默认用**本地情景引擎**（电量 / 充电 / 网络 / 时间 + 7 句英语台词轮换）。要把豆包回复映射成宠物表情，加一个服务端 JSON 接口即可；**设备端不保存任何云 Key**：

```text
GET /pet/state  ->  {"emotion":"happy","text":"Hello！跟我学英语吧！"}
emotion ∈ idle | happy | excited | thinking | speaking | sleepy | sad | love | listening
```

menuconfig → `English Pet Watch UI Demo` → `Doubao pet bridge URL` 填 `http://<电脑IP>:8788/pet/state`，轮询间隔默认 8000ms（`EPW_PET_BRIDGE_INTERVAL_MS`）。

本地调试（不需要 Key）：

```bash
cd projects/english-pet-watch/firmware/ui_demo
python3 tools/doubao_bridge_mock.py --port 8788              # 纯剧本，6s 换一种情绪
export ARK_API_KEY=xxxx                                      # 火山方舟 Key
python3 tools/doubao_bridge_mock.py --port 8788 --doubao     # 真豆包回复 → 情绪 + 台词
```

桥接只更新宠物状态，不主动弹页；服务端 30s 无新数据时自动回落到本地情景引擎。正式语音链路仍走 xiaozhi-server 的 WebSocket（ASR → 豆包 LLM → TTS）。

## 1. V0.1 能做什么

| 能力 | 状态 |
|---|---|
| 连接 2.4GHz Wi-Fi，断线自动重连 | ✅ 已实现 |
| 向服务端上报型号/版本，检查新固件 | ✅ 已实现 |
| 从服务端 `data/bin/` 下载固件并写入备用 OTA 分区 | ✅ 已实现 |
| 双 OTA 分区 + 自动切换启动分区 | ✅ 分区表已配 |
| 屏幕、音频、唤醒词、WebSocket 对话 | ⏳ V0.2 起，见 `../docs/06-自研固件与OTA.md` |

## 2. 目录结构

```text
firmware/
├── README.md
├── ui_demo/                  # ✅ 当前真机可跑：表盘 + 应用桌面 + 宠物页 + 控制中心 + 状态页 + Wi-Fi
│   ├── CMakeLists.txt
│   ├── partitions.csv        # 8MB factory app + 7MB SPIFFS
│   ├── sdkconfig.defaults    # 32MB Flash / 8MB PSRAM / LVGL 9.5
│   ├── sdkconfig.local       # 热点 SSID/密码（不入库）
│   ├── main/
│   │   ├── main.c            # NVS → 显示 → AXP2101 → UI → Wi-Fi → 豆包桥
│   │   ├── ui.c/h            # 表盘 / 应用桌面 / 宠物页 / 控制中心 / 状态页
│   │   ├── button.c/h        # BOOT → 宠物页，PWR → 应用桌面
│   │   ├── doubao_bridge.c/h # 轮询服务端 JSON，更新宠物情绪与台词
│   │   ├── power.c/h         # AXP2101 电量/充电/VBUS
│   │   ├── wifi.c/h          # 2.4GHz STA 自动重连 + AP 扫描诊断 + IP 字符串
│   │   ├── shot.c/h          # 真机截图（LVGL snapshot → 串口 base64）
│   │   ├── assets/           # GB2312 中文字体 + Fredoka 150px 时间字体 + Fluent Emoji 3D 正脸猫
│   │   └── Kconfig.projbuild # SSID/密码/重试/豆包桥/截图开关
│   └── tools/
│       ├── decode_shots.py   # 串口截图还原为 BMP
│       ├── doubao_bridge_mock.py # 宠物状态桥本地 mock / 真豆包调试
│       ├── png_to_lvgl.py    # PNG → LVGL RGB565A8
│       └── gen_ui_assets.sh  # 重新生成字体与表情资源
├── CMakeLists.txt            # 规划中的 V0.1 OTA 固件（尚未实现）
├── sdkconfig.defaults        # 32MB Flash / 双 OTA 槽（规划）
└── partitions.csv            # 两个 8MB OTA 槽 + 15.8MB storage（规划）
```

> 根目录的 V0.1（Wi-Fi + OTA 业务）仍是骨架规划；今天交付的是 `ui_demo/` 真机 UI。

## 3. 编译

```bash
# 1. 安装并激活 ESP-IDF（每次新终端都要 source）
. <你的ESP-IDF>/export.sh
idf.py --version          # 已验证 v6.0.2

# 2. 进入本目录
cd projects/english-pet-watch/firmware
idf.py set-target esp32s3

# 3. 配置 Wi-Fi、型号和 OTA 地址
idf.py menuconfig
#   English Pet Watch ->
#     Wi-Fi SSID / Password
#     Device model (OTA file prefix)   默认 epw-watch-175
#     OTA check URL                    例：http://<服务器IP>:5801/xiaozhi/ota/
#     OTA check interval (seconds)     默认 300

# 4. 编译
idf.py build
```

产物：`build/english_pet_watch.bin`。版本号在根 `CMakeLists.txt` 的 `PROJECT_VER`，改完要重新 `idf.py build`。

## 4. 烧录

```bash
# macOS
idf.py -p /dev/tty.usbmodem* flash monitor
# Linux
idf.py -p /dev/ttyACM0 flash monitor
# Windows
idf.py -p COMx flash monitor
```

首次烧录必须用 USB。烧录时断开电池，只用 USB 供电；识别不到串口时按住 BOOT 再点 RST。

启动日志应出现：

```text
英语宠物怀表 · 自研固件
版本: 0.1.0
型号: epw-watch-175
OTA : http://<服务器IP>:5801/xiaozhi/ota/
分区: ota_0
...
获得 IP: 192.168.x.x
当前已是最新固件: 0.1.0
```

## 5. 发布一次 OTA

服务端只认这个文件名：`{device-model}_{version}.bin`。

```bash
# 1. 提升版本号：改 CMakeLists.txt 里的 PROJECT_VER，例如 0.2.0
idf.py build

# 2. 发布到 vls 的 data/bin（脚本默认 ssh vls）
./tools/publish_firmware.sh build/english_pet_watch.bin 0.2.0

# 3. 板子还没到？先用电脑模拟设备
./tools/ota_probe.sh http://<服务器IP>:5801/xiaozhi/ota/ 00:11:22:33:44:55 0.1.0 epw-watch-175
```

预期响应里 `firmware.version = "0.2.0"`、`firmware.url` 指向 `/xiaozhi/ota/download/epw-watch-175_0.2.0.bin`。

> ⚠️ 服务端 `server.vision_explain` 必须写成**设备能访问的公网/局域网地址**，否则下载 URL 会变成 Docker 容器内网 IP。
> 见 `../software/host/vls/smoke.config.yaml` 的 `vision_explain`。

## 6. 验证（V0.1 四件事）

1. 串口出现 `获得 IP`；
2. 串口出现 `服务端 WebSocket 地址: ws://...`（说明 OTA 契约已通）；
3. 发布更高版本后，设备日志出现 `发现新固件` → `OTA 进度` → `重启后生效`；
4. 重启后版本号变为新版本，分区在 `ota_0` / `ota_1` 之间切换。

## 7. 已知边界

| 项 | 现状 | 后续 |
|---|---|---|
| 配网 | 编译时用 menuconfig 写死 Wi-Fi | V0.2 加 SoftAP 网页配网 |
| 固件签名 | 未做；仅靠 HTTPS/HTTP 通道与镜像格式校验 | 公网前加 `esp_secure_boot` 或镜像签名 |
| 传输 | 局域网可 HTTP；公网必须 HTTPS | 域名 + 证书 |
| 回滚 | 保留旧 bin 可重新发布；设备端未开自动回滚 | 需要时开 `BOOTLOADER_APP_ROLLBACK_ENABLE` |
| 语音 | 未实现 | V0.2 音频 + WebSocket 流式对话 |

## 8. 常见故障

| 现象 | 原因 | 处理 |
|---|---|---|
| `还没有配置 Wi-Fi` | menuconfig 里 SSID 为空 | 填 SSID/密码，重编重烧 |
| 一直 `等待 Wi-Fi 连接超时` | 5GHz 热点 / 密码错 / 信号弱 | 用 2.4GHz；核对密码；靠近路由器 |
| OTA 返回 HTTP 500 | 缺 `device-id`/`client-id` 头 | 确认跑的是本目录固件，不是旧固件 |
| `已是最新固件` 但发了新 bin | 文件名前缀不等于 `device-model`，或版本号没变大 | 文件名必须是 `epw-watch-175_x.y.z.bin`；版本数字要比设备大 |
| 下载 404 | `vision_explain` 没配公网地址 / nginx 没转发 `/xiaozhi/ota/download/` | 改服务端配置，重启容器；检查 nginx |
| OTA 后花屏/反复重启 | 新固件本身有问题或分区表不匹配 | USB 重新烧录上一版 bin |
| 编译找不到 `esp_https_ota.h` | IDF 版本过低 | 升级到 6.0.1+，`idf.py fullclean` 后重编 |

## 9. 安全提示

- ⚠️ 不要把 Wi-Fi 密码、服务器密钥提交到 git；本仓库只保存源码与模板。
- ⚠️ 公网 OTA 必须用 HTTPS，且建议后续加镜像签名；当前 HTTP 只用于局域网/短时联调。
- ⚠️ 电池接入、充电与装壳由家长执行，见 `../docs/03-组装与调试.md`。
