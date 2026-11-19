# tools · 开发机工具

> 适用版本：v0.1　|　最后更新：2026-11-13
> 当前以**命令清单**为主；脚本按“跑通一次再固化”的原则补，不预先写空壳。

## 1. 命令清单

| 任务 | 命令 |
|---|---|
| 检查 ESP-IDF | `. <IDF>/export.sh && idf.py --version` |
| 设置目标 | `idf.py set-target esp32s3` |
| 编译 | `idf.py build` |
| 烧录+监视 | `idf.py -p /dev/tty.usbmodem* flash monitor` |
| 清干净重编 | `idf.py fullclean && idf.py build` |
| 启动服务端 | `docker compose up -d` |
| 看服务端日志 | `docker logs -f xiaozhi-esp32-server` |
| 测 OTA | `curl http://127.0.0.1:8003/xiaozhi/ota/` |
| 找串口（macOS） | `ls /dev/tty.usbmodem*` |
| 找串口（Linux） | `ls /dev/ttyACM*` |

## 2. 计划脚本（待补，不占位）

| 脚本 | 作用 | 前置 |
|---|---|---|
| `setup.sh` | 检查 IDF/Python/Docker、提示缺什么 | 无 |
| `flash.sh` | 一条命令编译+烧录，参数是串口 | IDF 已装 |
| `check-env.sh` | 串口/服务端/OTA 三项自检 | 服务端已起 |

> 脚本补上后必须：顶部写用途/用法/依赖/影响范围；幂等；不打印密钥。

## 3. 延迟测量（手工版）

自动静默检查：在语音服务器容器内执行 `python data/epw-stream-update/tools/check_voice_chain.py --require-stream`。工具仅发送两句固定合成音频，记录 ASR 中间结果、模型文字快照、最终识别、首音频与 TTS 完成时间并验证 Opus 可完整解码；不采集麦克风、不播放。依赖沿用容器已有的 `edge_tts`、`opuslib_next`、`websockets` 和 ffmpeg。部署及限制见 [`../host/streaming/README.md`](../host/streaming/README.md)。

1. 服务端日志打时间戳；
2. 说一句 “Hello”，记录“说完”到“听到第一声”的秒数；
3. 连续 5 次，去掉最高最低，记平均；
4. 换 ASR/LLM/TTS 后重测，记录在 `docs/04-软件环境.md` 的验证表。

> 没有测量就没有“更快”；宣传值一律标 `待验证`。
