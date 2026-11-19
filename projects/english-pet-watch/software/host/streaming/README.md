# Mimi 文字与语音流式修复

适用：当前部署的 xiaozhi-server 0.9.8、本地 FunASR + 豆包 LLM + EdgeTTS。复用现有模型、账号、ffmpeg 与 Opus 编码器；没有新增第三方依赖。上游来源及许可见 [`../../../docs/99-参考资源.md`](../../../docs/99-参考资源.md)。

## 行为与边界

- 新固件 hello 声明 `features.text_stream=true`。旧设备继续收到原协议，不接收 ASR 预览或模型文字快照。
- `fun_local_preview.py` 复用已加载的 SenseVoice/FunASR，每秒最多启动一次预览、每连接最多一个在途任务，模型推理串行；最终识别只需等待已经运行的那一次预览。预览为 `stt` + `final:false`，不触发聊天；最终结果仍走原链路。断句、重置和退出后不发送迟到预览。
- **这是离线模型的增量识别预览，不是原生流式模型。** 为限制计算量，仅对前八秒音频提供预览；长句最终仍完整识别。600ms 静音断句替代原 900ms，儿童长停顿的误断句率待验证。
- `epw_text_stream.py` 在 LLM 生成过程中每约 80ms 推送一次累计文字的 UTF-8 尾部快照，最后补一次终态；单条正文不超过 300 字节。设备可合并更新，不会因为少处理一个 delta 而缺字。当前部署使用 `nointent` 普通文字回答；`direct_answer` 工具参数流未扩展。
- `edge_stream.py` 在现有句子切分后，边接收 Edge MP3、边通过 ffmpeg 转为 PCM、边编码 Opus；字幕、音量、句序和发送流控沿用原有实现。超时/取消清理子进程；首包前失败可回退原流程，已有音频发出后不重播前半句。要求保存音频文件或旧设备未声明功能时沿用原实现。
- 固件默认关闭未经实机声学校准的自动语音打断，确保回答不会被扬声器回声打断；右滑退出仍立即停播。校准后才启用 `CONFIG_EPW_VOICE_BARGE_IN`，不能用主机测试代替校准。

## 检查与应用

本地回归：

```bash
python3 projects/english-pet-watch/software/host/streaming/test_streaming.py
```

9 项标准库测试；已有 ffmpeg 时同时验证“下载未结束便产出 PCM”和新旧转码 PCM 逐字节一致，不调用在线语音服务。

把本目录复制到容器后，在容器内执行（应用前先确认路径）：

```bash
python /tmp/epw-stream-update/host/streaming/test_streaming.py
python /tmp/epw-stream-update/host/streaming/apply_patch.py /opt/xiaozhi-esp32-server
python -m py_compile core/connection.py core/providers/asr/fun_local_preview.py core/providers/tts/edge_stream.py core/utils/epw_text_stream.py
python /tmp/epw-stream-update/host/streaming/configure.py data/.config.yaml
```

`apply_patch.py` 仅修改三个新增模块与 `core/connection.py`；替换点不唯一时停止，不猜测上游结构。首次源码备份位于 `.epw-stream-backup/`，配置备份位于 `data/.config.yaml.epw-stream-backup`，配置及备份权限为 600。不输出密钥。`configure.py` 只切换兼容的本地 ASR、EdgeTTS 和断句等待，其余配置保持原值。

在宿主机重启容器，然后静默验证：

```bash
docker restart xiaozhi-esp32-server
docker exec xiaozhi-esp32-server python data/epw-stream-update/tools/check_voice_chain.py --require-stream
```

部署时将补丁包保存到挂载的 `data/epw-stream-update/`；该目录含代码及测试，不含密钥或录音。**容器 restart 保留修复；重建容器会丢失可写层的源码修改，必须先重新应用补丁再启动服务。** 不直接拉取最新版替换当前容器。

回滚（容器内恢复两处备份，再从宿主机重启容器）：

```bash
cp .epw-stream-backup/core/connection.py core/connection.py
cp data/.config.yaml.epw-stream-backup data/.config.yaml
chmod 600 data/.config.yaml
```

## 2026-11-19 验证

- 本地与服务器 9 项回归通过；两轮固定合成语音均收到 ASR 预览、模型文字、TTS start/stop，完整解码分别为 62 包/3.72 秒及 36 包/2.16 秒。测试不读取麦克风，也不向手表或本机声卡播放。
- 同输入的修复前最终 ASR 约为输入结束后 558/565ms，启用预览与 600ms 断句后约为 252/238ms；首次预览在本次输入开始约 860/979ms 到达。合成素材带有尾部静音，这不是人的实际停嘴延迟，也不是统计性能保证。
- 固定长句独立测试（不经过 LLM，约 11.34 秒语音）交替运行旧/新 TTS 两次：旧首包 2718/1635ms，新首包 948/1005ms，均为 189 包。短问答端到端首包仍有约 3–4 秒的云端与网络波动，不能据此承诺所有问题立即出声。
- 实际外放完整性、连续多轮、弱网恢复和儿童停顿断句仍待实机验证。默认关闭自动打断不代表 AEC 已校准。

### 间歇延迟诊断（2026-11-19）

`edge_stream` 每句记录 `EPW TTS timing`：首包、总生成、最后一包到退出的收尾耗时及生成包最大间隔，只含时间与包数，不含正文或录音。此计时用于区分 Edge 等待、转码收尾和服务器发送阶段；不能直接表示设备实际播完的时间。两轮静默复测从输入音频末尾到模型首字约 1.57/1.63 秒、语音首包约 2.53/3.31 秒，间歇长等待仍待定位。

计时补丁部署并重启后两轮链路通过（ASR 预览、模型快照、TTS 完整停止均存在）。三段 TTS 生成首包为 1160/1926/1163ms，收尾均为 1ms，生成包最大间隔为 602/531/155ms；这三段未重现此前约八秒的额外等待，不能据此断言该问题已消除。
