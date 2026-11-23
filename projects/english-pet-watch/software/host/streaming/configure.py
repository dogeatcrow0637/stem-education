#!/usr/bin/env python3
"""启用本地 ASR 预览、EdgeTTS 增量转码和 600ms 断句；首次修改前备份配置。"""
import copy
from pathlib import Path
import shutil
import sys
import tempfile


def configure(config):
    config = copy.deepcopy(config)
    selected = config["selected_module"]
    active = config["ASR"][selected["ASR"]]
    if active["type"] not in ("fun_local", "fun_local_preview"):
        raise ValueError("仅支持当前本地 FunASR，未修改其它 ASR 配置")
    config["ASR"]["FunASRPreview"] = dict(active, type="fun_local_preview")
    selected["ASR"] = "FunASRPreview"
    tts = config["TTS"][selected["TTS"]]
    if tts["type"] not in ("edge", "edge_stream"):
        raise ValueError("仅支持当前 EdgeTTS，未修改其它 TTS 配置")
    config["TTS"]["EdgeTTSStream"] = dict(tts, type="edge_stream")
    selected["TTS"] = "EdgeTTSStream"
    config["VAD"][selected["VAD"]]["min_silence_duration_ms"] = 600
    return config


WATCH_POLICY = """[EPW 手表语音控制]
用户要求操作手表时必须调用 self_watch_* 设备工具，根据真实返回值答复，不能只口头承诺执行。
播放3S儿歌必须先 search_songs，用返回的 song_id 调 music play；中文歌名可翻译成英文关键词查询，查不到就诚实告知或请用户补充，不能猜ID。不要用服务器音乐播放工具代替手表3S曲库。
音量和亮度的模糊增减默认 delta 为正负10，静音 value=0。多个操作要逐项调用。
accepted=true 只表示已排队，简短说即将执行，然后结束回复，让设备在说完后切页或播放。不要再调用其它切页或播放操作，也不要声称已经播放成功。
shutdown只会打开确认滑块，明确请用户在手表滑动关机；不得声称已经关机。
闲聊和讲故事正常回答，不调用设备工具。工具错误如实说明，不绕过限制。
"""


def configure_watch_control(config):
    config = copy.deepcopy(config)
    config.setdefault("Intent", {})["EPWWatchControl"] = {"type": "function_call", "functions": []}
    config["selected_module"]["Intent"] = "EPWWatchControl"
    prompt = config.get("prompt", "")
    if "[EPW 手表语音控制]" not in prompt:
        config["prompt"] = prompt.rstrip() + "\n\n" + WATCH_POLICY
    return config


if __name__ == "__main__":
    import yaml

    path = Path(sys.argv[1])
    config = configure(yaml.safe_load(path.read_text()))
    if "--watch-control" in sys.argv[2:]:
        config = configure_watch_control(config)
    result = yaml.safe_dump(config, allow_unicode=True, sort_keys=False)
    suffix = ".epw-control-backup" if "--watch-control" in sys.argv[2:] else ".epw-stream-backup"
    backup = path.with_name(path.name + suffix)
    if not backup.exists():
        shutil.copy2(path, backup)
        backup.chmod(0o600)
    with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, delete=False) as temporary:
        temporary.write(result)
        staged = Path(temporary.name)
    staged.chmod(0o600)
    staged.replace(path)
    if "--watch-control" in sys.argv[2:]:
        print("Intent=EPWWatchControl; 已启用设备工具调用。")
    print("ASR=FunASRPreview; TTS=EdgeTTSStream; VAD silence=600ms; 配置备份已保留。")
