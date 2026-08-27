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
        char *v;
        if ((v = strstr(buf, "\"channel_id\":\""))) {
            v += 14;
            char *e = strchr(v, '"');
            if (e) {
                size_t l = (size_t)(e - v);
                if (l >= sizeof(channel->channel_id))
                    l = sizeof(channel->channel_id) - 1;
                __builtin_memcpy(channel->channel_id, v, l);
            }
        }
        if ((v = strstr(buf, "\"name\":\""))) {
            v += 8;
            char *e = strchr(v, '"');
            if (e) {
                size_t l = (size_t)(e - v);
                if (l >= sizeof(channel->name))
                    l = sizeof(channel->name) - 1;
                __builtin_memcpy(channel->name, v, l);
            }
        }
        if ((v = strstr(buf, "\"type\":\""))) {
            v += 8;
            char *e = strchr(v, '"');
            if (e) {
                size_t l = (size_t)(e - v);
                if (l >= sizeof(channel->type))
                    l = sizeof(channel->type) - 1;
                __builtin_memcpy(channel->type, v, l);
            }
        }
        if ((v = strstr(buf, "\"status\":\""))) {
            v += 10;
            char *e = strchr(v, '"');
            if (e) {
                size_t l = (size_t)(e - v);
                if (l >= sizeof(channel->status))
                    l = sizeof(channel->status) - 1;
                __builtin_memcpy(channel->status, v, l);
            }
        }
        if ((v = strstr(buf, "\"buffer_size\":"))) {
            channel->buffer_size = (size_t)atoll(v + 14);
        }
        if ((v = strstr(buf, "\"current_usage\":"))) {
            channel->current_usage = (size_t)atoll(v + 16);
        }
        if ((v = strstr(buf, "\"created_at\":"))) {
            channel->created_at = (uint64_t)strtoull(v + 13, NULL, 10);
        }
        if ((v = strstr(buf, "\"last_activity_at\":"))) {
            channel->last_activity_at = (uint64_t)strtoull(v + 20, NULL, 10);
        }
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
        char *v;
        if ((v = strstr(buf, "\"buffer_id\":\""))) {
            v += 13;
            char *e = strchr(v, '"');
            if (e) {
                size_t l = (size_t)(e - v);
                if (l >= sizeof(buffer->buffer_id))
                    l = sizeof(buffer->buffer_id) - 1;
                __builtin_memcpy(buffer->buffer_id, v, l);
            }
        }
        if ((v = strstr(buf, "\"channel_id\":\""))) {
            v += 14;
            char *e = strchr(v, '"');
            if (e) {
                size_t l = (size_t)(e - v);
                if (l >= sizeof(buffer->channel_id))
                    l = sizeof(buffer->channel_id) - 1;
                __builtin_memcpy(buffer->channel_id, v, l);
            }
        }
        if ((v = strstr(buf, "\"size\":"))) {
            buffer->size = (size_t)atoll(v + 7);
        }
        if ((v = strstr(buf, "\"used\":"))) {
            buffer->used = (size_t)atoll(v + 7);
        }
        if ((v = strstr(buf, "\"status\":\""))) {
            v += 10;
            char *e = strchr(v, '"');
            if (e) {
                size_t l = (size_t)(e - v);
                if (l >= sizeof(buffer->status))
                    l = sizeof(buffer->status) - 1;
                __builtin_memcpy(buffer->status, v, l);
            }
        }
        if ((v = strstr(buf, "\"created_at\":"))) {
            buffer->created_at = (uint64_t)strtoull(v + 13, NULL, 10);
        }
    }
    fclose(fp);
    return heapstore_SUCCESS;
}
