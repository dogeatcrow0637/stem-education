#!/usr/bin/env python3
"""给指定 xiaozhi-server 源码目录应用文字流式补丁；不修改配置、不重启服务。

用法：python3 apply_patch.py /opt/xiaozhi-esp32-server
依赖：Python 标准库；先备份源码，每个替换点必须唯一，不匹配即停止。
"""
import ast
from pathlib import Path
import shutil
import sys
import tempfile


def patch_connection(source):
    marker = "from core.utils.epw_text_stream import send_llm_text\n"
    if marker in source:
        return source
    replacements = [
        ("from core.utils import textUtils\n", "from core.utils import textUtils\n" + marker),
        ("        response_message = []\n", "        response_message = []\n        epw_text_at = 0.0\n"),
        ("                        response_message.append(content)\n",
         "                        response_message.append(content)\n"
         "                        if time.monotonic() - epw_text_at >= 0.08:\n"
         "                            epw_text_at = time.monotonic()\n"
         "                            asyncio.run_coroutine_threadsafe(\n"
         "                                send_llm_text(self, ''.join(response_message), current_sentence_id), self.loop\n"
         "                            )\n"),
        ("        # 处理function call\n",
         "        if response_message and not self.client_abort:\n"
         "            asyncio.run_coroutine_threadsafe(\n"
         "                send_llm_text(self, ''.join(response_message), current_sentence_id, final=True), self.loop\n"
         "            )\n"
         "        # 处理function call\n"),
        ("        # Clear ASR buffers\n",
         "        self._epw_preview_epoch = getattr(self, '_epw_preview_epoch', 0) + 1\n"
         "        # Clear ASR buffers\n"),
    ]
    for old, new in replacements:
        if source.count(old) != 1:
            raise ValueError(f"上游源码替换点不唯一，停止：{old.strip()}")
        source = source.replace(old, new)
    ast.parse(source)
    return source


def patch_send_audio(source):
    marker = '    # EPW: playback-timed word captions.\n'
    if marker in source:
        return source
    old = '    if text is None and state == "sentence_start":\n'
    if source.count(old) != 1:
        raise ValueError("上游字幕发送替换点不唯一，停止")
    new = marker + '''    if state == "sentence_start" and isinstance(text, dict):
        if (conn.features or {}).get("audio_text_sync"):
            await conn.websocket.send(json.dumps({
                "type": "tts", "session_id": conn.session_id,
                "state": "word_boundary", "at_ms": text["at_ms"], "text": text["text"],
            }, ensure_ascii=False))
        return
'''
    source = source.replace(old, new + old)
    ast.parse(source)
    return source


def apply(root):
    here = Path(__file__).resolve().parent
    connection = root / "core/connection.py"
    outputs = {
        connection: patch_connection(connection.read_text()),
        root / "core/handle/sendAudioHandle.py": patch_send_audio((root / "core/handle/sendAudioHandle.py").read_text()),
        root / "core/providers/asr/fun_local_preview.py": (here / "fun_local_preview.py").read_text(),
        root / "core/utils/epw_text_stream.py": (here / "epw_text_stream.py").read_text(),
        root / "core/providers/tts/edge_stream.py": (here / "edge_stream.py").read_text(),
    }
    for source in outputs.values():
        ast.parse(source)
    # 所有替换校验通过后才开始写入；备份保留首次应用前的版本。
    backup = root / ".epw-stream-backup"
    for path, source in outputs.items():
        if path.exists() and path.read_text() == source:
            continue
        original = backup / path.relative_to(root)
        if path.exists() and not original.exists():
            original.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, original)
        with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, delete=False) as temporary:
            temporary.write(source)
            staged = Path(temporary.name)
        staged.chmod(path.stat().st_mode if path.exists() else 0o644)
        staged.replace(path)


if __name__ == "__main__":
    apply(Path(sys.argv[1]))
    print("文字流式补丁已应用；配置和服务状态未修改。")
