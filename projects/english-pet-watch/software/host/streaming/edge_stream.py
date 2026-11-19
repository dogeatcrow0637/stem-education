"""Edge MP3 增量转 PCM/Opus，沿用现有句子切分、音量与发送流控。"""
import asyncio
import time

import edge_tts
from core.providers.tts.edge import TTSProvider as EdgeTTS
from core.providers.tts.dto.dto import SentenceType
from core.utils.tts import MarkdownCleaner


class TTSProvider(EdgeTTS):
    def to_tts_stream(self, text, opus_handler=None):
        # 需要保留音频文件时沿用原实现，不改变原有保存语义。
        if not self.delete_audio_file or not (self.conn.features or {}).get("text_stream"):
            return super().to_tts_stream(text, opus_handler)
        original = text
        text = MarkdownCleaner.clean_markdown(text)
        if self._correct_words_pattern:
            text = self._correct_words_pattern.sub(lambda match: self.correct_words[match.group(0)], text)
        sentence_id = self.current_sentence_id
        emitted = 0
        started = time.monotonic()
        first = last = None
        max_gap = 0.0

        def receive(packet):
            nonlocal emitted, first, last, max_gap
            if self.conn.client_abort or sentence_id != self.conn.sentence_id or self.conn.stop_event.is_set():
                return
            now = time.monotonic()
            if first is None:
                first = now
            if last is not None:
                max_gap = max(max_gap, now - last)
            last = now
            if emitted == 0:
                self.tts_audio_queue.put((SentenceType.FIRST, None, original, sentence_id))
            emitted += 1
            opus_handler(packet)

        try:
            asyncio.run(asyncio.wait_for(self._stream(text, receive, sentence_id), self.tts_timeout))
        except Exception as error:
            self.conn.logger.warning(f"Edge audio stream failed: {type(error).__name__}; frames={emitted}")
            # 未开始播放时才可整句重试，避免重播已经说过的前半句。
            if emitted == 0 and not self.conn.client_abort and sentence_id == self.conn.sentence_id:
                return super().to_tts_stream(original, opus_handler)
            raise
        finally:
            # 只记录阶段耗时和包数，不包含正文或录音。
            ended = time.monotonic()
            self.conn.logger.info(
                f"EPW TTS timing frames={emitted} first_ms={round((first-started)*1000) if first is not None else -1} "
                f"total_ms={round((ended-started)*1000)} tail_ms={round((ended-last)*1000) if last is not None else -1} "
                f"max_gap_ms={round(max_gap*1000)}"
            )

    async def _stream(self, text, receive, sentence_id):
        process = await asyncio.create_subprocess_exec(
            "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
            "-probesize", "32", "-analyzeduration", "0", "-f", "mp3", "-i", "pipe:0",
            "-f", "s16le", "-ar", str(self.conn.sample_rate), "-ac", "1", "-flush_packets", "1", "pipe:1",
            stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL,
        )
        self.opus_encoder.reset_state()

        async def feed():
            try:
                communicate = edge_tts.Communicate(text, voice=self.voice, rate=self.edge_rate,
                                                  volume=self.edge_volume, pitch=self.edge_pitch)
                async for chunk in communicate.stream():
                    if self.conn.client_abort or sentence_id != self.conn.sentence_id or self.conn.stop_event.is_set():
                        break
                    if chunk["type"] == "audio":
                        process.stdin.write(chunk["data"])
                        await process.stdin.drain()
            finally:
                process.stdin.close()

        async def decode():
            frame_bytes = self.conn.sample_rate * 60 // 1000 * 2
            while True:
                try:
                    pcm = await process.stdout.readexactly(frame_bytes)
                except asyncio.IncompleteReadError as end:
                    if len(end.partial) % 2:
                        raise ValueError("unaligned PCM tail")
                    self.opus_encoder.encode_pcm_to_opus_stream(end.partial, True, receive)
                    break
                self.opus_encoder.encode_pcm_to_opus_stream(pcm, False, receive)

        tasks = [asyncio.create_task(feed()), asyncio.create_task(decode())]
        try:
            await asyncio.gather(*tasks)
            if await process.wait() != 0:
                raise RuntimeError("MP3 decoder failed")
        finally:
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            if process.returncode is None:
                process.kill()
            await process.wait()
