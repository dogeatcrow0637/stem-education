# 02 · 选型与 BOM

> 适用版本：方案 v1.0　|　最后更新：2026-11-26　|　前置条件：读完 `01-项目简介.md`
> 价格均为 2026-11-13 的**参考价**，以下单页面为准；未实测项标 `待验证`。

## 一、版本 / 型号对比

### 1.1 开发板

| 参数 | A：Waveshare ESP32-S3-Touch-AMOLED-1.75（推荐） | B：同尺寸 LCD 圆屏方案 |
|---|---|---|
| 屏幕 | 1.75" AMOLED 466×466，CO5300，QSPI | 1.75" 圆屏 LCD（型号待确认） |
| 触摸 | CST9217 电容触摸 | 视型号 |
| 音频 | ES8311 + ES7210、双麦、AEC 参考 | 视型号，常需外接 |
| 主控 / 内存 | ESP32-S3R8，**32MB Flash** / 8MB PSRAM（1.75C 实机实测；早期资料写 16MB） | ESP32-S3，容量视型号 |
| 电源管理 | AXP2101 + MX1.25 电池口 | 视型号 |
| 形态适配 | 单圆屏，最适合怀表 | 单圆屏，较厚 |
| 价格 | ￥220–300（参考） | ￥150–220（待确认） |
| 结论 | **首选**：自研固件 + 复用 xiaozhi-server；板级引脚参考上游 | 预算方案：需自己补音频与板级配置 |

> ⚠️ 该板有 **1.75** 与 **1.75C** 两个版本，`config.h` 里 MCLK/LCD_RST/触摸复位引脚不同。下单前确认丝印和商品标题；烧录时选对 `CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_AMOLED_1_75` 或 `..._1_75C`。两者功能差异以 Waveshare 页面为准（`待确认`）。

### 1.2 语音方案

| 方案 | 链路 | 首响延迟 | 成本 | 可控性 | 建议 |
|---|---|---|---|---|---|
| ① 流式管线 | 流式 ASR → LLM → 流式 TTS | 1–2s | 低–中 | 高，可逐段换供应商 | **MVP 首选** |
| ② 实时语音模型 | 端到端语音大模型（豆包实时语音、Qwen-Omni、OpenAI Realtime 等） | 0.5–1.2s | 中–高 | 低，绑定供应商 | V2 追求自然度时再上 |
| ③ 全本地 | 本地唤醒 + 本地 ASR + 本地小模型 + 本地 TTS | — | 低 | 高 | **不可行**：ESP32-S3 跑不动可用 LLM |

### 1.3 云端组件候选

| 组件 | 候选 | 说明 |
|---|---|---|
| ASR | 火山引擎、阿里云、腾讯云、讯飞、本地 FunASR/SenseVoice | 中文识别优先；英语口音识别要实测 |
| LLM | 豆包、DeepSeek、Qwen、GLM | 英语教学用「便宜 + 稳定 + 可关联网」的模型即可 |
| TTS | 火山引擎、阿里云、腾讯云、Edge TTS、Azure | 选**儿童友好、语速可调**的英语音色 |
| 发音评测 | 讯飞语音评测、腾讯云口语评测、Azure Pronunciation Assessment | 可选 V2；按次计费，先小流量试 |
| 服务端 | [xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server)（MIT） | 支持 WebSocket / MQTT+UDP，本机 Docker 可跑 |

> 供应商价格与免费额度经常变化，本文不写死单价。先各充最小额度，跑一周再决定。

### 1.4 语音组件逐项选型（VAD / ASR / LLM / TTS / Memory / Intent / Prompt）

**五条原则**（后面每个模块都按它选）：

1. **先免费闭环**：FunASR + glm-4-flash + EdgeTTS + mem_local_short，不花一分钱先跑通。
2. **再一次换一项**：换模块前先录一段基线，换完只对比这一项，别同时动三个。
3. **儿童英语优先**：ASR 要中英混识，TTS 要英语音色且能放慢，LLM 要短句、低温度。
4. **能本地就本地**：VAD、记忆、ASR 优先本地，少传孩子声音出去。
5. **只改 `data/.config.yaml`**：不动 `config.yaml`；启用智控台后以网页配置为准。

#### ⓪ 唤醒词（Wake Word）——先决定「怎么叫醒它」

| 项 | 内容 |
|---|---|
| 一句话理解 | 像叫小名：只有喊对名字，它才从待机进入对话 |
| 与 VAD 的区别 | 唤醒词判断「是不是在叫它」；VAD 判断「这句话说完了没有」。两个都要，但先后不同 |
| 配置位置 | **固件** `menuconfig` 的 `Wake Word Implementation Type`（Kconfig `WAKE_WORD_TYPE`），不是服务端 YAML |
| 本板选择 | `USE_AFE_WAKE_WORD`（S3 + PSRAM，WakeNet + AFE，顺带做 AEC/VAD） |
| 默认唤醒词 | 「你好小智」，ESP-SR 内置 WakeNet 模型，本地运行、免费 |
| 换内置词 | `ESP Speech Recognition → use wakenet → Select wake words`，约 26 个免费词可选 |
| 自定义词 | `USE_CUSTOM_WAKE_WORD`（MultiNet），填拼音如 `ni hao xiao zhi`；阈值 1–99，越小越灵敏（默认 20） |
| 代价 | 麦克风常开 → 续航下降（`待实测`）；环境吵会误唤醒 |
| 建议路线 | MVP 先用触摸/按键 → 跑通后开唤醒词 → 最后才考虑自定义「Hi Pico」 |

> ⚠️ 改唤醒词必须重新 `idf.py build` 并烧录；只改 `sdkconfig` 不重编不生效。自定义唤醒词只支持 ESP32-S3 / P4 / S31 且必须有 PSRAM。

#### ① VAD（语音活动检测）——决定「你什么时候说完」

| 项 | 内容 |
|---|---|
| 配置键 | `VAD: SileroVAD`（`type: silero`） |
| 一句话理解 | 像声控录音笔：有人说话才录，安静就停；它只判断「有没有人声」，不知道你说的是什么 |
| 作用 | 判断有没有人声、说完没有；决定录音何时截断送 ASR |
| 关键参数 | `threshold: 0.5`、`threshold_low: 0.3`、`min_silence_duration_ms: 700–1000` |
| 为什么选它 | 本地运行、免费、无需密钥；服务端默认就是它 |
| 什么时候换 | 基本不用换；嘈杂环境先调参数，不要换模块 |
| 验收 | 说「Hello, my name is…」停顿 1 秒，日志出现一整句，而不是被切成两段 |
| 常见坑 | 值太小 → 孩子换气就被打断（抢话）；值太大 → 要等 1 秒以上才回应 |

#### ② ASR（语音识别）——把声音变成文字

| 候选键名 | type | 位置 | 计费 | 适合 |
|---|---|---|---|---|
| `FunASR` | `fun_local` | 本地 | 免费 | **MVP 默认**；SenseVoiceSmall，中英日韩粤 |
| `FunASRServer` | `fun_server` | 本地容器 | 免费 | 想独立部署/多设备共用 |
| `SherpaASR` / `SherpaParaformerASR` | `sherpa_onnx_local` | 本地 | 免费 | 低性能机器 |
| `DoubaoStreamASR` | `doubao_stream` | 云端流式 | 按小时 | **升级首选**；大模型 ASR，中英混识好 |
| `DoubaoStreamASRV2` | `doubao_stream` | 云端流式 | 按小时（更便宜） | 高并发/省钱 |
| `XunfeiStreamASR` | `xunfei_stream` | 云端流式 | 按次/按时 | 国内稳定，`language: zh_cn / en_us` |
| `AliyunStreamASR` / `AliyunBLStreamASR` | `aliyun_stream` / `aliyunbl_stream` | 云端流式 | 按秒 | 阿里生态 |
| `Qwen3ASRFlash` | `qwen3_asr_flash` | 云端 | 按次 | 自带语种检测 `enable_lid` |
| `TencentASR` / `BaiduASR` / `AliyunASR` | 各云 | 云端非流式 | 按次 | 便宜但首响高，不适合对话 |
| `VoskASR` | `vosk` | 本地 | 免费 | 完全离线，准确率一般 |
| `OpenaiASR` / `GroqASR` | `openai` | 境外 | 按次 | 需要境外网络，本项目不选 |

- **MVP 选择**：`FunASR`（免费）→ 升级 `DoubaoStreamASR` 或 `XunfeiStreamASR`。
- **为什么**：免费档先验证链路；升级要的是「流式 + 中英混识 + 儿童口音容忍」。
- **关键配置**：`FunASR.language: auto`（自动语种）；若中文短句被误判成韩文，改 `zh` 或 `zh,en`。
- **英语专项**：`XunfeiStreamASR.language: en_us`；`Qwen3ASRFlash.enable_lid: true`。
- **验收**：说「Hello, I want an apple」→ 服务端日志文本与口述一致（大小写/标点不计）。
- **常见坑**：儿童口音识别差 → 先用「慢速、短句」测试，必要时换云 ASR；热词表用来补人名/单词。

#### ③ LLM（大模型）——决定「它怎么想、怎么回答」

| 候选键名 | 默认模型 | 计费 | 适合 |
|---|---|---|---|
| `ChatGLMLLM` | `glm-4-flash` | 免费档 | **MVP 默认**；支持 function_call |
| `DoubaoLLM` | `doubao-seed-2-0-lite-260215` | 低 | **稳定首选**；function_call 稳 |
| `AliLLM` | `qwen-flash` | 低 | 阿里生态，中文好 |
| `DeepSeekLLM` | `deepseek-v4-flash` | 低 | 便宜、指令跟随好 |
| `OllamaLLM` / `LMStudioLLM` / `XinferenceLLM` | 本地模型 | 电费 | 有显卡/想全离线 |
| `DifyLLM` / `CozeLLM` / `FastgptLLM` | 外部工作流 | 各自计费 | **Prompt 在平台里配，`config.yaml` 的 prompt 不生效** |
| `GeminiLLM` | `gemini-2.0-flash` | 按量 | 需代理，本项目不采用 |

- **MVP 选择**：`ChatGLMLLM`（免费）→ `DoubaoLLM` 或 `AliLLM`（稳定、便宜）。
- **完全不想申请 Key**：用 `OllamaLLM`（本地 `qwen2.5:3b`），ASR 用 FunASR、TTS 用 EdgeTTS，整条链路零 Key、零费用；代价是电脑要常开、首响更慢、英语质量一般（`待实测`）。模板见 [`../software/configs/local-ollama.config.yaml`](../software/configs/local-ollama.config.yaml)。
- **豆包免费额度**：`DoubaoLLM` 用 `doubao-seed-2-0-lite-260215` 有免费 token 额度，但同样要注册火山引擎、实名并开通服务，流程并不比智谱简单；它的优势是 `function_call` 更稳。
- **参数建议**：`temperature: 0.3–0.5`（教学要稳）、`max_tokens: 200–300`（强制短回答）。
- **为什么不用最强模型**：英语陪练只需要「短、慢、不跑题」，推理能力过剩只增加费用和延迟。
- **验收**：问「What's this?」→ 回答 ≤2 句、以英语为主、不写小作文。
- **常见坑**：回答越来越长 → 降 `max_tokens` 并改 Prompt；突然长篇中文 → Prompt 加「每次不超过 2 句」；不支持 function_call → Intent 降级 `nointent`。

> **境外模型统一不采用**：Gemini（含 Live 系列）、OpenAI、Claude 在当前家庭网络下都要代理，且免费层通常会把内容用于产品改进。孩子的声音与对话不适合走这条链路，本项目只选国内可直连的模型。

#### ④ TTS（语音合成）——把文字变回声音

| 候选键名 | type | 计费 | 特点 |
|---|---|---|---|
| `EdgeTTS` | `edge` | 免费 | **MVP 默认**；`voice` 可换英语音色，`language` 可指定 |
| `DoubaoTTS` | `doubao` | 按字符 | 音色多，普通 HTTP 流式 |
| `HuoshanDoubleStreamTTS` | `huoshan_double_stream` | 按字符 | **升级首选**；双向流式、首响低 |
| 其他云 TTS | 各云 | 按字符 | 以 `config.yaml` 的 TTS 段为准 |

- **MVP 选择**：`EdgeTTS` → `HuoshanDoubleStreamTTS`。
- **英语专项**：`EdgeTTS.voice` 改成英语儿童音色（音色名以 Edge TTS 当期列表为准，先在服务端试听）；`language: "英语"`。火山在控制台选英语音色，`speech_rate` 设为负值放慢。
- **验收**：回复是英语、语速慢、尾音不吞；连续 10 轮不报错。
- **常见坑**：中文音色念英语口音怪 → 换英语音色；免费并发低（火山免费档约 2 并发）→ 报 TTS 错误时买最小套餐；`tts_timeout: 15` 超时 → 换流式。

#### ⑤ Memory（记忆）——决定「它记不记得昨天」

| 候选键名 | type | 位置 | 计费 | 适合 |
|---|---|---|---|---|
| `nomem` | `nomem` | — | 免费 | 不记忆；先验证链路 |
| `mem_local_short` | `mem_local_short` | 本地 | 免费 | **MVP 默认**；服务端 LLM 总结，短期记忆 |
| `powermem` | `powermem` | 本地（sqlite/OB） | 免费（用免费 LLM） | **单词本/长期复习首选**；可开用户画像 |
| `mem0ai` | `mem0ai` | 云端 | 1000 次/月免费 | 跨设备长期记忆，注意隐私 |

- **MVP 选择**：`mem_local_short`；要做「昨天学的单词」再升级 `powermem`。
- **为什么**：孩子的语音与学习记录优先留在本机；`powermem` 用 sqlite + 免费 LLM 可零成本。
- **关键配置**：`mem_local_short.llm` 指向便宜/免费模型（只申请豆包就填 `DoubaoLLM`，别用贵模型）；`powermem.enable_user_profile: true`。
- **验收**：同一会话里问「我刚才说了哪个单词」能答对；升级后隔天问「昨天学了什么」能复述。
- **常见坑**：一上来用云端记忆 → 隐私与费用；记忆总结占用对话模型 → 单独指定免费 LLM。

#### ⑥ Intent（意图/工具调用）——决定「能不能控制设备」

| 候选键名 | type | 速度 | 适合 |
|---|---|---|---|
| `nointent` | `nointent` | 最快 | 只要聊天，不要工具 |
| `function_call` | `function_call` | 快（按需） | **MVP 默认**；需要退出/音量/切角色 |
| `intent_llm` | `intent_llm` | 慢（串行前置） | LLM 不支持 function_call 时兜底 |

- **MVP 选择**：`function_call`，但**只加载必要插件**。默认 `functions` 里有 `web_search`、`get_weather`、新闻、音乐，儿童设备建议删到只留 `change_role` 和内置的退出。
- **为什么**：插件越多，LLM 越容易选错工具、越容易跑题。
- **验收**：说「退出」能结束会话；说「换老师」能切角色；不加载搜索时不会突然联网。
- **常见坑**：所选 LLM 不支持 function_call → 降级 `nointent`；加载 `play_music` 与 `hass_play_music` 两个音乐插件 → 二选一。

#### ⑦ Prompt（人设/教学规则）——决定「它像不像英语老师」

| 项 | 内容 |
|---|---|
| 配置位置 | 顶层 `prompt`（也可用 `prompt_template`） |
| MVP 文案 | 本项目 [`../software/prompts/english-teacher.md`](../software/prompts/english-teacher.md) |
| 配套项 | `system_error_response`（出错时说什么）、`end_prompt`（结束语，可关） |
| 为什么必须换 | 服务端默认 Prompt 是「台湾腔女友」，给孩子用不合适 |
| 验收 | 第一句英语 + 一句中文提示；每轮 ≤2 句；每 3 轮复习；不聊暴力/成人话题 |
| 常见坑 | 启用智控台后文件里的 `prompt` 不生效；Prompt 太长会挤占上下文；一次改太多条无法判断哪条有效 |

**替换任一模块的三步法**：

1. 改 `selected_module` 里的名字 → 2. 在 `data/.config.yaml` 填该模块的必填字段 → 3. `docker compose restart` 后用同一句话复测，对比基线。

> 配置入口：服务端默认读 `main/xiaozhi-server/config.yaml`，实际覆盖写 `data/.config.yaml`；**使用智控台时文件里的这些配置不生效**，要到网页里按智能体改。可直接复制的完整 YAML 与逐模块验证表见 `04-软件环境.md` 的 2.5。
> 上表价格为量级判断，不写死单价；云厂商价格与免费额度以官网当期页面为准。

### 1.5 免费但要「不那么死板」：ASR / TTS 备选

先分清三件事：**ASR 决定「听没听对」**（听错就答非所问，显得笨）；**TTS 决定「声音自不自然」**（死板多来自音色和语调）；**Prompt 决定「说话像不像人」**（短句、语气词、反问，往往比换引擎更有效）。

> **「占本机算力」指跑服务端的那台电脑，不是怀表。** 怀表里的 ESP32-S3 只做唤醒、录音、放音、屏幕表情；VAD/ASR/TTS 都在服务端或云端。S3 实测 32MB Flash / 8MB PSRAM，仍跑不了 SenseVoice、Whisper 这类模型。
>
> | 环节 | 跑在哪 | 说明 |
> |---|---|---|
> | 唤醒 / AEC / 降噪 | ESP32-S3（板端） | ESP-SR 小模型，为单片机优化过 |
> | 录音 / 播放 / Opus | ESP32-S3（板端） | 只做音频搬运 |
> | VAD / ASR | 服务端电脑 / NAS / 云主机 | FunASR、Silero 都在服务端 |
> | LLM | 云端（豆包）或服务端（Ollama） | 板子不参与 |
> | TTS | 服务端 | EdgeTTS 由服务端调微软接口 |

#### TTS：按上手难度排序

| 方案 | 费用 | 自然度 | 代价 | 适合 |
|---|---|---|---|---|
| **EdgeTTS 换音色 + 放慢** | 免费、无需 Key | 中上（微软神经音色） | 无 | **先做这个** |
| 本地 Kokoro-82M | 免费开源 | 高，英语接近真人 | 电脑常开，要起 OpenAI 兼容 TTS 服务 | 不想注册、有 Mac/PC |
| 阿里云百炼 CosyVoice | 有免费额度 | 很高，可带情感/语气指令 | 阿里云实名 + Key | 愿意注册一次 |
| MiniMax speech-02 | 新用户额度 | 很高 | 实名 + Key | 追求自然度 |
| 火山豆包 TTS | 有试用/少量免费 | 高 | 需语音 `appid`/`access_token`（与方舟 LLM Key 不是一个） | 已经在用豆包 |
| Piper / MeloTTS | 免费开源 | 中 | 本机部署 | 低配机器、离线 |

**EdgeTTS 音色建议**（改 `voice` 即可，零成本）：

- `en-US-JennyNeural`：温和女声，像老师，**默认推荐**；
- `en-US-AvaMultilingualNeural` / `en-US-EmmaMultilingualNeural`：起伏更自然（以 Edge 端当期支持的音色为准）；
- `en-US-AnaNeural`：童声，可爱但偏平，适合宠物短句，不适合长句；
- `en-GB-SoniaNeural`：英式，配英式教材时用；
- `rate: -8% ~ -12%` 放慢（当前版本支持时）；句子必须带标点，TTS 才有停顿。

#### ASR：按上手难度排序

| 方案 | 费用 | 英语口音 | 代价 | 适合 |
|---|---|---|---|---|
| **FunASR / SenseVoice 本地** | 免费 | 中 | 占本机 CPU/内存 | **MVP 默认** |
| faster-whisper（`small.en` / `distil-small.en`） | 免费开源 | 好 | 延迟更高，需本机 ASR 服务 | 英语识别不准时先换 |
| 硅基流动 SenseVoiceSmall API | 免费模型（以当期列表为准） | 中 | 注册 + Key、联网 | 不想占本机算力 |
| 阿里云百炼 Paraformer / SenseVoice | 有免费额度 | 中上 | 实名 + Key | 已在阿里云 |
| 讯飞语音听写 | 有免费额度 | 中上（中文更强） | 实名 + Key | 家里中文指令多 |
| 豆包流式 ASR | 按小时（少量免费） | 好，中英混识 | 需语音 `appid`/`access_token` | 追求低延迟 |

> ⚠️ faster-whisper / Kokoro 需要服务端有对应适配器；没有时先用 FunASR + EdgeTTS，或在服务端加一个 OpenAI 兼容的本地服务再指向它。这类自建适配属于 V2，不进 MVP。

#### 不让它像客服的 5 条 Prompt 规则

1. 用缩写：`I'm`、`let's`、`that's`，不要 `I am`、`let us`；
2. 每轮先给 1 个语气词：`Oh!` / `Nice!` / `Hmm...`；
3. 每轮结尾抛回一个问题，让孩子继续开口；
4. 纠错只示范一遍：`Try with me: ...`，不解释语法；
5. 禁止 `As an AI...`、长段解释和书面语。

**推荐顺序（不折腾版）**：先只改 EdgeTTS 音色 + 语速 + Prompt（0 元、0 注册）→ 英语识别不准再上本机 faster-whisper → 声音仍死板再考虑 Kokoro（零 Key）或 CosyVoice（愿注册）→ 每次只换一项，录前后各 10 秒对比。

## 二、套件 / 分包方案

| 方案 | 包含 | 适合谁 | 参考价 |
|---|---|---|---|
| 最小可跑包 | 开发板 + 喇叭 + USB-C 数据线 | 只想先验证语音链路 | ￥260–330 |
| 怀表完整包（官方铝壳） | 1.75C（含铝壳、挂绳）+ 702530 500mAh 电池 | 想做成随身成品，免打印 | ￥330–450 |
| 怀表完整包（打印壳） | 最小包 + 103450 2000mAh + 3D 打印壳 + 挂绳 | 追求续航，接受自打印 | ￥330–450 |
| 课堂演示包 | 开发板 + 喇叭 + 充电宝供电（不装电池） | 学校/机构，避免电池管理 | ￥270–340 |

**容易被忽略的不含项**：

- 能传数据的 USB-C 线（很多线只供电）；
- 8Ω 喇叭与匹配的 MX1.25 插头（买前看板子接口定义）；
- 电池**带保护板**、插头极性正确；
- 3D 打印的支撑清理与螺丝；
- 服务端电脑/NAS 的电费与常开要求。

## 三、成本构成

| 类别 | 金额(CNY) | 说明 |
|---|---|---|
| 主控与屏幕 | 220–300 | Waveshare 1.75 AMOLED 开发板 |
| 音频 | 5–15 | 8Ω 2W 喇叭 + 线 |
| 电源 | 20–40 | 702530 500mAh（官方壳原位）；升级 702540/802535 约 +10 |
| 结构件 | 30–80 | PETG 打印怀表壳 + 挂绳 |
| 线材/耗材 | 10–25 | USB-C 线、螺丝、双面胶 |
| 工具（可选） | 0–150 | 万用表、螺丝刀；已有则 0 |
| **硬件合计** | **300–450** | 不含云端费用 |
| 云端（每月） | 20–80 | 每天 10–20 分钟，**待验证** |

## 四、BOM 与采购

- 明细：[`../hardware/bom/bom_english-pet-watch_v1.0.csv`](../hardware/bom/bom_english-pet-watch_v1.0.csv)
- 采购注意：
  1. 开发板优先官方店/授权店，确认是 **AMOLED 触摸版**而不是无触摸版；
  2. 电池必须 **3.7V 锂聚合物 + 保护板 + MX1.25 2P 插头**，到货先量极性与线序；官方铝壳只认 **702530（7×25×30）** 尺寸，最大可试 702540/802535，**1500mAh 放不进官方壳**；
  3. 喇叭选 8Ω 2W 腔体喇叭，先确认板子的喇叭座规格；
  4. 外壳先不打样：用纸板/现成塑料盒做 1:1 模型，确认屏幕开孔和厚度后再打印；
  5. 备损：喇叭线、电池插头各多买 1 套，运费比零件贵。

### 4.1 电池容量与尺寸约束（1.75C 官方铝壳）

- 整机 **Ø55.0 × 厚 15.05mm**（Waveshare 官方尺寸图）；原装电池是 **702530（7×25×30mm、3.7V 500mAh、1.85Wh）**，电池仓按它设计，厚度方向只剩约 7mm。
- 官方壳内原位可换的极限约 **650–700mAh**，提升 30–40%：

| 型号 | 尺寸(mm) | 标称容量 | 相对原装 | 风险 |
|---|---|---|---|---|
| 702530（原装） | 7×25×30 | 500mAh | — | 基准 |
| **702540**（推荐） | 7×25×40 | 650mAh | +30% | 宽/厚不变，只长 10mm；能否合盖 `待验证` |
| 802535 | 8×25×35 | 700mAh | +40% | 厚 +1mm，可能顶后盖 |
| 603040 | 6×30×40 | 600–700mAh | +20~40% | 宽 +5、长 +10，圆仓角上易干涉 |

**电池仓怎么量**（开不了壳就按原装 702530 比对，能开壳用卡尺量三向）：

```text
俯视：打开后盖看电池仓（单位 mm）
┌───────────────────────────────┐
│        ╭─────────────────╮    │  ① 长 L：原装 30 → 702540 要 40
│        │     电池仓       │    │  ② 宽 W：25，圆仓壁限制基本不能加
│        │    L × W         │    │  ③ 深 D：7，802535 要 8（会顶后盖）
│        ╰─────────────────╯    │
└──────────── 内腔 Ø≈50 ────────┘

侧剖：沿长边切开
┌───────────────────────────────┐
│ 屏幕 + 主板            约 8    │
├───────────────────────────────┤
│ 电池仓深度 D      7 / 8  ←卡尺 │
├───────────────────────────────┤
│ 后盖                   约 1    │
└───────────────────────────────┘
整机厚 15.05（Waveshare 官方尺寸图）
```

- 量法：卡尺贴后盖内壁量 **长×宽**，再从仓底量到主板/屏蔽罩得 **深**；没有卡尺就拿原装 702530 电池比对，四周各留 0.5mm 余量。
- 判定：**长**最有余量（702540 只 +10mm），**宽**受圆形仓壁限制，**深**每加 1mm 都要确认后盖能压合、屏幕不受挤压。

- **想上 1500–2000mAh（103450，10×34×50）必须换后盖或换打印壳**，官方铝壳放不下。
- 采购规格：3.7V 1S、带保护板、MX1.25 2P 两线（不要 NTC 三线）、线长 3–5cm、尺寸为含保护板总尺寸。
- 购买入口（搜索页比单品链接稳定，下单前核对尺寸图）：
  - 主选 702540 650mAh：[淘宝搜索](https://s.taobao.com/search?q=702540%20650mAh%20MX1.25%20%E4%BF%9D%E6%8A%A4%E6%9D%BF) ｜ [1688 搜索](https://s.1688.com/selloffer/offer_search.htm?keywords=702540%20650mAh%20MX1.25)
  - 备选 802535 700mAh：[淘宝搜索](https://s.taobao.com/search?q=802535%20700mAh%20MX1.25%20%E4%BF%9D%E6%8A%A4%E6%9D%BF) ｜ [1688 搜索](https://s.1688.com/selloffer/offer_search.htm?keywords=802535%20700mAh%20MX1.25)
  - 原装同款 702530 500mAh：[淘宝搜索](https://s.taobao.com/search?q=702530%20500mAh%20MX1.25%20%E4%BF%9D%E6%8A%A4%E6%9D%BF)
- ⚠️ 线序：不同店铺 MX1.25 的红黑顺序可能相反，插前对照原装电池插头（红=正）；无万用表时拍照让客服按原装 702530 线序做。

## 五、待确认 / 待验证

| 项 | 为什么重要 | 怎么确认 |
|---|---|---|
| 开发板当日价格与版本 | 差价与引脚差异 | 下单页 + 丝印 |
| 1.75C 的具体差异 | 引脚/外设可能不同 | Waveshare Wiki 与原理图 |
| 连续对话续航 | 决定电池容量与壳厚 | 装壳后实测 30/60/120 分钟 |
| 首响延迟 | 决定体验是否「像人」 | 服务端日志打时间戳，测 20 次取中位数 |
| 发音评测 API 价格与英语支持 | 决定 V2 是否做 | 各厂商控制台试用 |
| 外壳尺寸与天线净空 | 影响 Wi-Fi 与装配 | 先用纸板模型 + 网络吞吐测试 |
| 官方壳内腔实测尺寸 | 决定 702540/802535 能否装下 | 卡尺量长×宽×深；无工具则按 702540 试装（`待验证`） |
| 升级电池实际容量 | 商家标称普遍虚高 | 满电实测放电时间，按原装 702530 折算 |
