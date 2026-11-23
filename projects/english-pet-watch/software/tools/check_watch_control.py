#!/usr/bin/env python3
"""静默验证真实服务端的中文→设备工具调用；模拟手表，绝不控制真机或输出声音。"""
import argparse
import ast
import asyncio
import json
import time
from pathlib import Path
import uuid

import websockets


def firmware_tools(path):
    source = path.read_text()
    section = source.split('static const char *const s_tools[] = {', 1)[1].split('\n};', 1)[0]
    return [json.loads(ast.literal_eval(line.strip().rstrip(','))) for line in section.splitlines() if line.strip()]


async def check(url, tools, text, expected):
    headers = {"Device-Id": "epw-control-test-" + uuid.uuid4().hex[:10], "Protocol-Version": "1"}
    calls, audio, captions = [], 0, 0
    async with websockets.connect(url, additional_headers=headers, max_size=65536) as socket:
        await socket.send(json.dumps({"type": "hello", "version": 1, "transport": "websocket",
            "features": {"mcp": True, "text_stream": True, "audio_text_sync": True},
            "audio_params": {"format": "opus", "sample_rate": 16000, "channels": 1, "frame_duration": 60}}))
        sent = False
        deadline = time.monotonic() + 90
        while True:
            raw = await asyncio.wait_for(socket.recv(), max(0.01, deadline - time.monotonic()))
            if isinstance(raw, bytes):
                audio += 1
                continue
            msg = json.loads(raw)
            if msg.get("type") == "hello":
                session = msg["session_id"]
            elif msg.get("type") == "mcp":
                req = msg["payload"]
                method = req.get("method")
                if method == "initialize":
                    result = {"protocolVersion": "2024-11-05", "capabilities": {"tools": {}},
                              "serverInfo": {"name": "epw-silent-test", "version": "1"}}
                elif method == "tools/list":
                    result = {"tools": tools}
                elif method == "tools/call":
                    name = req["params"]["name"].removeprefix("self.watch.")
                    args = req["params"].get("arguments", {})
                    calls.append((name, args))
                    if name == "search_songs":
                        data = {"songs": [{"song_id": "qa001", "title": "Five Little Monkeys", "seconds": 120}], "total_matches": 1}
                    elif name in ("music", "open", "shutdown"):
                        data = {"accepted": True, "message": "测试设备仅模拟：Mimi说完后执行；shutdown需要滑动确认"}
                    else:
                        data = {"volume": args.get("value", 45), "brightness": args.get("value", 60), "battery_percent": 85,
                                "timer_state": "running", "timer_remaining_seconds": args.get("seconds", 60)}
                    result = {"isError": False, "content": [{"type": "text", "text": json.dumps(data, ensure_ascii=False)}]}
                else:
                    continue
                await socket.send(json.dumps({"type": "mcp", "payload": {"jsonrpc": "2.0", "id": req["id"], "result": result}}))
                if method == "tools/list" and not sent:
                    await asyncio.sleep(1)  # 服务端将工具注册到模型可见列表。
                    await socket.send(json.dumps({"type": "listen", "state": "detect", "text": text, "session_id": session}))
                    sent = True
            elif msg.get("type") == "tts":
                captions += msg.get("state") == "word_boundary"
                if msg.get("state") == "stop" and sent:
                    break
    assert any(name == expected for name, _ in calls), {"text": text, "calls": calls}
    assert audio > 0 and captions > 0, {"audio_packets": audio, "captions": captions}
    if expected == "music":
        assert calls[0][0] == "search_songs", calls
        assert any(name == "music" and args.get("song_id") == "qa001" for name, args in calls), calls
    print(json.dumps({"text": text, "calls": calls, "audio_packets": audio, "caption_events": captions}, ensure_ascii=False))


async def main(args):
    tools = firmware_tools(args.firmware)
    for text, expected in [
        ("把手表音量调到百分之五", "volume"),
        ("屏幕亮度调到百分之六十", "brightness"),
        ("播放3S儿歌五只小猴子", "music"),
        ("开始一个一分钟的倒计时", "timer"),
        ("我要关机", "shutdown"),
    ]:
        await check(args.url, tools, text, expected)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='ws://127.0.0.1:8000/xiaozhi/v1/')
    parser.add_argument('--firmware', type=Path, default=Path(__file__).resolve().parents[2] / 'firmware/ui_demo/main/watch_control.c')
    asyncio.run(main(parser.parse_args()))
