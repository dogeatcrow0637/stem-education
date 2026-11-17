#!/usr/bin/env python3
"""静默验证 xiaozhi VAD→ASR→LLM→TTS；只在内存中解码，绝不调用声卡或设备。"""
import argparse
import asyncio
import json
import subprocess
import tempfile
import uuid
from pathlib import Path

import edge_tts
import opuslib_next
import websockets


async def fixture(text, directory, index):
    path = directory / f"fixture-{index}.mp3"
    await edge_tts.Communicate(text, "zh-CN-XiaoyiNeural").save(str(path))
    result = subprocess.run(
        ["ffmpeg", "-nostdin", "-v", "error", "-i", str(path), "-f", "s16le",
         "-ar", "16000", "-ac", "1", "pipe:1"],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
    )
    assert 3200 < len(result.stdout) <= 480000, "fixture duration invalid"
    return result.stdout


async def send_audio(socket, pcm):
    encoder = opuslib_next.Encoder(16000, 1, opuslib_next.APPLICATION_AUDIO)
    frame_bytes = 1920  # 60ms at 16kHz / mono / int16
    pcm = bytes(frame_bytes * 5) + pcm + bytes(frame_bytes * 35)
    for offset in range(0, len(pcm), frame_bytes):
        frame = pcm[offset:offset + frame_bytes].ljust(frame_bytes, b"\0")
        await socket.send(encoder.encode(frame, 960))
        await asyncio.sleep(0.06)


async def receive_turn(socket):
    result = {"asr": False, "llm_event": False, "tts_start": False,
              "tts_text": False, "tts_stop": False, "opus_packets": 0, "pcm_bytes": 0}
    decoder = opuslib_next.Decoder(16000, 1)
    async for raw in socket:
        if isinstance(raw, bytes):
            pcm = decoder.decode(raw, 1920)
            result["opus_packets"] += 1
            result["pcm_bytes"] += len(pcm)
            continue  # 没有输出设备，也不把合成音频发送给手表。
        message = json.loads(raw)
        if message.get("type") == "stt":
            result["asr"] |= bool(message.get("text"))
        elif message.get("type") == "llm":
            result["llm_event"] = True
        elif message.get("type") == "tts":
            state = message.get("state")
            if state == "start":
                result["tts_start"] = True
            elif state == "sentence_start":
                result["tts_text"] |= bool(message.get("text"))
            elif state == "stop":
                result["tts_stop"] = True
                break
    assert all(result[k] for k in ("asr", "tts_start", "tts_text", "tts_stop")), result
    assert result["opus_packets"] > 0 and result["pcm_bytes"] > 0, result
    return result


async def check(url):
    # 固定的合成句子，无环境录音、无个人数据。
    questions = ["你好，请只说一句简短的中文问候。", "请告诉我一加一等于几。"]
    with tempfile.TemporaryDirectory(prefix="epw-silent-") as temporary:
        frames = await asyncio.gather(*(fixture(t, Path(temporary), i) for i, t in enumerate(questions)))
        headers = {"Device-Id": "epw-silent-" + uuid.uuid4().hex[:12], "Protocol-Version": "1"}
        async with websockets.connect(url, additional_headers=headers, max_size=65536) as socket:
            await socket.send(json.dumps({"type": "hello", "version": 1, "transport": "websocket",
                "audio_params": {"format": "opus", "sample_rate": 16000, "channels": 1, "frame_duration": 60}}))
            hello = json.loads(await asyncio.wait_for(socket.recv(), 15))
            assert hello.get("type") == "hello" and hello.get("session_id"), "missing server hello"
            session = hello["session_id"]
            for index, pcm in enumerate(frames):
                await socket.send(json.dumps({"type": "listen", "state": "start", "mode": "auto", "session_id": session}))
                await asyncio.sleep(0.5)
                receiver = asyncio.create_task(receive_turn(socket))
                try:
                    await send_audio(socket, pcm)
                    result = await asyncio.wait_for(receiver, 60)
                finally:
                    if not receiver.done():
                        receiver.cancel()
                        await asyncio.gather(receiver, return_exceptions=True)
                print(json.dumps({"turn": index + 1, "silent": True, **result}), flush=True)
            await socket.send(json.dumps({"type": "abort", "session_id": session}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="ws://127.0.0.1:8000/xiaozhi/v1/")
    args = parser.parse_args()
    asyncio.run(asyncio.wait_for(check(args.url), 180))
