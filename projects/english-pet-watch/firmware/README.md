# firmware · 英语宠物怀表（自研固件）

> 适用版本：自研固件 V0.1 · UI Demo V0.11（ESP-IDF 6.0.2）　|　最后更新：2026-11-18
> 目标板：Waveshare ESP32-S3-Touch-AMOLED-1.75C（实测 ESP32-S3 rev v0.2 / **32MB Flash** / 8MB PSRAM）
>
> **本项目不使用 xiaozhi-esp32 自带固件。** 固件源码在本目录，自行实现 Wi-Fi、OTA 与后续语音链路；
> xiaozhi-esp32 只作为**板级引脚与协议参考**，不复制其代码。服务端复用 xiaozhi-esp32-server 的 OTA 与 WebSocket 能力。

## 收尾修复（2026-11-18）

- 移除 `main.c` 无条件启动的图标性能任务及 `ui.c` 对应测试缓存；正常开机只进入表盘，不再自动打开桌面、模拟滑动或输出测试截图。`CONFIG_EPW_BOOT_SHOTS` 保持关闭。
- 网络页打开/刷新仍扫描附近 2.4GHz Wi-Fi。若正在重连旧热点，先暂停本轮连接，扫描期间禁止重连抢占；已连接时保持连接扫描。扫描完成后恢复原自动重连策略，不擦除保存账号；选择新热点仍在获得 IP 后才覆盖保存。扫描失败与没有热点分别提示。
- 儿歌首次使用音量为 30%，以后读取独立 NVS 音量键，旧固件强制静音的配置不迁移到新键。手动设为 0 仍保留；退出音量页时保存，不逐帧写 NVS。Mimi 的 TTS 仍不外放。
- 播放页显示「音量 N% · 调节」或「静音 · 调节」，点按直接打开已有音量滑杆，返回后继续当前播放器。扬声器打开时检查设置音量的返回值，失败显示扬声器不可用并记录日志。

验证：31 项主机回归、字库覆盖与 ESP-IDF 6.0.2 编译通过；修复包已烧录至 USB 手表并通过写入哈希校验。此次 USB 串口未捕获到启动输出，未据此确认启动画面或实际外放。

手动验收（待验证）：⚠️ 使用稳定 USB 供电，先在音量页调到低音量，再点播放检查扬声器；不要播放测试音时贴近耳朵。
重启后静置至少 15 秒，应保持表盘且不自动滑动；打开网络页应列出其他热点，关闭旧热点后刷新仍应能选择新热点，连接成功后重启应自动连新热点。
儿歌点音量进入滑杆，返回应保留歌曲；确认进度推进且实际有声，手动静音应明确显示，调整后退出音量页并重启应保留设置。
本轮不修改 BOM、引脚或外壳，无需重印；真实声压、跨热点连接及断电保存仍需实机操作确认。

## 本轮修复与验证（2026-11-17）

当前源码已调整：

- **开关机**：大按钮短按保留桌面/返回；长按约 3 秒打开「滑动来关机」，从左侧圆钮拖到右端后松手才提交。短拖、纵向拖动或触点丢失回弹；「取消」或物理短按退出确认。
  电源键未松开时推迟断电，避免同一次长按重新唤醒；确认后保存暂停中的打卡、停止计时/音频/网络，再请求 AXP2101 软关机，不擦除 NVS。
  电池冷启动由 PWRON 唤醒时，固件在黑屏阶段补足约 3 秒连续长按，再显示 Mimi Logo，随后进入主表盘；提前松开会请求重新关电。硬件唤醒固定为 128ms，软件按启动时钟补足，含引导加载误差，实际按压时长 **待测量**。
  USB 插入、烧录复位和软件复位直接显示 Logo 后进入主界面；关机失败显示错误，可取消/重试，不用黑屏冒充断电。确认页只在首次使用时创建，Logo 复用现有素材，正常包不自动弹出关机页。

- 新增 **3S 英语儿歌页**：表盘**从左往右拖出**（与控制中心下拉同款快照抽屉，过 1/4 自动吸附），页内**左滑跟手回表盘**，不做第二层导航。
  首屏保留 104px 播放/暂停、72px 上一首/下一首、歌名、歌词与进度；点击底部 44px 高按钮切换本周曲库/返回播放，也可上下滑切换。
  曲库列表每行 50px；播放模式、打卡和愿望卡移到曲库内滚动查看。横向跟手拖动期间冻结动态刷新，避免进度/歌词触发额外重绘。
  曲库从家庭小站 `family/modules/nursery/` 拉取，MP3 走 HTTP 流式播放，不占 32MB Flash；
  本周曲库按 `nursery-v1` 盐值每周六 0 点自动生成，同周各设备一致。
  早/午/晚三场打卡（点按开始或暂停、长按结束）、本周星星与愿望卡都写入 NVS，重启不丢。
  音频复用现有 16kHz 单声道 I2S，与语音聊天互斥；页面 500ms 刷新进度、歌词与打卡。
- 应用桌面按 **中心九宫格坐标** 排布 Mimi / 英语儿歌 / 状态 / 网络 / 设置 / 计时器六个入口，空位保留。图标统一为彩色实心底、白色符号，Mimi 默认居中最大，越靠边越小；八方向自由拖动、松手保留位置，只有越界才回弹；各图标下显示名称，点图标或名称即可打开，无需移到中心。亮度 / 音量 / 手电 / 睡眠仍只在控制中心。
- 系统设置提供 15 秒 / 30 秒 / 1 分钟 / 2 分钟 / 5 分钟 / 常亮，写 NVS，重启生效；保存失败保留原值。
- 计时器提供 1 / 3 / 5 / 10 / 30 / 60 分钟，支持暂停、继续、重置；使用单调时钟，熄屏和切页继续计时，到点自动亮屏显示「时间到」，不外放、不驱动震动引脚。未完成计时不跨重启保存。

- 控制中心为 **3+3 六磁贴**，不设天气磁贴；图标装饰不再抢点击。
- 控制中心图标统一为 44px、3px 圆角描边；睡眠关闭时为轮廓月牙，开启时为紫色磁贴、实心月牙与勾选标记。
- 睡眠模式全局锁定背光为 **10%**，亮度页显示「睡眠锁定 10%」；切页、手电、熄屏唤醒均保持锁定，退出睡眠恢复原手动亮度。正常模式手电仍为 100%。
- 表盘天气靠近上环：只显示晴 / 多云 / 转晴 / 阴 / 雨 / 雷雨 / 雪 / 雾图标和实况温度，下方显示「萧山」，不展示来源与观测时间。固定杭州萧山区，**不获取定位**。数据来自中央气象台公开免密钥接口；实况晴雨缺失时采用当日对应昼夜时段预报，不根据温度猜天气。
  每 15 分钟请求约 10KB 数据，失败每 60s 重试；超过 2 小时清除实况温度。HTTPS 校验证书，响应上限 16KB；来源仍记录在参考资源文档。
- 黑屏点击直接恢复亮度，透明唤醒层保留屏内画面，避免黑幕逐行重绘；整次唤醒按压不触发底层页面拖动。
- 下拉折返可取消；PWR 按当前顶层页面逐层返回。
- 未校时显示「等待校时」；USB 供电和 PMU 实际充电状态分别处理。
- 校时采用阿里云 / 腾讯 / 中国 NTP 池三个来源；首次 10s 仍未成功时，后台读取公共服务固定 IP 的 HTTP Date 兜底（不依赖热点 DNS），每 20s 重试，成功后退出兜底任务。联网后保留热点 DNS，并增加公共备用 DNS；串口打印校时成功的北京时间。
- 音量首次为 30%，以后保留手动设置，滑杆允许调节 0–100；退出调节页写 NVS。Mimi 的 TTS 仍只接收显示文字，不创建播放解码器或缓冲。
- 进入宠物页不会自动录音，**左滑才请求聊天**；联网并完成服务器 hello 后才打开麦克风。
  右滑退出、PWR 返回、BOOT 退出、切页或 30s 熄屏均取消会话，后台关闭麦克风并释放编码器和网络资源。
- 删除固定台词模拟的聊天计时器，表情和回复由实际服务器事件驱动；未配置时明确提示。
- 中文 JSON 使用官方 `espressif/cjson`，支持 UTF-8 和 Unicode 转义；WebSocket 分片重组上限 8192 字节，超限整条丢弃。
- Wi-Fi 连接成功获得 IP 后才保存账号；密码键盘覆盖全部可打印 ASCII，支持 WPA2/WPA3 常见密码及 64 位十六进制 PSK。
- 16px 保留完整常用中文字库，20px 仅保留静态文案子集（静态文案变更后重新生成）。字库提取按字符串/注释顺序识别，避免 HTTPS URL 导致漏字；回归检查包含天气全部状态文案。

界面验证（2026-11-17）：23 项主机回归、字库覆盖及 ESP-IDF 编译通过。
儿歌重设计正式包已烧录并通过哈希校验，启动进入 `UI ready`；取得 455 首曲库、萧山 14.8°C（17:15 实况）。
播放器与曲库布局已截图检查；长歌名单行省略，自动巡页与截图配置已恢复关闭。
手势回归覆盖右滑打开、左滑关闭、短拖/触点丢失回弹、方向仲裁及抽屉两端边界。
回归覆盖桌面八方向拖动、双轴边界回弹、移动后的图标命中、拖动防误点、触点丢失，
以及睡眠亮度锁定、手电优先级、熄屏 0%、唤醒 10% 和退出恢复原设定。
实机截图确认开启/关闭外观，串口确认睡眠唤醒值为 10、原设定 80 保留；
截图传输期间设备会自动熄屏，因此滑杆和手电亮度优先级由主机回归验证。
10% 为驱动设置值，实际屏幕亮度未作仪器测量。
截图：[关闭](ui_demo/screenshots/center_sleep_off.png)、[开启](ui_demo/screenshots/center_sleep_on.png)、[睡眠表盘](ui_demo/screenshots/home_sleep_ten.png)。

桌面实机截图：[中心布局](ui_demo/screenshots/apps_grid.png) / [计时器](ui_demo/screenshots/timer_presets.png)。
手动验收：PWR 进入桌面，分别横拖、竖拖、斜拖后点开各应用；拖过边界松手应回弹；未越界松手保留原位，
下一次点按可直接打开任意可见图标或其名称，拖动结束本身不得打开应用。再次进入桌面应保留位置。
此前未显示图标下方名称的版本优化实测（当前文字版拖动性能待测）：同一路径 48 帧，旧四图标平均重绘 102.23ms，新六图标 22.32ms，P95 105.49 → 29.18ms。
测量包含布局和强制刷新，不代表整机稳定帧率。此前 48 档 RGB565 缓存共 844800 字节。当前改为每图标一张原图和一张可复用输出缓冲，
合计 345600 字节，指定 PSRAM；按 1px 连续尺寸双线性重采样，LVGL 按 1:1 绘制，拖动不分配图片缓冲。
名称保持固定字号、无需居中选中；缓存不足回退到单图缩放 / 矢量绘制。
手动验证：设置熄屏时间后重启确认保留；计时开始后离开页面、熄屏，时间到应直接亮屏提示，
暂停期间剩余时间不减，重置后无旧提醒，PWR 返回后仍可再次进入。

本轮正式固件已烧录并通过哈希校验，串口进入 `UI ready`，无启动重启。
到点唤醒使用临时 1 秒期限验收通过，诊断入口已移除；NVS 断电重启保存、六种预设的实机长时计时待验证。
热点恢复后已取得萧山 14.8°C（16:25 实况）；失联期间无天气是热点未连接（reason=201）。


Mimi 重启定位（2026-11-17）：用户再次左滑时抓到 `A stack overflow in task epw_voice`，
发生在麦克风启动后进入 Opus 编码的阶段。原任务只有 6144 字节栈；按托管 `esp_audio_codec/README.md`
建议改为 40KB，仍显式放 PSRAM（比原来增加 34KB，不挤占内部 DMA 内存）。删除重复的超时分支。
真机合成 PCM 编码测试覆盖关闭/重新创建编码器三轮、每轮 50 帧，未开启麦克风或输出设备；
当前栈最小余量约 19KB，编码峰值约 22KB。真实会话也在首次编码后记录栈余量，便于跟踪。
重复左滑保持当前会话，取消后才允许新建；真实麦克风连续会话仍需用户操作复验。

Mimi 重启复查（2026-11-17）：串口捕获 `A stack overflow in task epw_voice`，
当时麦克风刚启动；原任务只有 6144 字节，Opus 编码栈不足。按组件说明改为 40KB PSRAM 栈，
合成 PCM 真机连续 3 轮开关编码器、每轮 50 帧，最低剩余 19048 字节，未溢出。
该测试不打开麦克风、不联网传音频、不播放声音；正式会话首次编码成功也记录最低剩余栈字节数。
重复左滑保持当前会话，不重复分配；主机回归覆盖此行为、任意菜单位置点击和名称命中。
菜单截图：[名称在图标下方](ui_demo/screenshots/apps_labels.png)。临时合成测试与截图任务已移除。

此前语音修复曾将开机输出设为 0；2026-11-18 已改为首次 30%、保留手动音量。Mimi 空白区也可左滑聊天，
ASR 文本、豆包回复、表情和 TTS 事件联动；TTS Opus 分片只统计接收，不开启扬声器或解码缓冲。
回复期间排空麦克风旧帧，再进入下一轮聆听；hello 20s / 回复无进展 45s 超时会结束会话。
控制任务栈和音频临时缓冲放 PSRAM。退出/熄屏仍取消会话，TTS 结束保留回复文字。
服务端静音链路检查（VAD → FunASR → 豆包 LLM → EdgeTTS → Opus）已连续两轮通过，
仅在服务器内存解码合成音频，不访问声卡，不向手表发送测试音频。
脚本：`software/tools/check_voice_chain.py`（从项目目录；需在已配置服务端环境运行）。
手表真实麦克风端到端录音识别、连续拖动手感与儿歌连续打卡仍 **待验证**；本轮不执行外放或自动播放验收。

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
天气缺字复查：修复提取器把 URL 中 `//` 当注释的问题，补齐 10 个漏掉的静态汉字，
8 项主机回归及字库覆盖检查通过，修复固件已烧录且写入校验通过；串口确认 UI 启动、校时成功。
天气显示的实际排版、断网两小时后的失效提示及长期更新仍待真机观察。

本次天气布局、下拉与唤醒验证：

- 天气上移，显示真实雨天图标 / 温度 / 萧山；[实机截图](../resources/screenshots/home-weather-2026-11-17.png)。
- 临时诊断固件自动执行 4 轮展开/收起，监听 LVGL `RENDER_START/READY`：旧版 18 帧累计 4,914,658µs，均值约 273ms；预热双快照版 85 帧累计 3,431,646µs，均值约 40ms。Wi-Fi 扫描同时测试为 80 帧 / 3,064,101µs，约 38ms，未见 DMA 分配错误。
- 此统计包含开合收尾和期间刷新，不是触摸采样率或稳定帧率。恢复实时表盘仍有单次长帧（本轮最大 234ms，扫描时 282ms）；连续拖动手感与长期运行仍待人工确认。
- 11 项主机回归及字库检查通过；覆盖缓存预热/复用、拖动冻结、申请失败回退、唤醒吞掉整次按压、睡眠背光锁定，以及天气实况优先 / 同日昼夜预报回退。
- 3S 英语儿歌页已通过 `idf.py build`（ESP-IDF 6.0.2，ESP32-S3，镜像约 4.4MB，app 分区剩余 48%）；MP3 解码器在 `nursery_init()` 里按官方测试用例同时注册 `esp_audio_dec_register_default()` + `esp_audio_simple_dec_register_default()`——`esp_audio_simple_dec_open()` 走的是“通用解码表 + 简单解码表”两层查找，只注册其中一层会出现「MP3 解码器不可用」。两项都按 menuconfig 裁剪，只链入已启用的解码器。曲库/歌词/MP3 Range 资源已用 HTTP 探测可达，真机外放、跟手抽屉手感与连续打卡**待验证**。
- 儿歌页滑入改为与控制中心下拉同款：空闲时预渲染整页 RGB565 快照（曲库就绪、页面关闭时各重拍一次），右滑只搬运这张位图；表盘同时冻结成快照。横滑事件注册在下拉回调之前，斜向滑动时优先判定横滑，避免被 `home_pull_cb` 抢走。慢滑额外用 `LV_EVENT_GESTURE` 兜底。
- 正式固件已烧录并通过写入校验；串口确认 `UI ready`、约 5s 校时成功、约 15s 获取萧山 14.0°C / 雨图标（观测时间 12:15）。临时计时和截图调用已移除，自动巡页关闭，不会开机自动操作界面。
- 天气 HTTPS 偶发/持续取不到：串口为 `mbedtls_ssl_setup returned -0x008D`（PSA_ERROR_INSUFFICIENT_MEMORY）。根因是 `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y` 强制 TLS 的 16KB 输入 / 4KB 输出缓冲只从内部 RAM 分配，表盘绘制缓冲与音频占用后不足；已改为 `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`（mbedTLS 缓冲走 PSRAM），并写入 `sdkconfig.defaults`。修复后烧录验证：串口输出 `epw-weather: Xiaoshan 14.7 C, observed 14:55, icon=5`；13 项主机回归通过。失败分支现在会打印内部 RAM / PSRAM 剩余字节。
- 天气图标显示成问号：实况 `weather.info` 为 `-` 时需要回退到当日预报，而气象台会把当天已过或未发布的时段置为 `9999`（当日 17:20 白天段即为 `9999`）。`forecast_kind()` 现在按「当天当前时段 → 当天另一时段 → 明天当前时段 → 明天另一时段」取第一个有效预报，不再把 `9999` 当作“未知”。烧录后串口确认 `icon=5`（雨天），22 项主机回归通过。

开关机开发验证（2026-11-17）：26 项主机回归、中文字体覆盖和正式包编译通过；
已烧录并通过写入哈希校验，串口确认 AXP2101 `0x4A`、`UI ready`、曲库和天气恢复。
[开机 Logo 截图](ui_demo/screenshots/boot_mimi.png) / [关机确认截图](ui_demo/screenshots/power_confirm.png)。
截图临时调用已移除，`CONFIG_EPW_BOOT_SHOTS` 关闭；未自动触发真实断电，未进行播放测试。

开关机人工验收（**待验证**）：

- ⚠️ 先确认电池连接及 USB 供电状态；仅烧录操作沿用下方 USB 安全上电流程。电池断电复验时先正常拔出 USB，切勿短接电池或 PMU 引脚。
- 长按大按钮约 3 秒，只出现确认页；保持按住不会绕过滑块直接关机。短拖/拖出滑轨后松手应回弹，取消后回到原页面。
- 滑块拖到底松手，并松开大按钮：屏幕/系统电源应关闭。再次长按约 3 秒应出现 Mimi，然后是主表盘；提前松开保持关机。
- USB 供电下真实切断效果、断电后连续长按开机、短按回退及实际按压时长均需手动检查；本轮不自动断电，避免设备关机后无法代按物理键唤醒。

其余真机交互与语音验收 **待验证**：

1. ⚠️ 首次烧录按下文流程断开电池，仅用 USB 供电；核对板型、32MB Flash 和串口。
2. 点六个磁贴的图标中心及空白处，再从图标上拖动收起；下拉后拖回起点应取消。
3. 桌面 → 状态 → PWR 应回桌面；网络密码页应逐层返回。音量开机为 0，允许用户手动调整；本轮不执行播放测试。
4. 宠物页左滑后观察 `microphone active`，服务端应收到真实语音帧；退出/熄屏后应出现
   `voice session stopped`，服务端不再收到新的录音帧。断网和服务器不可达时不应启动麦克风。
5. 中文正常显示；没有网络校时时显示等待状态，不出现演示日期或天气；联网校时后应出现天气图标、萧山实况温度和城市名；来源及观测时间仅保留在串口与文档。
6. 接电池前核对极性与电压，由家长操作；对照 PMU 电压与实际电池电压校准电量判断。
   分别验证 USB 无电池、充电中、已充满三种状态；电流、帧率、内存峰值和续航改善均待验证。

以下截图与真机记录是修复前的历史版本，布局和语音行为以本节为准。

## 0. 当前可用：真机 UI Demo（2026-11-16）

`ui_demo/` 是独立可编译的 ESP-IDF 工程，已在 **Waveshare ESP32-S3-Touch-AMOLED-1.75C** 上跑通：

| 项 | 值 |
|---|---|
| 屏幕 | 466×466 圆屏，CO5300，QSPI，亮度可调 |
| 触摸 | CST9217，I2C，单点；点按 / 滑动 / 长按逻辑已实现（现场操作时串口打印 `gesture/tap/long press`） |
| 页面 | **单表盘**（无刻度 / Fredoka 150px 大号时间 / 中文日期 / 顶部天气 / 三色装饰环 / 底部绿色圆弧电量）；表盘上空下滑 → **控制中心**（3+3 手绘图标磁贴）、**左滑 → 3S 英语儿歌页**；控制中心点状态/电量 → **状态页**；点亮度/音量 → 独立调节页；手电 → 白屏；`PWR` 短按仍可进二维气泡应用桌面，`BOOT` 短按进宠物页。**无每日单词页、无页码点、无点击换屏** |
| 宠物 | 独立宠物页 **Mimi Cat · 3D 正脸猫**：8 张 Fluent Emoji 3D（MIT）+ 9 种情绪、对话气泡、纯手势互动；本地情景引擎按电量/充电/网络/时间换状态 |
| 物理键 | `BOOT`（GPIO0）短按 → 宠物页；`PWR`（AXP2101）短按 → 应用桌面；长按 PWR 仍由 PMU 关机 |
| 豆包桥接 | 设备轮询 `EPW_PET_BRIDGE_URL` 的轻量 JSON（`emotion` + `text`），云 Key 只在服务端；未配置时只用本地情景引擎 |
| 音频 | 录音与儿歌播放共用 16kHz 单声道 I2S，**互斥**：语音聊天只上行、扬声器静音；儿歌页解码 MP3 后外放。音量页同时调节儿歌音量 |
| 网络 | 编译期写入 2.4GHz 热点，上电自动连接；联网后 SNTP 同步北京时间，未校时时显示等待状态 |
| 构建 | ESP-IDF **6.0.2**，LVGL 9.5.0，Waveshare BSP 3.0.0 |

```bash
cd projects/english-pet-watch/firmware/ui_demo
. <ESP-IDF>/export.sh                # 本机 /Users/eatcrow/esp/esp-idf-v6.0.2

# 首次：复制模板并填写本机配置（epw.env 已被 gitignore）
cp ../epw.env.example ../epw.env && "${EDITOR:-vi}" ../epw.env
# 生成两个工程的 sdkconfig.local
./tools/sync_env.sh

SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.local" idf.py build
# 后续直接增量编译
idf.py build                         # 产物 build/epw_ui_demo.bin
idf.py -p /dev/cu.usbmodem* flash monitor
```

Wi-Fi、语音服务端、曲库、OTA 与发布主机等本机配置统一放 `firmware/epw.env`（**不入库**），
`tools/sync_env.sh` 会生成 `sdkconfig.local`。字段说明见 `firmware/epw.env.example`。
也可以 `idf.py menuconfig` → `English Pet Watch UI Demo` 里临时填写。

> `main/CMakeLists.txt` 在配置阶段把托管 Waveshare BSP 改为 **50 行、内部 RAM、单缓冲**（约 46KB，启动时分配）。
> 原先 PSRAM 双缓冲需要临时 DMA 反弹内存，扫描时易碎片化；缩为 10 行又显著增加提交/调度开销。
> 预留内部缓冲避免运行中申请大块 DMA 内存。拖动期间表盘和控制中心均使用 RGB565 快照，
> 表盘新增约 424KiB PSRAM 缓存，时间、电量、网络、天气或睡眠样式改变后，在无触摸的空闲时段预热并复用；拖动入口不截图，结束后恢复实时控件。分配失败回退实时绘制。
> `managed_components` 重新拉取后补丁仍会自动生效。


2026-11-16 真机验证（`/dev/cu.usbmodem1101`，UI Demo V0.11）：

- 单表盘：150px Fredoka SemiBold 圆润数字时间（垂直居中、整体下移）+ 20px 中文日期；顶部太阳 / 24° / 晴贴着上沿弧线；右上 Wi-Fi 图标只用颜色表达状态；**无刻度环、无电子宠物**。
- 外圈：直径 446、线宽 8 的亮黄 → 橙 → 天蓝三色装饰环常驻；底部再收一圈 120° 电量弧（⚡ + 百分比），主色固定绿色 `#2EE66B`，插 USB 时只多一圈天蓝柔光。两种模式截图见 `resources/screenshots/home*.png`。
- 控制中心：表盘下滑进入；4+3 小磁贴（状态 / 天气 / 电量 / 手电 + 亮度 / 音量 / Mimi Cat），每块一个独立手绘图标，无进度弧与端点圆点；上滑或点「上滑收起」返回表盘。
- 状态页：控制中心点「状态 / 电量」进入；运行时读取显示 / 触摸 / 电池 / 音频 / 网络 / IP / 芯片 / 闪存 / 内存；**从左往右滑**或点返回圆钮回到控制中心。
- 亮度 / 音量页：点对应磁贴进入；居中 44×280 科技浅绿粗轨道，只显示当前值与 100/0 刻度；必须按在轨道上拖动，左→右滑关闭。
- 手电页：点「手电」进入，纯白满亮度，双击退出并恢复原亮度。
- 宠物页：控制中心 Mimi Cat 磁贴 / 应用桌面「宠物」/ `BOOT` 短按进入；3D 正脸猫 Mimi 的 9 种情绪、气泡台词与纯手势互动。
- 应用桌面：`PWR` 短按进入（备用入口）；2×2 图标（Mimi / 3S 英语儿歌 / 状态 / 网络）可长按拖动换位，布局写 NVS。
- 已删除每日单词页、页码点与左右点击翻页；表盘点击不再换屏。
- Mimi Cat 磁贴与宠物页均使用 Microsoft Fluent Emoji 3D（MIT）素材，宠物页为 8 张正脸猫。
- 当前热点 `<你的热点>` 已连接成功；若热点不可见，串口会打印 `reason=201` 并列出扫描到的 2.4GHz AP。
- 2026-11-16 全量编译通过（`idf.py build`，零警告），镜像 `build/epw_ui_demo.bin` 约 4.7MB（app 分区剩余 44%）。

### 3S 英语儿歌页

入口：**表盘左滑**；页内**右滑**回表盘（不叠返回按钮）。页面内上滑切换到「本周曲库」卡，下滑回播放器卡。

| 能力 | 实现 |
|---|---|
| 曲库 | 启动后后台拉取 `http://<曲库服务器>:8083/family/modules/nursery/data/songs.js`（455 首，最多解析 512 首），不写入 Flash |
| 播放 | 每首 MP3 通过 HTTP Range 流式读取 → `esp_audio_simple_dec` 解码 → 44.1kHz 立体声线性重采样到 16kHz 单声道 → ES8311 外放 |
| 播放模式 | 列表循环 / 单曲循环 / 随机；上一首、下一首、播放/暂停 |
| 本周曲库 | 以 `nursery-v1` 为盐值按周生成 4–5 首；每周六 0 点自动刷新，同一周各设备结果一致；长按曲目可换歌（打卡中拒绝） |
| 打卡 | 早 / 午 / 晚三场，目标 40 / 10 / 40 分钟；点按开始或暂停，长按结束；早+晚都完成记 1 星，集满 5 星兑换 1 张愿望卡 |
| 持久化 | 打卡记录、本周曲库、音量写 NVS，重启不丢 |
| 互斥 | 儿歌播放与语音聊天共用 I2S，同一时间只允许一个方向；进入儿歌页会先停掉录音 |

歌词按 `lyrics/<id>.json` 拉取并缓存最近一首；播放失败的短状态显示在播放器卡片底部。

调试接口：`ui_demo_open_nursery()` / `ui_demo_close_nursery()`；截图任务用 `ui_debug_sleep_check(on)` 切换睡眠表盘。


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

配置（写进 `firmware/epw.env`，由 `tools/sync_env.sh` 生成 `sdkconfig.local`；留空 = 关闭语音链路）：

```text
EPW_VOICE_WS_URL='ws://<服务器IP>:8818/xiaozhi/v1/'
EPW_VOICE_TOKEN=''
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
├── epw.env.example           # 本机配置模板（Wi-Fi / 语音 / 曲库 / OTA / 发布主机）
├── epw.env                   # 本机真实配置（不入库，由模板复制填写）
├── ui_demo/                  # ✅ 当前真机可跑：表盘 + 应用桌面 + 宠物页 + 控制中心 + 状态页 + Wi-Fi
│   ├── CMakeLists.txt
│   ├── partitions.csv        # 8MB factory app + 7MB SPIFFS
│   ├── sdkconfig.defaults    # 32MB Flash / 8MB PSRAM / LVGL 9.5
│   ├── sdkconfig.local       # 由 tools/sync_env.sh 从 epw.env 生成（不入库）
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
│       ├── sync_env.sh       # epw.env → sdkconfig.local
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

# 2. 发布到服务器 data/bin（主机/目录读 firmware/epw.env 的 EPW_SERVER_HOST / EPW_SERVER_BIN_DIR）
./tools/publish_firmware.sh build/english_pet_watch.bin 0.2.0

# 3. 板子还没到？先用电脑模拟设备（URL 读 epw.env 的 EPW_OTA_URL，也可显式传参）
./tools/ota_probe.sh
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
