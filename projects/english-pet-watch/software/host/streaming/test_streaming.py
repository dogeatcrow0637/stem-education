"""标准库回归：增量预览限流、终态竞态、协议兼容和补丁幂等。"""
import asyncio
from collections import namedtuple
import importlib.util
import json
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class LocalASR:
    AudioArtifacts = namedtuple("AudioArtifacts", "pcm_frames pcm_bytes file_path temp_path")

    def __init__(self, config, delete_audio_file):
        self.started = asyncio.Event()
        self.release = asyncio.Event()
        self.calls = self.busy = self.peak = 0

    async def speech_to_text(self, data, session, artifacts):
        self.calls += 1
        self.busy += 1
        self.peak = max(self.peak, self.busy)
        self.started.set()
        await self.release.wait()
        self.busy -= 1
        return {"content": "你好，Mimi"}, None

    async def receive_audio(self, conn, pcm_frame, have_voice):
        conn.asr_audio.append(pcm_frame)
        if conn.client_voice_stop:
            await self.handle_voice_stop(conn, conn.asr_audio)
            conn.asr_audio = []
            conn.client_have_voice = conn.client_voice_stop = False

    async def handle_voice_stop(self, conn, data):
        await self.speech_to_text(data, conn.session_id, self.AudioArtifacts(data, b"".join(data), None, None))


def connection():
    messages = []

    async def send(raw):
        messages.append(json.loads(raw))

    return SimpleNamespace(
        features={"text_stream": True}, client_abort=False, sentence_id="turn", session_id="session",
        stop_event=SimpleNamespace(is_set=lambda: False), websocket=SimpleNamespace(send=send),
        logger=SimpleNamespace(warning=lambda message: None, info=lambda message: None), messages=messages,
        asr_audio=[], client_have_voice=True, client_voice_stop=False,
    )


class StreamingTests(unittest.IsolatedAsyncioTestCase):
    def provider(self):
        stub = ModuleType("core.providers.asr.fun_local")
        stub.ASRProvider = LocalASR
        with patch.dict("sys.modules", {"core.providers.asr.fun_local": stub}):
            provider = load("fun_local_preview").ASRProvider({}, True)
        return provider

    async def test_preview_is_real_partial_and_does_not_trigger_chat(self):
        provider, conn = self.provider(), connection()
        provider.release.set()
        await provider.receive_audio(conn, bytes(19200), True)
        await conn._epw_preview_task
        self.assertEqual(provider.calls, 1)
        self.assertEqual(conn.messages, [{"type": "stt", "text": "你好，Mimi", "final": False, "session_id": "session"}])
        for _ in range(3):
            await provider.receive_audio(conn, bytes(1920), True)
        self.assertEqual(provider.calls, 1)
        self.assertTrue(conn.client_have_voice)

    async def test_only_one_inference_and_no_partial_after_final(self):
        provider, conn = self.provider(), connection()
        await provider.receive_audio(conn, bytes(19200), True)
        await provider.started.wait()
        for _ in range(5):
            await provider.receive_audio(conn, bytes(1920), True)
        self.assertEqual(provider.calls, 1)
        final = asyncio.create_task(provider.handle_voice_stop(conn, conn.asr_audio))
        await asyncio.sleep(0)
        provider.release.set()
        await final
        await conn._epw_preview_task
        self.assertEqual(provider.calls, 2)
        self.assertEqual(provider.peak, 1)
        self.assertEqual(conn.messages, [])

    async def test_old_clients_long_audio_and_closed_connections(self):
        provider, conn = self.provider(), connection()
        provider.release.set()
        conn.features = {}
        await provider.receive_audio(conn, bytes(19200), True)
        self.assertFalse(hasattr(conn, "_epw_preview_task"))
        conn.features = {"text_stream": True}
        await provider.receive_audio(conn, bytes(256000), True)
        self.assertFalse(hasattr(conn, "_epw_preview_task"))
        conn.asr_audio = []
        conn.stop_event.is_set = lambda: True
        await provider.receive_audio(conn, bytes(19200), True)
        await conn._epw_preview_task
        self.assertEqual(conn.messages, [])

    async def test_llm_snapshot_limits_utf8_and_rejects_old_turns(self):
        send = load("epw_text_stream").send_llm_text
        conn = connection()
        await send(conn, "中文🙂" * 100, "turn", True)
        message = conn.messages.pop()
        self.assertTrue(message["final"])
        self.assertLessEqual(len(message["text"].encode()), 300)
        self.assertTrue(("中文🙂" * 100).endswith(message["text"]))
        await send(conn, "旧回答", "old")
        conn.client_abort = True
        await send(conn, "中止回答", "turn")
        conn.client_abort = False
        conn.features = {}
        await send(conn, "旧客户端", "turn")
        self.assertEqual(conn.messages, [])

    async def test_text_send_failure_does_not_break_tts(self):
        conn = connection()

        async def fail(raw):
            raise ConnectionError("closed")

        conn.websocket.send = fail
        await load("epw_text_stream").send_llm_text(conn, "文字", "turn")

    def test_patch_is_valid_idempotent_and_refuses_unknown_source(self):
        module = load("apply_patch")
        source = '''from core.utils import textUtils
class Connection:
    def chat(self):
        response_message = []
        if True:
            if True:
                if True:
                    if True:
                        response_message.append(content)
        # 处理function call
        pass
    def reset_audio_states(self):
        # Clear ASR buffers
        pass
'''
        fixed = module.patch_connection(source)
        compile(fixed, "connection.py", "exec")
        self.assertEqual(module.patch_connection(fixed), fixed)
        with self.assertRaises(ValueError):
            module.patch_connection(source.replace("response_message = []", "renamed = []"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for folder in ("core", "core/utils", "core/providers/asr", "core/providers/tts"):
                (root / folder).mkdir(parents=True, exist_ok=True)
            target = root / "core/connection.py"
            target.write_text(source)
            module.apply(root)
            module.apply(root)
            self.assertEqual((root / ".epw-stream-backup/core/connection.py").read_text(), source)
            self.assertEqual(target.read_text(), fixed)

    def test_config_preserves_credentials_modules_and_original(self):
        source = {"selected_module": {"ASR": "FunASR", "VAD": "Silero", "TTS": "EdgeTTS"},
                  "ASR": {"FunASR": {"type": "fun_local", "model_dir": "existing"}},
                  "VAD": {"Silero": {"min_silence_duration_ms": 900}},
                  "TTS": {"EdgeTTS": {"type": "edge", "voice": "existing-voice"}}, "opaque": "preserve"}
        configure = load("configure").configure
        result = configure(source)
        self.assertEqual(source["selected_module"]["ASR"], "FunASR")
        self.assertEqual(result["selected_module"]["ASR"], "FunASRPreview")
        self.assertEqual(result["selected_module"]["TTS"], "EdgeTTSStream")
        self.assertEqual(result["TTS"]["EdgeTTSStream"]["voice"], "existing-voice")
        self.assertEqual(result["opaque"], "preserve")
        self.assertEqual(result["ASR"]["FunASRPreview"]["model_dir"], "existing")
        self.assertEqual(configure(result), result)

    def edge_provider(self, communicate):
        class Base:
            def to_tts_stream(self, text, handler):
                self.fallbacks += 1

        modules = {
            "edge_tts": SimpleNamespace(Communicate=communicate),
            "core.providers.tts.edge": SimpleNamespace(TTSProvider=Base),
            "core.providers.tts.dto.dto": SimpleNamespace(SentenceType=SimpleNamespace(FIRST="first")),
            "core.utils.tts": SimpleNamespace(MarkdownCleaner=SimpleNamespace(clean_markdown=lambda text: text)),
        }
        with patch.dict("sys.modules", modules):
            cls = load("edge_stream").TTSProvider
        provider = cls()
        provider.conn = connection()
        provider.conn.sample_rate = 16000
        provider.current_sentence_id = "turn"
        provider.voice = "test"
        provider.edge_rate = provider.edge_volume = provider.edge_pitch = "0"
        provider.tts_audio_queue = queue.Queue()
        provider.tts_timeout = 5
        provider.delete_audio_file = True
        provider._correct_words_pattern = None
        provider.fallbacks = 0
        return provider

    @unittest.skipUnless(shutil.which("ffmpeg"), "需要已有 ffmpeg 验证增量 MP3 转码")
    async def test_edge_emits_before_download_ends_and_preserves_pcm(self):
        mp3 = subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-f", "lavfi", "-i",
                              "sine=frequency=440:duration=3", "-f", "mp3", "pipe:1"],
                             capture_output=True, check=True).stdout
        ready = asyncio.Event()
        completed = False
        output = bytearray()

        class Communicate:
            def __init__(self, *args, **kwargs):
                pass

            async def stream(self):
                nonlocal completed
                yield {"type": "audio", "data": mp3[:len(mp3)//2]}
                await asyncio.wait_for(ready.wait(), 3)
                yield {"type": "audio", "data": mp3[len(mp3)//2:]}
                completed = True

        def encode(pcm, final, receive):
            output.extend(pcm)
            if pcm:
                if not ready.is_set():
                    self.assertFalse(completed)
                ready.set()

        provider = self.edge_provider(Communicate)
        provider.opus_encoder = SimpleNamespace(reset_state=lambda: None, encode_pcm_to_opus_stream=encode)
        await asyncio.wait_for(provider._stream("test", lambda packet: None, "turn"), 5)
        expected = subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-f", "mp3", "-i", "pipe:0",
                                   "-f", "s16le", "-ar", "16000", "-ac", "1", "pipe:1"],
                                  input=mp3, capture_output=True, check=True).stdout
        self.assertTrue(completed)
        self.assertEqual(bytes(output), expected)

    async def test_edge_fallback_only_before_any_audio_is_emitted(self):
        provider = self.edge_provider(None)
        timings = []
        provider.conn.logger.info = timings.append

        async def fail(text, receive, sentence_id):
            raise ConnectionError("unavailable")

        provider._stream = fail
        await asyncio.to_thread(provider.to_tts_stream, "test", lambda packet: None)
        self.assertEqual(provider.fallbacks, 1)

        async def partial_failure(text, receive, sentence_id):
            receive(b"opus")
            raise ConnectionError("interrupted")

        provider._stream = partial_failure
        packets = []
        with self.assertRaises(ConnectionError):
            await asyncio.to_thread(provider.to_tts_stream, "test", packets.append)
        self.assertEqual(provider.fallbacks, 1)
        self.assertEqual(packets, [b"opus"])
        self.assertEqual(provider.tts_audio_queue.get_nowait(), ("first", None, "test", "turn"))
        self.assertEqual(len(timings), 2)
        self.assertIn("frames=0 first_ms=-1", timings[0])
        self.assertIn("frames=1", timings[1])
        self.assertIn("max_gap_ms=0", timings[1])
        self.assertNotIn("test", " ".join(timings))


if __name__ == "__main__":
    unittest.main(verbosity=2)
