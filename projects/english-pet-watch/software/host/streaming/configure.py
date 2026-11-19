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


if __name__ == "__main__":
    import yaml

    path = Path(sys.argv[1])
    result = yaml.safe_dump(configure(yaml.safe_load(path.read_text())), allow_unicode=True, sort_keys=False)
    backup = path.with_name(path.name + ".epw-stream-backup")
    if not backup.exists():
        shutil.copy2(path, backup)
        backup.chmod(0o600)
    with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, delete=False) as temporary:
        temporary.write(result)
        staged = Path(temporary.name)
    staged.chmod(0o600)
    staged.replace(path)
    print("ASR=FunASRPreview; TTS=EdgeTTSStream; VAD silence=600ms; 配置备份已保留。")
