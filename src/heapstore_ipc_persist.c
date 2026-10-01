// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_ipc_persist.c
 * @brief AgentRT data partition IPC 通道/缓冲区 JSON 持久化。
 *
 * 本文件拆分自 heapstore_ipc.c，负责通道与缓冲区的 JSON 文件持久化
 * 及从文件加载恢复。
 */

// @owner: team-B
#include "heapstore_ipc_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

heapstore_error_t persist_channel_to_file(const heapstore_ipc_channel_t *channel)
{
    if (!channel || !s_ipc_path[0])
        return heapstore_ERR_INVALID_PARAM;
    char path[heapstore_IPC_MAX_PATH + 256];
    snprintf(path, sizeof(path), "%s/channels/%s.json", s_ipc_path, channel->channel_id);
    FILE *fp = fopen(path, "w");
    if (!fp)
        return heapstore_ERR_FILE_OPEN_FAILED;
    char _buf[1024];
    snprintf(_buf, sizeof(_buf),
             "{\"channel_id\":\"%s\",\"name\":\"%s\",\"type\":\"%s\","
             "\"status\":\"%s\",\"created_at\":%llu,"
             "\"last_activity_at\":%llu,\"buffer_size\":%zu,"
             "\"current_usage\":%zu}\n",
             channel->channel_id, channel->name, channel->type, channel->status,
             (unsigned long long)channel->created_at, (unsigned long long)channel->last_activity_at,
             channel->buffer_size, channel->current_usage);
    fputs(_buf, fp);
    fclose(fp);
    return heapstore_SUCCESS;
}

heapstore_error_t persist_buffer_to_file(const heapstore_ipc_buffer_t *buffer)
{
    if (!buffer || !s_ipc_path[0])
        return heapstore_ERR_INVALID_PARAM;
    char path[heapstore_IPC_MAX_PATH + 256];
    snprintf(path, sizeof(path), "%s/buffers/%s.json", s_ipc_path, buffer->buffer_id);
    FILE *fp = fopen(path, "w");
    if (!fp)
        return heapstore_ERR_FILE_OPEN_FAILED;
    char _buf[1024];
    snprintf(_buf, sizeof(_buf),
             "{\"buffer_id\":\"%s\",\"channel_id\":\"%s\","
             "\"size\":%zu,\"used\":%zu,"
             "\"created_at\":%llu,\"status\":\"%s\"}\n",
             buffer->buffer_id, buffer->channel_id, buffer->size, buffer->used,
             (unsigned long long)buffer->created_at, buffer->status);
    fputs(_buf, fp);
    fclose(fp);
    return heapstore_SUCCESS;
}

/**
 * 从 JSON 行中抽取字符串字段：定位 key 后复制至下一个引号为止。
 * 字段缺失或引号不闭合时保持空串。
 */
static void json_take_str(const char *buf, const char *key, char *dst, size_t dst_size)
{
    dst[0] = '\0';
    const char *v = strstr(buf, key);
    if (!v) {
        return;
    }
    v += strlen(key);
    const char *e = strchr(v, '"');
    if (!e) {
        return;
    }
    size_t len = (size_t)(e - v);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    __builtin_memcpy(dst, v, len);
    dst[len] = '\0';
}

/** 从 JSON 行中抽取无符号整数字段，字段缺失时返回 0。 */
static uint64_t json_take_u64(const char *buf, const char *key)
{
    const char *v = strstr(buf, key);
    return v ? strtoull(v + strlen(key), NULL, 10) : 0;
}

heapstore_error_t load_channel_from_file(const char *channel_id,
                                         heapstore_ipc_channel_t *channel)
{
    if (!channel_id || !channel || !s_ipc_path[0])
        return heapstore_ERR_INVALID_PARAM;
    char path[heapstore_IPC_MAX_PATH + 256];
    snprintf(path, sizeof(path), "%s/channels/%s.json", s_ipc_path, channel_id);
    FILE *fp = fopen(path, "r");
    if (!fp)
        return heapstore_ERR_NOT_FOUND;
    __builtin_memset(channel, 0, sizeof(*channel));
    char buf[2048];
    if (fgets(buf, sizeof(buf), fp)) {
        json_take_str(buf, "\"channel_id\":\"", channel->channel_id, sizeof(channel->channel_id));
        json_take_str(buf, "\"name\":\"", channel->name, sizeof(channel->name));
        json_take_str(buf, "\"type\":\"", channel->type, sizeof(channel->type));
        json_take_str(buf, "\"status\":\"", channel->status, sizeof(channel->status));
        channel->buffer_size = (size_t)json_take_u64(buf, "\"buffer_size\":");
        channel->current_usage = (size_t)json_take_u64(buf, "\"current_usage\":");
        channel->created_at = json_take_u64(buf, "\"created_at\":");
        channel->last_activity_at = json_take_u64(buf, "\"last_activity_at\":");
    }
    fclose(fp);
    return heapstore_SUCCESS;
}

heapstore_error_t load_buffer_from_file(const char *buffer_id,
                                        heapstore_ipc_buffer_t *buffer)
{
    if (!buffer_id || !buffer || !s_ipc_path[0])
        return heapstore_ERR_INVALID_PARAM;
    char path[heapstore_IPC_MAX_PATH + 256];
    snprintf(path, sizeof(path), "%s/buffers/%s.json", s_ipc_path, buffer_id);
    FILE *fp = fopen(path, "r");
    if (!fp)
        return heapstore_ERR_NOT_FOUND;
    __builtin_memset(buffer, 0, sizeof(*buffer));
    char buf[2048];
    if (fgets(buf, sizeof(buf), fp)) {
        json_take_str(buf, "\"buffer_id\":\"", buffer->buffer_id, sizeof(buffer->buffer_id));
        json_take_str(buf, "\"channel_id\":\"", buffer->channel_id, sizeof(buffer->channel_id));
        buffer->size = (size_t)json_take_u64(buf, "\"size\":");
        buffer->used = (size_t)json_take_u64(buf, "\"used\":");
        buffer->created_at = json_take_u64(buf, "\"created_at\":");
        json_take_str(buf, "\"status\":\"", buffer->status, sizeof(buffer->status));
    }
    fclose(fp);
    return heapstore_SUCCESS;
}
