# 英语宠物怀表

> 一句话定位：一块 **ESP32-S3 + 1.75 英寸 AMOLED 触摸圆屏**的怀表，接入语音大模型，做孩子随身的英语陪练。

| 字段 | 值 |
|---|---|
| 项目编号 | 06 |
| 目录 | `projects/english-pet-watch` |
| 状态 | 真机 UI Demo V0.11 验证通过（2026-11-16；Fredoka 150px 纯数字表盘 + 三色环 + 绿色电量弧 + 4+3 手绘图标控制中心 + 状态页 + 亮度/音量调节页已在 1.75C 实机显示并重新截图） |
| 方向 | 语音交互 / 大模型 / 儿童英语 / 可穿戴 |
| 建立日期 | 2026-11-13 |
| 固件 | 自研固件（`firmware/`；上游仅作板级引脚/协议参考） |
| 建议年龄 | 6–10 岁（需家长配置与监管） |

## 这是什么

这是一块**怀表形状的语音电子宠物**：按一下或喊一声，它抬头、眨眼、用英语跟你聊天；聊完会记住今天学过的单词，第二天主动复习。它不追求机器人身体，只保留「屏幕表情 + 麦克风 + 扬声器 + 大模型」这条最核心的链路。

设计上只做一件事，因此刻意简化：

| 简化点 | 做法 | 原因 |
|---|---|---|
| 硬件形态 | 单块 1.75" 466×466 AMOLED 圆屏 + 怀表壳 | 硬件更少、更像随身物 |
| 唤醒方式 | 先用触摸/按键说话；后续用 ESP-SR 内置唤醒词 | 不自训练唤醒模型 |
| 语音链路 | 国内可用：流式 ASR → LLM → TTS | 家庭网络可用、成本可控 |
| 功能范围 | 只做单词本、今日句型、学习 streak | 聚焦英语老师 |
| 调试入口 | 可选的 LVGL 桌宠预览 | 先保证设备端闭环 |

教学价值：孩子能亲眼看到「声音 → 文字 → 大模型 → 声音」这条 AI 链路，并参与装壳、烧录、配置、对话设计和续航测试；最后得到一件**自己参与做的、每天真的会用**的作品。

## 能力与参数

| 参数 | 值 |
|---|---|
| 主控 / 平台 | ESP32-S3R8，双核 LX7 240MHz，**32MB Flash** / 8MB PSRAM（2026-11-16 实机实测） |
| 屏幕 | 1.75" AMOLED 圆屏，466×466，CO5300 驱动，QSPI |
| 触摸 | CST9217 电容触摸，I2C（实机识别为 0x9217 / ProjectID 0x5734） |
| 实测型号 | Waveshare **ESP32-S3-Touch-AMOLED-1.75C**；MAC `7c:0c:5f:48:02:98`；芯片 rev v0.2 |
| 音频 | ES8311（DAC）+ ES7210（ADC），双麦阵列，板载回声消除参考 |
| 语音链路 | 本地唤醒/触摸 → Opus 24kHz → 云端 ASR → LLM → 流式 TTS |
| 网络 | 2.4GHz Wi-Fi（家里路由器 / 手机热点）；蓝牙 5 LE（未使用）；无 4G |
| 电源 | AXP2101 + 3.7V LiPo（MX1.25 接口），USB-C 充电 |
| 其他板载 | QMI8658 六轴 IMU、PCF85063 RTC、TF 卡槽、8PIN 扩展排针 |
| 整机形态 | 怀表：目标外径 60–68mm、厚 18–24mm（待结构验证） |
| 续航 | 连续对话约 2–4h；息屏看时间约 1–2 天（**待验证**，与电池容量强相关） |
| 首响延迟 | 目标 ≤1.5s（流式管线）；实时语音模型可更低（**待实测**） |
| 成本 | 硬件 ￥300–450；云端 ￥20–80/月（**参考价，待验证**） |

## 当前进度

- [x] 硬件选型：确认 Waveshare ESP32-S3-Touch-AMOLED-1.75C；引脚以 1.75C BSP 为准
- [x] 实物验板（2026-11-16）：32MB Flash / 8MB PSRAM / MAC / 触摸 / AXP2101 均已实测，见 [hardware/README.md](hardware/README.md)
- [x] 真机 UI Demo V0.11：**儿童纯数字表盘**（Fredoka 150px 圆润时间 + 20px 中文日期 + 顶部天气 + 黄/橙/天蓝三色装饰环 + 顶部绿色天气分割线 + 底部绿色圆弧电量 + 闪电/百分比，充电只叠天蓝柔光，Wi-Fi 仅用颜色，无刻度环、无电子宠物）+ 下滑 4+3 小磁贴控制中心（状态 / 天气 / 电量 / 手电 + 亮度 / 音量 / Mimi Cat，全部独立手绘图标）+ 只读状态页 + 科技浅绿独立亮度/音量拖动页 + 双击退出手电页；已删除每日单词页、页码点与点击换屏，1.75C 六张真机截图见 `resources/screenshots/`
- [x] 六维工程文档、家长实现手册、6 课时课程与知识卡
- [x] 自研固件 V0.1 骨架：Wi-Fi 自动连接与断线重连、OTA 分区（业务逻辑待接入）
- [ ] 音频链路：ES8311/ES7210、唤醒词、WebSocket 对话（办公室环境暂不验证）
- [ ] 服务端部署与英语老师 Prompt 调优
- [ ] 怀表外壳打样与续航实测
- [ ] 发音评测接入与家长控制页

## 真机 UI Demo（2026-11-16）

目标：只做 **UI 设计 / 交互设计 / 实现 / 真机展示**。办公室环境不测试真实对话，**音量固定 0**，不初始化 I2S/ES8311。

| 页面 | 内容 | 交互 |
|---|---|---|
| 表盘（唯一主页面） | Fredoka 150px 圆润时间（居中偏下）、20px 中文日期、顶部绿色弧线天气、黄/橙/天蓝三色装饰环、底部 120° 绿色电量弧（⚡ + 百分比，充电只叠天蓝柔光）、Wi-Fi 仅用颜色；**无刻度、无电子宠物** | 表盘下滑打开控制中心；点按不换屏 |
| 控制中心 | 4+3 圆形磁贴：状态 / 天气 / 电量 / 手电；亮度 / 音量 / Mimi Cat（直径 84），每块一个独立手绘图标，无进度弧、无端点圆点 | 点状态/电量进状态页；点亮度/音量进独立调节页；点手电进白色手电页；点 Mimi Cat 进宠物页；点「上滑收起」或上滑返回表盘 |
| 状态页 | 显示 / 触摸 / 电池 / 音频 / 网络 / IP / 芯片 / 闪存 / 内存（只读，不再放亮度调节） | 从左往右滑或点返回圆钮回到控制中心 |
| 亮度/音量页 | 居中 44×280 科技浅绿粗轨道 + 当前值 + 0/100 刻度，无滑块圆点与说明文字 | 必须按在轨道上上下拖动才改值；其他区域从左往右滑关闭，避免误触 |
| 手电页 | 纯白满亮度 | 双击白屏退出并恢复进入前亮度 |

交互链路：表盘 → 下滑 → 控制中心 →（状态/电量 → 状态页；亮度/音量 → 独立拖动页；手电 → 白屏）→ 右滑/双击返回 → 控制中心 → 上滑 → 表盘。
**没有**每日单词页、页码点和左右点击翻页。

网络：编译时写入热点 SSID/密码（本地 `sdkconfig.local`，不入库），上电自动连接并每 15s 重试。
本机验证时热点 `AiPhone` 已开启并连接成功（表盘右上 Wi-Fi 图标变青绿）；若热点不可见，串口会打印
`reason=201` 并列出扫描到的 2.4GHz AP，便于确认热点是否开启或是否为 5GHz。

真机截图：演示构建 `CONFIG_EPW_BOOT_SHOTS=y` 会自动巡页并把 LVGL 快照按行 base64 回传串口，
主机侧用 `firmware/ui_demo/tools/decode_shots.py` 还原为 BMP。截图存放在
[resources/screenshots/](resources/screenshots/)。

构建与烧录：

```bash
cd projects/english-pet-watch/firmware/ui_demo
. <ESP-IDF>/export.sh
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

## 板子到货后的开发顺序

按顺序做，每步做完再进下一步；每步都有能看见的结果。

> 2026-11-16 已完成 **D0 到货验收 / D1 最小系统**，并提前完成一版 **真机 UI Demo**（见上一节）。
> D2 起的 OTA/音频业务代码仍待接入；UI 这版可直接用于上课演示。

| 阶段 | 做什么 | 入口 | 完成标志 |
|---|---|---|---|
| D0 到货验收 | 点件、确认板型号、量电池极性 | [03 步骤 1](docs/03-组装与调试.md) | BOM 到齐、照片存档 |
| D1 最小系统 | 只插 USB，逐个跑官方 demo | [03 步骤 2–3](docs/03-组装与调试.md) | 屏/触摸/RTC/IMU/音频全过 |
| D2 烧自研 V0.1 | menuconfig 填 Wi-Fi / 型号 / OTA 地址，build + flash | [firmware 第 3 节](firmware/README.md) | 串口出现 `获得 IP` |
| D3 起服务端 | 复制零申请 / 只接豆包 / 零成本模板 | [software/configs](software/configs/) | 日志出现 8000/8003 |
| D4 验证 OTA | 改版本号 → build → `publish_firmware.sh` → 设备重启 | [docs/06](docs/06-自研固件与OTA.md) | 版本号变化、分区切换 |
| D5 第一次对话 | **V0.2 完成音频/WS 后**：说 Hello、看 ASR 日志 | [docs/06 第六节](docs/06-自研固件与OTA.md) | 3 秒内英语回复 |
| D6 换英语老师 | 用本项目 Prompt 替换默认人设 | [04 第 2.4 节](docs/04-软件环境.md) | 英语为主、≤2 句 |
| D7 调 VAD | `min_silence_duration_ms` 700–1000 | [04 第 2.5 节](docs/04-软件环境.md) | 孩子说完不被截断 |
| D8 开唤醒词 | **V0.4**：自研唤醒或 AFE WakeNet | [docs/06](docs/06-自研固件与OTA.md) | 喊唤醒词能开始录音 |
| D9 换流式（可选） | 复制流式配置模板 | [software/configs](software/configs/) | 首响更快（`待实测`） |
| D10 装壳与续航 | 干装 → 合壳 → 放电测试 | [03 步骤 7](docs/03-组装与调试.md) | 合壳功能正常、续航回填 |

> 顺序原则：先让 V0.1 联网 + OTA 通过，再加 V0.2 音频/WS，最后才做唤醒词和外壳。每一步改完只测一件事；配置改坏用 `data/.config.yaml.bak` 回滚。

## 快速开始

```bash
# 1. 看选型与采购
open projects/english-pet-watch/docs/02-选型与BOM.md
open projects/english-pet-watch/hardware/bom/bom_english-pet-watch_v1.0.csv

# 2. 先烧真机 UI Demo（单表盘 / 控制中心 / 状态页）
open projects/english-pet-watch/firmware/README.md

# 3. 起服务端（本机 Docker）
open projects/english-pet-watch/software/README.md

# 4. 装壳与续航测试
open projects/english-pet-watch/enclosure/README.md

# 5. 上课
open projects/english-pet-watch/docs/儿童课程/README.md
python3 .trae/skills/stem-project-onboarding/scripts/build_slides.py projects/english-pet-watch/docs/儿童课程/*/课件.md --strict
```

> 只想要「能聊天的怀表」：可以买板 + 电池 + 喇叭先烧上游 xiaozhi 固件做对照；本项目走自研固件路线，V0.1 先打通 Wi-Fi 与 OTA，V0.2 再接语音。

## 目录导航

| 目录 | 内容 | 入口 |
|---|---|---|
| `docs/` | 六维工程文档 + 家长实现 + 儿童课程 | [docs/](docs/) |
| `docs/家长实现/` | 零基础家长路线、任务卡、安全采购 | [00-总纲](docs/家长实现/00-总纲与路线图.md) |
| `docs/儿童课程/` | 6 课时，一课一个自包含目录 | [课程总览](docs/儿童课程/README.md) |
| `hardware/` | BOM、板载资源、接线与供电核对 | [hardware/README.md](hardware/README.md) |
| `enclosure/` | 怀表外壳设计约束与打印参数 | [enclosure/README.md](enclosure/README.md) |
| `firmware/` | 自研固件：真机 UI Demo（`ui_demo/`）+ OTA 规划 | [firmware/README.md](firmware/README.md) |
| `software/` | 服务端、英语老师 Prompt、家长配置 | [software/README.md](software/README.md) |
| `code/` | 对话脚本、发音评测与单词本实验 | [code/README.md](code/README.md) |
| `resources/` | 图片、视频与外部资料 | [resources/README.md](resources/README.md) |

## 已知问题与踩坑

| 现象 | 原因 | 规避方式 |
|---|---|---|
| 麦克风一直「听到自己」 | 扬声器声音回灌麦克风；AEC 未启用或参考通道不对 | 用 xiaozhi 板级配置里的 `CONFIG_USE_DEVICE_AEC=y`；喇叭与麦克风开孔隔开；先戴耳机验证 |
| 唤醒词怎么喊都不灵 | 用了非官方唤醒模型，或麦克风增益太低 | MVP 先用触摸/按键说话；唤醒词用 ESP-SR 内置词 |
| 板子能亮但没声音 | ES8311 未初始化、功放使能脚（GPIO46）未拉高、喇叭接错 | 先烧官方 `06_I2SCodec` demo；查 PA 使能与喇叭阻抗 |
| 触摸坐标偏移/反了 | 1.75 与 1.75C 的触摸/复位引脚不同 | 按 `config.h` 选对 `CONFIG_BOARD_TYPE_..._1_75` 或 `_1_75C` |
| 联网后频繁断 | 2.4GHz 信号弱、天线被电池/金属壳遮挡 | 天线区域开非金属窗；电池不要贴天线；先用手机热点近距离验证 |
| 连续对话 1 小时就烫 | 屏幕高亮 + Wi-Fi + 功放同时工作 | 降亮度、限制单次 10–15 分钟、外壳留散热孔 |
| 电池鼓包 | 廉价电池/过充/挤压 | 用带保护板的电池；AXP2101 充电电流按电池规格设置；鼓包立即停用 |
| 云端费用超预期 | 实时语音模型按音频时长计费 | 先设每日时长上限；用流式管线 + 短回复；关闭后台常连 |
| 孩子越聊越晚 | 没有时长边界 | 固件层做 10–15 分钟自动「宠物困了」；家长口令解锁 |

## 许可与致谢

- 本项目文档、BOM 整理、课程与自绘结构：可自由用于教学；引用上游图片/代码时保留原作者署名与许可。
- 固件：本项目自研（`firmware/`）；上游 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 仅作板级引脚与协议参考，MIT。
- 服务端 [xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server)：MIT。
- 开发板资料：[Waveshare Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75C)。实测板为 **1.75C**，引脚以 1.75C 的 BSP/原理图为准。

## 参考资料

见 [docs/99-参考资源.md](docs/99-参考资源.md)。
