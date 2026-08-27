// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_ipc_msg.c
 * @brief AgentRT data partition IPC 通道生命周期与消息传输。
 *
 * 本文件拆分自 heapstore_ipc.c，负责通道的创建/销毁与消息发送/接收：
 * - POSIX：基于命名共享内存的活动通道 + 消息传递
 * - Windows：基于内存缓冲区表的模拟通道
 */

// @owner: team-B
#include "heapstore_ipc_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "airy_memory.h"
#include "logging_compat.h"

#ifndef _WIN32

typedef struct {
    char channel_id[128];
    ipc_shm_region_t *shm;
    size_t data_len;
    atomic_uint_fast32_t ready_flag;
} ipc_active_channel_t;

#define IPC_ACTIVE_MAX 64
static ipc_active_channel_t s_active_channels[IPC_ACTIVE_MAX];
static size_t s_active_count = 0;

static ipc_active_channel_t *find_active_channel(const char *channel_id)
{
    for (size_t i = 0; i < s_active_count; i++) {
        if (strcmp(s_active_channels[i].channel_id, channel_id) == 0) {
            return &s_active_channels[i];
        }
    }
    return NULL;
}

static ipc_active_channel_t *create_active_channel(const char *channel_id, size_t buffer_size)
{
    if (find_active_channel(channel_id))
        return NULL;
    if (s_active_count >= IPC_ACTIVE_MAX)
        return NULL;

    ipc_active_channel_t *ac = &s_active_channels[s_active_count];
    __builtin_memset(ac, 0, sizeof(*ac));
    AIRY_STRNCPY_TERM(ac->channel_id, channel_id, sizeof(ac->channel_id));

    ac->shm = find_or_create_shm(channel_id, buffer_size);
    if (!ac->shm)
        return NULL;

    ac->data_len = 0;
    atomic_store_explicit(&ac->ready_flag, 0, memory_order_release);
    s_active_count++;
    return ac;
}

heapstore_error_t heapstore_ipc_create_channel(const char *channel_id, const char *name,
                                               const char *type, size_t buffer_size)
{
    if (!s_initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    if (!channel_id || !name || !type)
        return heapstore_ERR_INVALID_PARAM;
    if (buffer_size < 256)
        buffer_size = 65536;

    airy_mtx_lock(&s_ipc_lock);

    ipc_active_channel_t *existing = find_active_channel(channel_id);
    if (existing) {
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_SUCCESS;
    }

    ipc_active_channel_t *ac = create_active_channel(channel_id, buffer_size);
    if (!ac) {
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_ERR_INTERNAL;
    }

    heapstore_ipc_channel_t ch;
    __builtin_memset(&ch, 0, sizeof(ch));
    AIRY_STRNCPY_TERM(ch.channel_id, channel_id, sizeof(ch.channel_id));
    AIRY_STRNCPY_TERM(ch.name, name, sizeof(ch.name));
    AIRY_STRNCPY_TERM(ch.type, type, sizeof(ch.type));
    AIRY_STRNCPY_TERM(ch.status, "open", sizeof(ch.status));
    ch.buffer_size = buffer_size;
    ch.created_at = (uint64_t)time(NULL);
    ch.last_activity_at = ch.created_at;

    __builtin_memcpy(&s_channels[s_channel_count], &ch, sizeof(heapstore_ipc_channel_t));
    s_channel_count++;

    airy_mtx_unlock(&s_ipc_lock);
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_ipc_destroy_channel(const char *channel_id)
{
    if (!s_initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    if (!channel_id)
        return heapstore_ERR_INVALID_PARAM;

    airy_mtx_lock(&s_ipc_lock);

    for (size_t i = 0; i < s_active_count; i++) {
        if (strcmp(s_active_channels[i].channel_id, channel_id) == 0) {
            (void)&s_active_channels[i];

            ipc_shm_release(channel_id);

            if (i < s_active_count - 1) {
                __builtin_memmove(&s_active_channels[i], &s_active_channels[s_active_count - 1],
                                  sizeof(ipc_active_channel_t));
            }
            s_active_count--;
            break;
        }
    }

    for (size_t i = 0; i < s_channel_count; i++) {
        if (strcmp(s_channels[i].channel_id, channel_id) == 0) {
            if (i < s_channel_count - 1) {
                __builtin_memmove(&s_channels[i], &s_channels[s_channel_count - 1],
                                  sizeof(heapstore_ipc_channel_t));
            }
            s_channel_count--;
            break;
        }
    }

    airy_mtx_unlock(&s_ipc_lock);
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_ipc_send(const char *channel_id, const void *data, size_t len)
{
    if (!s_initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    if (!channel_id || !data || len == 0)
        return heapstore_ERR_INVALID_PARAM;

    airy_mtx_lock(&s_ipc_lock);

    ipc_active_channel_t *ac = find_active_channel(channel_id);
    if (!ac || !ac->shm) {
        heapstore_ipc_create_channel(channel_id, channel_id, "auto", len + 256);
        ac = find_active_channel(channel_id);
        if (!ac) {
            AIRY_LOG_ERROR("heapstore_ipc_send: failed to create channel '%s'", channel_id);
            airy_mtx_unlock(&s_ipc_lock);
            return heapstore_ERR_INTERNAL;
        }
    }

    size_t header_size = sizeof(uint32_t) * 2;
    if (len + header_size > ac->shm->mapped_size) {
        AIRY_LOG_ERROR(
            "heapstore_ipc_send: message too large for channel '%s' (len=%zu mapped=%zu)",
            channel_id, len, ac->shm->mapped_size);
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_ERR_INTERNAL;
    }

    volatile uint32_t *msg_len = (volatile uint32_t *)ac->shm->mapped;
    volatile uint32_t *msg_ready =
        (volatile uint32_t *)((char *)ac->shm->mapped + sizeof(uint32_t));

    *msg_len = (uint32_t)len;
    __builtin_memcpy((char *)ac->shm->mapped + header_size, data, len);
    atomic_thread_fence(memory_order_seq_cst);
    *msg_ready = 1;
    ac->data_len = len;

    for (size_t i = 0; i < s_channel_count; i++) {
        if (strcmp(s_channels[i].channel_id, channel_id) == 0) {
            s_channels[i].last_activity_at = (uint64_t)time(NULL);
            s_channels[i].current_usage = len;
            break;
        }
    }

    airy_mtx_unlock(&s_ipc_lock);
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_ipc_receive(const char *channel_id, void **out_data, size_t *out_len)
{
    if (!s_initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    if (!channel_id || !out_data || !out_len)
        return heapstore_ERR_INVALID_PARAM;

    *out_data = NULL;
    *out_len = 0;

    airy_mtx_lock(&s_ipc_lock);

    ipc_active_channel_t *ac = find_active_channel(channel_id);
    if (!ac || !ac->shm) {
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_ERR_NOT_FOUND;
    }

    volatile uint32_t *msg_ready =
        (volatile uint32_t *)((char *)ac->shm->mapped + sizeof(uint32_t));
    atomic_thread_fence(memory_order_seq_cst);

    if (*msg_ready != 1) {
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_ERR_NOT_FOUND;
    }

    volatile uint32_t *msg_len = (volatile uint32_t *)ac->shm->mapped;
    uint32_t len = *msg_len;
    if (len == 0 || len > ac->shm->mapped_size - sizeof(uint32_t) * 2) {
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_ERR_INVALID_PARAM;
    }

    void *buf = AIRY_MALLOC(len);
    if (!buf) {
        AIRY_LOG_ERROR("heapstore_ipc_receive: alloc failed for channel '%s' (len=%u)", channel_id,
                       len);
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    __builtin_memcpy(buf, (char *)ac->shm->mapped + sizeof(uint32_t) * 2, len);
    atomic_thread_fence(memory_order_seq_cst);
    *msg_ready = 0;
    *msg_len = 0;
    ac->data_len = 0;

    *out_data = buf;
    *out_len = len;

    airy_mtx_unlock(&s_ipc_lock);
    return heapstore_SUCCESS;
}

#else

heapstore_error_t heapstore_ipc_create_channel(const char *channel_id, const char *name,
                                               const char *type, size_t buffer_size)
{
    if (!channel_id || !name || !type)
        return heapstore_ERR_INVALID_PARAM;
    airy_mtx_lock(&s_ipc_lock);
    if (s_channel_count >= heapstore_IPC_MAX_CHANNELS) {
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_ERR_NO_SPACE;
    }
    heapstore_ipc_channel_t *ch = &s_channels[s_channel_count];
    snprintf(ch->channel_id, sizeof(ch->channel_id), "%s", channel_id);
    snprintf(ch->name, sizeof(ch->name), "%s", name);
    snprintf(ch->type, sizeof(ch->type), "%s", type);
    snprintf(ch->status, sizeof(ch->status), "active");
    ch->buffer_size = buffer_size > 0 ? buffer_size : 4096;
    ch->current_usage = 0;
    ch->created_at = (uint64_t)time(NULL);
    ch->last_activity_at = ch->created_at;
    s_channel_count++;
    persist_channel_to_file(ch);
    airy_mtx_unlock(&s_ipc_lock);
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_ipc_destroy_channel(const char *channel_id)
{
    if (!channel_id)
        return heapstore_ERR_INVALID_PARAM;
    airy_mtx_lock(&s_ipc_lock);
    for (size_t i = 0; i < s_channel_count; i++) {
        if (strcmp(s_channels[i].channel_id, channel_id) == 0) {
            s_channels[i] = s_channels[s_channel_count - 1];
            __builtin_memset(&s_channels[s_channel_count - 1], 0, sizeof(heapstore_ipc_channel_t));
            s_channel_count--;
            airy_mtx_unlock(&s_ipc_lock);
            return heapstore_SUCCESS;
        }
    }
    airy_mtx_unlock(&s_ipc_lock);
    return heapstore_ERR_NOT_FOUND;
}

heapstore_error_t heapstore_ipc_send(const char *channel_id, const void *data, size_t len)
{
    if (!channel_id || !data || len == 0)
        return heapstore_ERR_INVALID_PARAM;
    airy_mtx_lock(&s_ipc_lock);
    heapstore_ipc_channel_t *ch = NULL;
    for (size_t i = 0; i < s_channel_count; i++) {
        if (strcmp(s_channels[i].channel_id, channel_id) == 0) {
            ch = &s_channels[i];
            break;
        }
    }
    if (!ch) {
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_ERR_NOT_FOUND;
    }
    if (len > ch->buffer_size) {
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_ERR_NO_SPACE;
    }
    if (s_buffer_count >= heapstore_IPC_MAX_BUFFERS) {
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_ERR_NO_SPACE;
    }
    heapstore_ipc_buffer_t *buf = &s_buffers[s_buffer_count];
    snprintf(buf->buffer_id, sizeof(buf->buffer_id), "buf_%zu_%llu", s_buffer_count,
             (unsigned long long)time(NULL));
    snprintf(buf->channel_id, sizeof(buf->channel_id), "%s", channel_id);
    buf->size = ch->buffer_size;
    buf->used = len;
    snprintf(buf->status, sizeof(buf->status), "ready");
    buf->created_at = (uint64_t)time(NULL);
    s_buffer_count++;
    ch->current_usage += len;
    ch->last_activity_at = (uint64_t)time(NULL);
    persist_buffer_to_file(buf);
    persist_channel_to_file(ch);
    airy_mtx_unlock(&s_ipc_lock);
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_ipc_receive(const char *channel_id, void **out_data, size_t *out_len)
{
    if (!channel_id || !out_data || !out_len)
        return heapstore_ERR_INVALID_PARAM;
    airy_mtx_lock(&s_ipc_lock);
    heapstore_ipc_buffer_t *found = NULL;
    for (size_t i = 0; i < s_buffer_count; i++) {
        if (strcmp(s_buffers[i].channel_id, channel_id) == 0 &&
            strcmp(s_buffers[i].status, "ready") == 0) {
            found = &s_buffers[i];
            break;
        }
    }
    if (!found) {
        *out_data = NULL;
        *out_len = 0;
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_ERR_NOT_FOUND;
    }
    void *data = AIRY_MALLOC(found->used > 0 ? found->used : 1);
    if (!data) {
        airy_mtx_unlock(&s_ipc_lock);
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    __builtin_memset(data, 0, found->used > 0 ? found->used : 1);
    *out_data = data;
    *out_len = found->used;
    snprintf(found->status, sizeof(found->status), "consumed");
    for (size_t i = 0; i < s_channel_count; i++) {
        if (strcmp(s_channels[i].channel_id, channel_id) == 0) {
            s_channels[i].current_usage -= found->used;
            s_channels[i].last_activity_at = (uint64_t)time(NULL);
            persist_channel_to_file(&s_channels[i]);
            break;
        }
    }
    airy_mtx_unlock(&s_ipc_lock);
    return heapstore_SUCCESS;
}

#endif
