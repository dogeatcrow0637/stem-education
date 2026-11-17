# firmware · 英语宠物怀表（自研固件）

> 适用版本：自研固件 V0.1 · UI Demo V0.11（ESP-IDF 6.0.2）　|　最后更新：2026-11-16
> 目标板：Waveshare ESP32-S3-Touch-AMOLED-1.75C（实测 ESP32-S3 rev v0.2 / **32MB Flash** / 8MB PSRAM）
>
> **本项目不使用 xiaozhi-esp32 自带固件。** 固件源码在本目录，自行实现 Wi-Fi、OTA 与后续语音链路；
> xiaozhi-esp32 只作为**板级引脚与协议参考**，不复制其代码。服务端复用 xiaozhi-esp32-server 的 OTA 与 WebSocket 能力。

## 本轮修复与验证（2026-11-17）

当前源码已调整：

- 控制中心为 **3+3 六磁贴**，不设天气磁贴；图标装饰不再抢点击。
- 表盘接入中央气象台萧山站实况（公开免密钥接口），固定杭州萧山区，**不获取定位**。显示温度、来源及观测时间；每 15 分钟请求约 1KB 数据，失败每 60s 重试。数据超过 2 小时显示暂不可用；无晴雨状态时不补造天气图标。HTTPS 校验证书，响应上限 4KB。
- 下拉折返可取消；PWR 按当前顶层页面逐层返回。
- 未校时显示「等待校时」；USB 供电和 PMU 实际充电状态分别处理。
- 校时采用阿里云 / 腾讯 / 中国 NTP 池三个来源；首次 10s 仍未成功时，后台读取公共服务固定 IP 的 HTTP Date 兜底（不依赖热点 DNS），每 20s 重试，成功后退出兜底任务。联网后保留热点 DNS，并增加公共备用 DNS；串口打印校时成功的北京时间。
- 音量页显示「静音」，没有无效滑杆；静音模式不创建 Opus 解码器和播放缓冲。
- 进入宠物页不会自动录音，**左滑才请求聊天**；联网并完成服务器 hello 后才打开麦克风。
  右滑退出、PWR 返回、BOOT 退出、切页或 30s 熄屏均取消会话，后台关闭麦克风并释放编码器和网络资源。
- 删除固定台词模拟的聊天计时器，表情和回复由实际服务器事件驱动；未配置时明确提示。
- 中文 JSON 使用官方 `espressif/cjson`，支持 UTF-8 和 Unicode 转义；WebSocket 分片重组上限 8192 字节，超限整条丢弃。
- Wi-Fi 连接成功获得 IP 后才保存账号；密码键盘覆盖全部可打印 ASCII，支持 WPA2/WPA3 常见密码及 64 位十六进制 PSK。
- 16px 保留完整常用中文字库，20px 仅保留静态文案子集。

开发验证（在 `ui_demo/` 内，先激活 ESP-IDF）：

```bash
idf.py build
python3 tools/test_firmware.py         # 需要主机 C 编译器；首次构建后使用托管 cJSON 源码
python3 tools/check_font_coverage.py
bash -n tools/gen_ui_assets.sh
```

主机回归覆盖中文/转义/无效 JSON、握手门槛、会话取消、两种 WebSocket 分片与超限、
下拉取消、PWR 顶层返回、装饰对象命中、供电状态、离线时间和密码键盘字符覆盖；
另覆盖 HTTP Date 日期解析与天气站点、温度、日期和时效校验。

2026-11-17 已烧录到 1.75C 实机，写入哈希校验通过；串口确认识别 8MB PSRAM、
生成 466×466 控制中心快照、进入 `UI ready` 并连接 Wi-Fi。
本次启动观察未出现崩溃或未操作便启动麦克风的日志。

同日天气/校时修复已再次烧录：串口确认启动约 15s 校时成功，约 21s 取得萧山
13.5°C（观测时间 10:45），HTTPS 证书校验通过；7 项主机回归与字库覆盖检查通过。
天气显示的实际排版、断网两小时后的失效提示及长期更新仍待真机观察。

其余真机交互与语音验收 **待验证**：

1. ⚠️ 首次烧录按下文流程断开电池，仅用 USB 供电；核对板型、32MB Flash 和串口。
2. 点六个磁贴的图标中心及空白处，再从图标上拖动收起；下拉后拖回起点应取消。
3. 桌面 → 状态 → PWR 应回桌面；网络密码页应逐层返回。音量页应只显示静音。
4. 宠物页左滑后观察 `microphone active`，服务端应收到真实语音帧；退出/熄屏后应出现
   `voice session stopped`，服务端不再收到新的录音帧。断网和服务器不可达时不应启动麦克风。
5. 中文正常显示；没有网络校时时显示等待状态，不出现演示日期或天气；联网校时后应出现萧山实况及其观测时间。
6. 接电池前核对极性与电压，由家长操作；对照 PMU 电压与实际电池电压校准电量判断。
   分别验证 USB 无电池、充电中、已充满三种状态；电流、帧率、内存峰值和续航改善均待验证。

以下截图与真机记录是修复前的历史版本，布局和语音行为以本节为准。

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
| 音频 | 按需启用 16kHz 单声道麦克风 → Opus(60ms) → WebSocket；扬声器静音，下行只显示文字 |
| 网络 | 编译期写入 2.4GHz 热点，上电自动连接；联网后 SNTP 同步北京时间，未校时时显示等待状态 |
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

> `main/CMakeLists.txt` 会在配置阶段把 Waveshare BSP 的 LVGL 分块高度 `50 → 10`（幂等）。
> 原因：LVGL 画布在 PSRAM，SPI 刷新要申请同样大小的内部 DMA 反弹缓冲；Wi-Fi 扫描时内部堆碎片化，
> 50 行（≈46KB）申请不到连续内存，整屏刷新会报 `Failed to allocate priv TX buffer`。
> `managed_components` 重新拉取后补丁仍会自动生效。

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

中文字体采用 16px 完整常用字库与 20px 静态界面子集（只从代码字符串取字，注释不进入子集）；表情来自 Microsoft Fluent Emoji 3D（MIT）。
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

### 豆包语音链路（xiaozhi-server WebSocket）

设备端实现见 `ui_demo/main/voice_client.c`，按 xiaozhi-esp32 `websocket_zh.md` 的 **v1** 协议：

- 上行：ES7210 麦克风 → PCM 16kHz/单声道/16bit → Opus 60ms 帧 → WebSocket 二进制帧；
- 下行：接收文本事件显示表情与台词；TTS 二进制帧直接丢弃，当前不解码、不播放；
- 文本事件：`hello` 建会话、`stt` → 宠物「思考」、`llm` 的 `emotion`/`text` → Mimi Cat 表情与气泡、`tts` 控制上下行；
- 鉴权 token 只从本地 Kconfig 读取，不保存云 Key。

配置（写进 `sdkconfig.local`，不入库；留空 = 关闭语音链路）：

```text
CONFIG_EPW_VOICE_WS_URL="ws://<服务器IP>:5801/xiaozhi/v1/"
CONFIG_EPW_VOICE_TOKEN=""
```

编译/烧录命令同上；串口验证顺序：

```text
宠物页左滑请求聊天
服务器完成 hello 握手
epw-voice: microphone active (16 kHz mono)
退出/熄屏后：epw-voice: voice session stopped; microphone closed
```

> 语音链路依赖 Wi-Fi 与服务端可达。2026-11-17 实测：`vls` 上 xiaozhi 容器在跑（`127.0.0.1:8000/8003`），
> 但 nginx `5801` 未监听、安全组未放行（外网 `nc <服务器IP> 5801` 超时）；
> 打通公网入口前，设备会停在「等 Wi-Fi / 等 WebSocket」阶段，不会崩。

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
