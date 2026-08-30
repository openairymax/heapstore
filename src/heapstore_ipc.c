// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_ipc.c
 * @brief AgentRT data partition IPC storage implementation.
 *
 * 本文件为 IPC 模块核心域：通道/缓冲区注册表、目录与生命周期管理。
 * JSON 持久化见 heapstore_ipc_persist.c；
 * POSIX 共享内存区域管理见 heapstore_ipc_shm.c；
 * 通道生命周期与消息传输见 heapstore_ipc_msg.c。
 */

// @owner: team-B
#include "heapstore_ipc_internal.h"

#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "airy_memory.h"
#include "logging_compat.h"

bool s_initialized = false;
airy_mtx_t s_ipc_lock = {0};
heapstore_ipc_channel_t s_channels[heapstore_IPC_MAX_CHANNELS];
size_t s_channel_count = 0;
heapstore_ipc_buffer_t s_buffers[heapstore_IPC_MAX_BUFFERS];
size_t s_buffer_count = 0;
char s_ipc_path[heapstore_IPC_MAX_PATH] = {0};

heapstore_error_t heapstore_ipc_init(void)
{
    if (s_initialized) {
        return heapstore_SUCCESS;
    }

    airy_mtx_init(&s_ipc_lock);

    char base_path[heapstore_IPC_MAX_PATH];
    snprintf(base_path, sizeof(base_path), "%s/kernel/ipc", heapstore_get_root());
    AIRY_STRNCPY_TERM(s_ipc_path, base_path, sizeof(s_ipc_path));

    heapstore_dir_ensure(s_ipc_path);

    char channels_path[heapstore_IPC_MAX_PATH];
    snprintf(channels_path, sizeof(channels_path), "%s/channels", s_ipc_path);
    heapstore_dir_ensure(channels_path);

    char buffers_path[heapstore_IPC_MAX_PATH];
    snprintf(buffers_path, sizeof(buffers_path), "%s/buffers", s_ipc_path);
    heapstore_dir_ensure(buffers_path);

    __builtin_memset(s_channels, 0, sizeof(s_channels));
    __builtin_memset(s_buffers, 0, sizeof(s_buffers));
    s_channel_count = 0;
    s_buffer_count = 0;

    s_initialized = true;

    AIRY_LOG_INFO("heapstore_ipc_init: IPC initialized (path=%s)", s_ipc_path);
    return heapstore_SUCCESS;
}

void heapstore_ipc_shutdown(void)
{
    if (!s_initialized) {
        return;
    }

    airy_mtx_lock(&s_ipc_lock);

#ifndef _WIN32
    ipc_shm_cleanup_all();
#endif

    AIRY_LOG_INFO("heapstore_ipc_shutdown: IPC shutdown (channels=%zu buffers=%zu)",
                  s_channel_count, s_buffer_count);
    __builtin_memset(s_channels, 0, sizeof(s_channels));
    __builtin_memset(s_buffers, 0, sizeof(s_buffers));
    s_channel_count = 0;
    s_buffer_count = 0;

    s_initialized = false;
    airy_mtx_unlock(&s_ipc_lock);
}

heapstore_error_t heapstore_ipc_record_channel(const heapstore_ipc_channel_t *channel)
{
    if (!s_initialized) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    if (!channel || channel->channel_id[0] == '\0') {
        return heapstore_ERR_INVALID_PARAM;
    }

    airy_mtx_lock(&s_ipc_lock);

    if (s_channel_count >= heapstore_IPC_MAX_CHANNELS) {
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < s_channel_count; i++) {
        if (strcmp(s_channels[i].channel_id, channel->channel_id) == 0) {
            __builtin_memcpy(&s_channels[i], channel, sizeof(heapstore_ipc_channel_t));
            airy_mtx_unlock(&s_ipc_lock);
            return heapstore_SUCCESS;
        }
    }

    __builtin_memcpy(&s_channels[s_channel_count], channel, sizeof(heapstore_ipc_channel_t));
    s_channel_count++;

    persist_channel_to_file(channel);

#ifndef _WIN32
    if (strcmp(channel->type, "shared_memory") == 0 && channel->buffer_size > 0) {
        find_or_create_shm(channel->channel_id, channel->buffer_size);
    }
#endif

    airy_mtx_unlock(&s_ipc_lock);

    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_ipc_get_channel(const char *channel_id,
                                            heapstore_ipc_channel_t *channel)
{
    if (!s_initialized) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    if (!channel_id || !channel) {
        return heapstore_ERR_INVALID_PARAM;
    }

    airy_mtx_lock(&s_ipc_lock);

    for (size_t i = 0; i < s_channel_count; i++) {
        if (strcmp(s_channels[i].channel_id, channel_id) == 0) {
            __builtin_memcpy(channel, &s_channels[i], sizeof(heapstore_ipc_channel_t));
            airy_mtx_unlock(&s_ipc_lock);
            return heapstore_SUCCESS;
        }
    }

    airy_mtx_unlock(&s_ipc_lock);

    heapstore_error_t file_err = load_channel_from_file(channel_id, channel);
    if (file_err == heapstore_SUCCESS) {
        airy_mtx_lock(&s_ipc_lock);
        if (s_channel_count < heapstore_IPC_MAX_CHANNELS) {
            __builtin_memcpy(&s_channels[s_channel_count], channel,
                             sizeof(heapstore_ipc_channel_t));
            s_channel_count++;
        }
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_SUCCESS;
    }

    return heapstore_ERR_NOT_FOUND;
}

heapstore_error_t heapstore_ipc_update_channel_activity(const char *channel_id)
{
    if (!s_initialized) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    if (!channel_id) {
        return heapstore_ERR_INVALID_PARAM;
    }

    airy_mtx_lock(&s_ipc_lock);

    for (size_t i = 0; i < s_channel_count; i++) {
        if (strcmp(s_channels[i].channel_id, channel_id) == 0) {
            s_channels[i].last_activity_at = (uint64_t)time(NULL);
            airy_mtx_unlock(&s_ipc_lock);
            return heapstore_SUCCESS;
        }
    }

    airy_mtx_unlock(&s_ipc_lock);
    return heapstore_ERR_NOT_FOUND;
}

heapstore_error_t heapstore_ipc_record_buffer(const heapstore_ipc_buffer_t *buffer)
{
    if (!s_initialized) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    if (!buffer || buffer->buffer_id[0] == '\0') {
        return heapstore_ERR_INVALID_PARAM;
    }

    airy_mtx_lock(&s_ipc_lock);

    if (s_buffer_count >= heapstore_IPC_MAX_BUFFERS) {
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < s_buffer_count; i++) {
        if (strcmp(s_buffers[i].buffer_id, buffer->buffer_id) == 0) {
            __builtin_memcpy(&s_buffers[i], buffer, sizeof(heapstore_ipc_buffer_t));
            airy_mtx_unlock(&s_ipc_lock);
            return heapstore_SUCCESS;
        }
    }

    __builtin_memcpy(&s_buffers[s_buffer_count], buffer, sizeof(heapstore_ipc_buffer_t));
    s_buffer_count++;

    persist_buffer_to_file(buffer);

    airy_mtx_unlock(&s_ipc_lock);

    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_ipc_get_buffer(const char *buffer_id, heapstore_ipc_buffer_t *buffer)
{
    if (!s_initialized) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    if (!buffer_id || !buffer) {
        return heapstore_ERR_INVALID_PARAM;
    }

    airy_mtx_lock(&s_ipc_lock);

    for (size_t i = 0; i < s_buffer_count; i++) {
        if (strcmp(s_buffers[i].buffer_id, buffer_id) == 0) {
            __builtin_memcpy(buffer, &s_buffers[i], sizeof(heapstore_ipc_buffer_t));
            airy_mtx_unlock(&s_ipc_lock);
            return heapstore_SUCCESS;
        }
    }

    airy_mtx_unlock(&s_ipc_lock);

    heapstore_error_t file_err = load_buffer_from_file(buffer_id, buffer);
    if (file_err == heapstore_SUCCESS) {
        airy_mtx_lock(&s_ipc_lock);
        if (s_buffer_count < heapstore_IPC_MAX_BUFFERS) {
            __builtin_memcpy(&s_buffers[s_buffer_count], buffer, sizeof(heapstore_ipc_buffer_t));
            s_buffer_count++;
        }
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_SUCCESS;
    }

    return heapstore_ERR_NOT_FOUND;
}

heapstore_error_t heapstore_ipc_get_stats(uint32_t *channel_count, uint32_t *buffer_count,
                                          uint64_t *total_size)
{
    if (!s_initialized) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    airy_mtx_lock(&s_ipc_lock);

    if (channel_count) {
        *channel_count = (uint32_t)s_channel_count;
    }
    if (buffer_count) {
        *buffer_count = (uint32_t)s_buffer_count;
    }
    if (total_size) {
        uint64_t size = 0;
        for (size_t i = 0; i < s_channel_count; i++) {
            size += s_channels[i].buffer_size;
        }
        for (size_t i = 0; i < s_buffer_count; i++) {
            size += s_buffers[i].size;
        }
        *total_size = size;
    }

    airy_mtx_unlock(&s_ipc_lock);

    return heapstore_SUCCESS;
}

bool heapstore_ipc_is_healthy(void)
{
    return s_initialized;
}
