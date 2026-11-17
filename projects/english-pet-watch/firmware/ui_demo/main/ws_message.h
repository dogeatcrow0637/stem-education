#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define EPW_WS_MESSAGE_MAX 8192

typedef struct {
    char data[EPW_WS_MESSAGE_MAX + 1];
    size_t used;
    size_t frame_offset;
    unsigned opcode;
    bool collecting;
} epw_ws_message_t;

/* 同时处理 WebSocket continuation 帧和 SDK 对单帧的分块回调。 */
static inline bool epw_ws_message_feed(epw_ws_message_t *msg, unsigned opcode, bool fin,
                                       size_t offset, size_t total, const char *data, size_t len)
{
    if (opcode > 2) {
        return false; /* ping/pong 不打断正在接收的消息 */
    }
    if (offset == 0) {
        if (opcode == 1 || opcode == 2) {
            msg->used = 0;
            msg->opcode = opcode;
            msg->collecting = true;
        }
        msg->frame_offset = 0;
    }
    if (!msg->collecting || offset != msg->frame_offset || offset > total ||
        len > total - offset || len > EPW_WS_MESSAGE_MAX - msg->used ||
        (len != 0 && data == NULL)) {
        msg->collecting = false;
        return false;
    }
    if (len != 0) {
        memcpy(msg->data + msg->used, data, len);
    }
    msg->used += len;
    msg->frame_offset += len;
    if (!fin || msg->frame_offset != total) {
        return false;
    }
    msg->data[msg->used] = '\0';
    msg->collecting = false;
    return true;
}
