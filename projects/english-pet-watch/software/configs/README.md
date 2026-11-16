# configs · 服务端配置模板

> 适用版本：xiaozhi-esp32-server（2026-11）　|　最后更新：2026-11-13
> 这里只放**无密钥**的配置模板；真正运行时复制到服务端 `data/.config.yaml` 再填自己的 Key。

## 四个模板怎么选

| 文件 | 用途 | 要申请什么 | 什么时候用 |
|---|---|---|---|
| [`local-ollama.config.yaml`](local-ollama.config.yaml) | **零申请、零费用** | 什么都不用 | 不想注册/实名，先跑通 |
| [`mvp-free.config.yaml`](mvp-free.config.yaml) | 零成本跑通 | 智谱免费 Key | 有智谱 Key 时 |
| [`doubao-llm-only.config.yaml`](doubao-llm-only.config.yaml) | **只接豆包 LLM** | 只需火山方舟 Key | 决定用豆包、先少申请 |
| [`mvp-stream.config.yaml`](mvp-stream.config.yaml) | 日常流式体验 | 火山方舟 + 火山语音（ASR/TTS） | 全量豆包 |

## 免费语音「不呆板」：先改这 3 处

不换引擎、不注册，先把 EdgeTTS 和 Prompt 调好，往往就够自然了：

```yaml
TTS:
  EdgeTTS:
    type: edge
    voice: en-US-JennyNeural     # 温和女声；童声换 en-US-AnaNeural
    language: "英语"
    rate: -10%                   # 放慢，方便跟读（当前版本支持时）
```

1. **换音色**：`en-US-JennyNeural`（老师感）或 `en-US-AvaMultilingualNeural`（起伏更自然，以当期音色为准）；`en-US-AnaNeural` 是童声，可爱但偏平。
2. **放慢 + 标点**：`rate: -10%`，每句结尾带 `.` / `?`，TTS 才有停顿。
3. **改 Prompt**：用缩写（`I'm`、`let's`）；每轮先来一个语气词（`Oh!` / `Hmm...`）；结尾反问一句；禁止长段解释和书面语。

> 如果还嫌死板：本机 [Kokoro-82M](https://github.com/hexgrad/kokoro)（免费开源、零 Key）或阿里云 CosyVoice（有免费额度）是下一步；前者需要服务端支持 OpenAI 兼容 TTS。完整对比见 [`../../docs/02-选型与BOM.md`](../../docs/02-选型与BOM.md) 1.5 节。

## 方案 0：Ollama（完全不申请 Key）

```bash
# 1. 安装 Ollama（macOS 用 Homebrew 或官网安装包）
brew install ollama

# 2. 拉一个够用的英文对话模型（3b 约 2GB，8GB 内存可跑；16GB 可换 7b）
ollama pull qwen2.5:3b

# 3. 让 Docker 里的服务端能访问 Ollama（默认只监听 127.0.0.1）
#    macOS / Windows Docker Desktop：容器用 http://host.docker.internal:11434
#    Linux：用宿主机局域网 IP，并让 Ollama 监听 0.0.0.0
OLLAMA_HOST=0.0.0.0:11434 ollama serve

# 4. 复制模板并重启服务端
cp <本项目>/software/configs/local-ollama.config.yaml data/.config.yaml
docker compose restart
```

> 取舍：本地 3b 模型的英语质量与指令跟随不如云端，首响也会更慢（`待实测`）；但完全免费、不用实名、声音不出家门。确认链路能跑通后，再决定要不要换云端。

## 豆包申请清单（照着点，分三阶段）

### 阶段 1：只开 LLM（先做这个，申请面最小）

1. 注册/登录火山引擎，完成个人实名认证。
2. 方舟控制台 → 开通管理 → 搜索 **Doubao-Seed-2.0-Lite** → 开通（有免费 token 额度）。
3. 方舟控制台 → API Key → 创建 → 复制 Key。
4. 复制 [`doubao-llm-only.config.yaml`](doubao-llm-only.config.yaml) 到 `data/.config.yaml`，把 `LLM.DoubaoLLM.api_key` 换成你的 Key。
5. 重启，先用 `FunASR + EdgeTTS` 跑通；这一步只需要一个 Key。

- 开通页：https://console.volcengine.com/ark/region:ark+cn-beijing/openManagement
- Key 页：https://console.volcengine.com/ark/region:ark+cn-beijing/apiKey

> 启动报「模型不存在 / 无权限」：在方舟创建**推理接入点**，把 `model_name` 改成 `ep-xxxxxxxx`（接入点 ID）。

### 阶段 2：加豆包 ASR（可选）

1. 语音技术控制台 → 创建应用 → 拿 `appid` 和 `access_token`。
2. 开通流式语音识别（小时版）。
3. 把 `ASR.DoubaoStreamASR` 的 `appid` / `access_token` 填上；`resource_id` 保持 `volc.bigasr.sauc.duration`。

- 应用页：https://console.volcengine.com/speech/app
- 流式 ASR 开通：https://console.volcengine.com/speech/service/10011

### 阶段 3：加豆包 TTS（可选，可能花钱）

1. 语音技术 → 语音合成大模型 → 开通并购买音色（起步约 30 元）。
2. 拿 `appid` / `access_token`；`resource_id` 固定 `volc.service_type.10029`。
3. `speaker` 填控制台里的**英语儿童音色**；语速用 `speech_rate: -10` 放慢。

- 大模型 TTS 开通：https://console.volcengine.com/speech/service/10007

> 免费档 TTS 约 2 并发，容易报 TTS 错误；不想花钱就继续用 `EdgeTTS`，只开阶段 1/2。

### 申请时的坑

- 国内云都要求实名，个人身份证即可；企业认证更慢。
- `appid` / `access_token` 是**语音产品**的，不是方舟 Key，别填错位置。
- 所有 Key 只放 `data/.config.yaml`，不进仓库、不进截图。
- 价格/免费额度以官网当期页面为准；本文只写量级。

## 使用步骤

```bash
# 1. 在服务端仓库根目录
cp <本项目>/software/configs/mvp-free.config.yaml data/.config.yaml

# 2. 编辑 data/.config.yaml，把 <...> 占位符换成自己的 Key
#    搜索 "你的" 或 "<" 即可找到全部待填项

# 3. 下载本地 ASR 模型（方案 A 需要）
#    https://modelscope.cn/models/iic/SenseVoiceSmall/resolve/master/model.pt
#    放到 models/SenseVoiceSmall/model.pt

# 4. 重启
docker compose restart
docker logs -f xiaozhi-esp32-server
```

切换方案：用 `mvp-stream.config.yaml` 覆盖 `data/.config.yaml`，重启即可；两个模板的 `selected_module` 是完整的。

## 约定

1. **不要提交填好 Key 的 `data/.config.yaml`**；本目录模板里只有占位符。
2. 改完先备份：`cp data/.config.yaml data/.config.yaml.bak`；改坏直接覆盖回来。
3. 一次只换一个模块，换完用同一句话复测并对比。
4. 启用智控台后，文件里的 `prompt` / `selected_module` 不生效，要在网页里按智能体配。
5. 价格与免费额度以云厂商当期页面为准；本项目只记录量级。
