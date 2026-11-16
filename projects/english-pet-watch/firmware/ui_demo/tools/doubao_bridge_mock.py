#!/usr/bin/env python3
"""豆包宠物状态桥 · 本地调试服务

给 ESP32-S3 英语宠物怀表提供最小 HTTP 接口：

    GET /pet/state
    -> {"emotion":"happy","text":"Hello！跟我学英语吧！"}

用法：
    # 1) 纯本地剧本（不需要任何 Key，先把 UI 状态机跑通）
    python3 tools/doubao_bridge_mock.py --port 8788

    # 2) 真正调用豆包（火山方舟）
    export ARK_API_KEY=xxxx
    python3 tools/doubao_bridge_mock.py --port 8788 --doubao

设备端在 menuconfig 里填：
    CONFIG_EPW_PET_BRIDGE_URL="http://<电脑IP>:8788/pet/state"

注意：这是演示/联调用的桥。正式链路仍建议用 xiaozhi-server 的
WebSocket 对话（ASR → 豆包 LLM → TTS），本服务只负责“把回复变成
宠物表情 + 台词”这一步。
"""

import argparse
import json
import os
import random
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

EMOTIONS = {
    "idle", "happy", "excited", "thinking", "speaking",
    "sleepy", "sad", "love", "listening",
}

# 本地剧本：覆盖 9 种情绪，每 6 秒换一条，方便在真机上看全所有表情
SCRIPT = [
    ("idle", "喵～ 你好呀！我是 Pip。"),
    ("happy", "见到你真开心！"),
    ("listening", "我在听，你说吧。"),
    ("thinking", "让我想一想…"),
    ("speaking", "Hello！跟我读：cat！"),
    ("excited", "哇！太棒啦！"),
    ("love", "我最喜欢你啦！"),
    ("sleepy", "我有点困了，晚安～"),
    ("sad", "别难过，我陪着你。"),
    ("happy", "Apple 是苹果，Banana 是香蕉。"),
]

DOUBAO_PROMPT = (
    "你是『皮皮 Pip』，一只陪 5 岁小朋友学英语的 3D 小猫。"
    "回答要短，中文为主，夹 1 个简单英文单词。"
    "情绪只能是 idle/happy/excited/thinking/speaking/sleepy/sad/love/listening 之一。"
    "只输出 JSON，不要 Markdown："
    '{"emotion":"happy","text":"一句不超过 20 个字的话"}'
)

_doubao_cache = {"at": 0.0, "reply": None}


def call_doubao() -> dict:
    api_key = os.environ.get("ARK_API_KEY", "").strip()
    if not api_key:
        raise RuntimeError("ARK_API_KEY is not set")

    model = os.environ.get("DOUBAO_MODEL", "doubao-seed-2-0-lite-260215")
    url = os.environ.get(
        "ARK_ENDPOINT", "https://ark.cn-beijing.volces.com/api/v3/chat/completions"
    )
    payload = {
        "model": model,
        "messages": [{"role": "user", "content": DOUBAO_PROMPT}],
        "temperature": 0.8,
        "max_tokens": 120,
    }
    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {api_key}",
        },
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=20) as resp:
        body = json.loads(resp.read().decode("utf-8"))

    content = body["choices"][0]["message"]["content"]
    start = content.find("{")
    end = content.rfind("}")
    if start < 0 or end <= start:
        raise RuntimeError(f"no JSON in Doubao reply: {content!r}")

    data = json.loads(content[start:end + 1])
    emotion = str(data.get("emotion", "idle")).lower()
    text = str(data.get("text", "")).strip()
    if emotion not in EMOTIONS:
        emotion = "idle"
    if not text:
        text = "喵～ 我们一起学英语吧！"
    return {"emotion": emotion, "text": text}


def next_reply(use_doubao: bool, cache_seconds: int) -> dict:
    if not use_doubao:
        idx = int(time.time() // 6) % len(SCRIPT)
        emotion, text = SCRIPT[idx]
        return {"emotion": emotion, "text": text}

    now = time.time()
    if _doubao_cache["reply"] and now - _doubao_cache["at"] < cache_seconds:
        return _doubao_cache["reply"]

    try:
        reply = call_doubao()
    except Exception as exc:  # noqa: BLE001 - 联调时需要把错误变成可读台词
        print(f"[doubao] fallback to script: {exc}", flush=True)
        emotion, text = random.choice(SCRIPT)
        reply = {"emotion": emotion, "text": text}

    _doubao_cache.update({"at": now, "reply": reply})
    return reply


class Handler(BaseHTTPRequestHandler):
    use_doubao = False
    cache_seconds = 20

    def _send_json(self, obj, status=200):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):  # noqa: N802 - BaseHTTPRequestHandler API
        if self.path.startswith("/pet/state"):
            reply = next_reply(self.use_doubao, self.cache_seconds)
            print(f"[pet] {reply['emotion']:9s} {reply['text']}", flush=True)
            self._send_json(reply)
            return
        if self.path.startswith("/health"):
            self._send_json({"ok": True, "doubao": self.use_doubao})
            return
        self._send_json({"error": "not found"}, status=404)

    def log_message(self, fmt, *args):
        # 默认日志太吵；只保留真正的请求行
        if args and str(args[0]).startswith("GET /pet/state"):
            return
        super().log_message(fmt, *args)


def main():
    parser = argparse.ArgumentParser(description="Doubao pet bridge mock")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8788)
    parser.add_argument("--doubao", action="store_true", help="call Volcengine Ark")
    parser.add_argument("--cache", type=int, default=20, help="Doubao cache seconds")
    args = parser.parse_args()

    Handler.use_doubao = args.doubao
    Handler.cache_seconds = args.cache

    mode = "Doubao (ARK_API_KEY)" if args.doubao else "local script"
    print(f"pet bridge: http://{args.host}:{args.port}/pet/state  [{mode}]", flush=True)
    ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
