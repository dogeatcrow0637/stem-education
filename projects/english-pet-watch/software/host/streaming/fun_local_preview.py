"""复用本地 FunASR 做限频增量预览；最终识别仍走原有聊天链路。"""
import asyncio
import json
import time

from core.providers.asr.fun_local import ASRProvider as LocalASR


class ASRProvider(LocalASR):
    def __init__(self, config, delete_audio_file):
        super().__init__(config, delete_audio_file)
        self._inference_lock = asyncio.Lock()

    async def speech_to_text(self, opus_data, session_id, artifacts=None):
        # 同一个模型不并发推理；最多等待一个已经开始的预览，不积压预览任务。
        async with self._inference_lock:
            return await super().speech_to_text(opus_data, session_id, artifacts)

    async def receive_audio(self, conn, pcm_frame, audio_have_voice):
        await super().receive_audio(conn, pcm_frame, audio_have_voice)
        if not (conn.features or {}).get("text_stream"):
            return
        task = getattr(conn, "_epw_preview_task", None)
        now = time.monotonic()
        if (not audio_have_voice or not conn.client_have_voice or
                conn.client_voice_stop or self._inference_lock.locked() or
                (task is not None and not task.done()) or
                now - getattr(conn, "_epw_preview_at", 0) < 1.0):
            return
        # shortcut: 本地离线模型仅预览前八秒，长句最终照常完整识别；原生流式 ASR 可替换此模块。
        size = sum(map(len, conn.asr_audio))
        if not 19200 <= size <= 256000:
            return
        conn._epw_preview_at = now
        epoch = getattr(conn, "_epw_preview_epoch", 0)
        pcm = b"".join(conn.asr_audio)
        conn._epw_preview_task = asyncio.create_task(self._preview(conn, pcm, epoch))

    async def _preview(self, conn, pcm, epoch):
        try:
            artifacts = self.AudioArtifacts([pcm], pcm, None, None)
            text, _ = await self.speech_to_text([pcm], conn.session_id, artifacts)
            if isinstance(text, dict):
                text = text.get("content", "")
            if (not text or epoch != getattr(conn, "_epw_preview_epoch", 0) or
                    conn.stop_event.is_set() or not conn.client_have_voice or
                    conn.client_voice_stop):
                return
            await conn.websocket.send(json.dumps({
                "type": "stt", "text": text, "final": False,
                "session_id": conn.session_id,
            }, ensure_ascii=False))
        except Exception as error:
            # 预览故障不能中止录音或最终识别，也不记录录音正文。
            conn.logger.warning(f"ASR preview unavailable: {type(error).__name__}")

    async def handle_voice_stop(self, conn, asr_audio_task):
        conn._epw_preview_epoch = getattr(conn, "_epw_preview_epoch", 0) + 1
        conn._epw_preview_at = time.monotonic()
        await super().handle_voice_stop(conn, asr_audio_task)
