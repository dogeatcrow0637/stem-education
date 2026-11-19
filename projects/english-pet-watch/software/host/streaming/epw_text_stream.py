"""模型文字快照独立于 TTS 推送；旧固件未声明功能时不发送。"""
import json


async def send_llm_text(conn, text, sentence_id, final=False):
    if (not (conn.features or {}).get("text_stream") or conn.client_abort or
            sentence_id != conn.sentence_id or conn.stop_event.is_set()):
        return
    # 完整 UTF-8 尾部快照允许设备合并过快更新，不会像丢 delta 一样缺字。
    text = text.encode("utf-8")[-300:].decode("utf-8", errors="ignore")
    try:
        await conn.websocket.send(json.dumps({
            "type": "llm", "state": "partial", "text": text, "final": final,
            "session_id": conn.session_id,
        }, ensure_ascii=False))
    except Exception as error:
        conn.logger.warning(f"LLM text preview unavailable: {type(error).__name__}")
