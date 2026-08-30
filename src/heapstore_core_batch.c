// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_core_batch.c
 * @brief Batch-write domain: batch context management and record enqueueing.
 */

// @owner: team-B
#include "heapstore.h"
#include "heapstore_batch.h"
#include "heapstore_ipc.h"
#include "heapstore_log.h"
#include "heapstore_memory.h"
#include "heapstore_migration.h"
#include "heapstore_registry.h"
#include "heapstore_trace.h"
#include "logging.h"
#include "logging_compat.h"
#include "platform.h"
#include "private.h"
#include "utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "airy_memory.h"

#include "atomic_compat.h"

/** 将条目追加到 batch 链表尾部（线程安全，持有上下文锁）。 */
static heapstore_error_t batch_append_item(heapstore_batch_context_t *ctx,
                                           heapstore_batch_item_t *item)
{
    if (!ctx || !item) {
        AIRY_FREE(item);
        return heapstore_ERR_INVALID_PARAM;
    }
    if (ctx->count >= ctx->capacity) {
        AIRY_FREE(item);
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    airy_mtx_lock(&ctx->lock);
    if (ctx->tail) {
        ctx->tail->next = item;
        ctx->tail = item;
    } else {
        ctx->head = ctx->tail = item;
    }
    ctx->count++;
    airy_mtx_unlock(&ctx->lock);
    return heapstore_SUCCESS;
}

heapstore_batch_context_t *heapstore_batch_begin(size_t batch_size)
{
    heapstore_batch_context_t *ctx =
        (heapstore_batch_context_t *)AIRY_MALLOC(sizeof(heapstore_batch_context_t));
    if (!ctx) {
        return NULL;
    }
    __builtin_memset(ctx, 0, sizeof(heapstore_batch_context_t));
    ctx->capacity = (batch_size > 0) ? batch_size : HEAPSTORE_BATCH_MAX_ITEMS;
    if (ctx->capacity > HEAPSTORE_BATCH_MAX_ITEMS) {
        ctx->capacity = HEAPSTORE_BATCH_MAX_ITEMS;
    }
#ifdef _WIN32
    airy_mtx_init(&ctx->lock);
#else
    airy_mtx_init(&ctx->lock);
#endif
    return ctx;
}

heapstore_error_t heapstore_batch_add_log(heapstore_batch_context_t *ctx, const char *service,
                                          int level, const char *message)
{
    if (!ctx || !service || !message) {
        return heapstore_ERR_INVALID_PARAM;
    }
    if (ctx->count >= ctx->capacity) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    heapstore_batch_item_t *item =
        (heapstore_batch_item_t *)AIRY_MALLOC(sizeof(heapstore_batch_item_t));
    if (!item) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    __builtin_memset(item, 0, sizeof(heapstore_batch_item_t));
    item->type = HEAPSTORE_BATCH_ITEM_LOG;
    AIRY_STRNCPY_TERM(item->data.log.service, service, sizeof(item->data.log.service));
    item->data.log.level = level;
    if (message) {
        AIRY_STRNCPY_TERM(item->data.log.message, message, sizeof(item->data.log.message));
    }

    return batch_append_item(ctx, item);
}

heapstore_error_t heapstore_batch_add_log_with_trace(heapstore_batch_context_t *ctx,
                                                     const char *service, int level,
                                                     const char *trace_id, const char *message)
{
    if (!ctx || !service || !message) {
        return heapstore_ERR_INVALID_PARAM;
    }
    if (ctx->count >= ctx->capacity) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    heapstore_batch_item_t *item =
        (heapstore_batch_item_t *)AIRY_MALLOC(sizeof(heapstore_batch_item_t));
    if (!item) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    __builtin_memset(item, 0, sizeof(heapstore_batch_item_t));
    item->type = HEAPSTORE_BATCH_ITEM_LOG;
    AIRY_STRNCPY_TERM(item->data.log.service, service, sizeof(item->data.log.service));
    item->data.log.level = level;
    if (trace_id) {
        AIRY_STRNCPY_TERM(item->data.log.trace_id, trace_id, sizeof(item->data.log.trace_id));
    }
    if (message) {
        AIRY_STRNCPY_TERM(item->data.log.message, message, sizeof(item->data.log.message));
    }

    return batch_append_item(ctx, item);
}

heapstore_error_t heapstore_batch_add_trace(heapstore_batch_context_t *ctx, const char *trace_id,
                                            const char *span_id, const char *parent_span_id,
                                            const char *name, int64_t start_time_us,
                                            int64_t end_time_us, int status, const char *attributes)
{
    if (!ctx || !trace_id || !span_id || !name) {
        return heapstore_ERR_INVALID_PARAM;
    }
    if (ctx->count >= ctx->capacity) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    heapstore_batch_item_t *item =
        (heapstore_batch_item_t *)AIRY_MALLOC(sizeof(heapstore_batch_item_t));
    if (!item) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    __builtin_memset(item, 0, sizeof(heapstore_batch_item_t));
    item->type = HEAPSTORE_BATCH_ITEM_SPAN;
    AIRY_STRNCPY_TERM(item->data.span.trace_id, trace_id, sizeof(item->data.span.trace_id));
    AIRY_STRNCPY_TERM(item->data.span.span_id, span_id, sizeof(item->data.span.span_id));
    if (parent_span_id) {
        AIRY_STRNCPY_TERM(item->data.span.parent_span_id, parent_span_id,
                          sizeof(item->data.span.parent_span_id));
    }
    AIRY_STRNCPY_TERM(item->data.span.name, name, sizeof(item->data.span.name));
    item->data.span.start_time_us = start_time_us;
    item->data.span.end_time_us = end_time_us;
    item->data.span.status = status;
    if (attributes) {
        AIRY_STRNCPY_TERM(item->data.span.attributes, attributes,
                          sizeof(item->data.span.attributes));
    }

    return batch_append_item(ctx, item);
}

heapstore_error_t heapstore_batch_add_session(heapstore_batch_context_t *ctx,
                                              const heapstore_session_record_t *record)
{
    if (!ctx || !record) {
        return heapstore_ERR_INVALID_PARAM;
    }
    if (ctx->count >= ctx->capacity) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    heapstore_batch_item_t *item =
        (heapstore_batch_item_t *)AIRY_MALLOC(sizeof(heapstore_batch_item_t));
    if (!item) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    __builtin_memset(item, 0, sizeof(heapstore_batch_item_t));
    item->type = HEAPSTORE_BATCH_ITEM_SESSION;
    __builtin_memcpy(&item->data.session, record, sizeof(heapstore_session_record_t));

    return batch_append_item(ctx, item);
}

heapstore_error_t heapstore_batch_add_agent(heapstore_batch_context_t *ctx,
                                            const heapstore_agent_record_t *record)
{
    if (!ctx || !record) {
        return heapstore_ERR_INVALID_PARAM;
    }
    if (ctx->count >= ctx->capacity) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    heapstore_batch_item_t *item =
        (heapstore_batch_item_t *)AIRY_MALLOC(sizeof(heapstore_batch_item_t));
    if (!item) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    __builtin_memset(item, 0, sizeof(heapstore_batch_item_t));
    item->type = HEAPSTORE_BATCH_ITEM_AGENT;
    __builtin_memcpy(&item->data.agent, record, sizeof(heapstore_agent_record_t));

    return batch_append_item(ctx, item);
}

heapstore_error_t heapstore_batch_add_skill(heapstore_batch_context_t *ctx,
                                            const heapstore_skill_record_t *record)
{
    if (!ctx || !record) {
        return heapstore_ERR_INVALID_PARAM;
    }
    if (ctx->count >= ctx->capacity) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    heapstore_batch_item_t *item =
        (heapstore_batch_item_t *)AIRY_MALLOC(sizeof(heapstore_batch_item_t));
    if (!item) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    __builtin_memset(item, 0, sizeof(heapstore_batch_item_t));
    item->type = HEAPSTORE_BATCH_ITEM_SKILL;
    __builtin_memcpy(&item->data.skill, record, sizeof(heapstore_skill_record_t));

    return batch_append_item(ctx, item);
}

heapstore_error_t heapstore_batch_add_memory_pool(heapstore_batch_context_t *ctx,
                                                  const heapstore_memory_pool_t *pool)
{
    if (!ctx || !pool) {
        return heapstore_ERR_INVALID_PARAM;
    }
    if (ctx->count >= ctx->capacity) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    heapstore_batch_item_t *item =
        (heapstore_batch_item_t *)AIRY_MALLOC(sizeof(heapstore_batch_item_t));
    if (!item) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    __builtin_memset(item, 0, sizeof(heapstore_batch_item_t));
    item->type = HEAPSTORE_BATCH_ITEM_MEMORY_POOL;
    __builtin_memcpy(&item->data.memory_pool, pool, sizeof(heapstore_memory_pool_t));

    return batch_append_item(ctx, item);
}

heapstore_error_t heapstore_batch_add_allocation(heapstore_batch_context_t *ctx,
                                                 const heapstore_memory_allocation_t *allocation)
{
    if (!ctx || !allocation) {
        return heapstore_ERR_INVALID_PARAM;
    }
    if (ctx->count >= ctx->capacity) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    heapstore_batch_item_t *item =
        (heapstore_batch_item_t *)AIRY_MALLOC(sizeof(heapstore_batch_item_t));
    if (!item) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    __builtin_memset(item, 0, sizeof(heapstore_batch_item_t));
    item->type = HEAPSTORE_BATCH_ITEM_MEMORY_ALLOC;
    __builtin_memcpy(&item->data.memory_alloc, allocation, sizeof(heapstore_memory_allocation_t));

    return batch_append_item(ctx, item);
}

heapstore_error_t heapstore_batch_add_ipc_channel(heapstore_batch_context_t *ctx,
                                                  const heapstore_ipc_channel_t *channel)
{
    if (!ctx || !channel) {
        return heapstore_ERR_INVALID_PARAM;
    }
    if (ctx->count >= ctx->capacity) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    heapstore_batch_item_t *item =
        (heapstore_batch_item_t *)AIRY_MALLOC(sizeof(heapstore_batch_item_t));
    if (!item) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    __builtin_memset(item, 0, sizeof(heapstore_batch_item_t));
    item->type = HEAPSTORE_BATCH_ITEM_IPC_CHANNEL;
    __builtin_memcpy(&item->data.ipc_channel, channel, sizeof(heapstore_ipc_channel_t));

    return batch_append_item(ctx, item);
}

heapstore_error_t heapstore_batch_add_ipc_buffer(heapstore_batch_context_t *ctx,
                                                 const heapstore_ipc_buffer_t *buffer)
{
    if (!ctx || !buffer) {
        return heapstore_ERR_INVALID_PARAM;
    }
    if (ctx->count >= ctx->capacity) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    heapstore_batch_item_t *item =
        (heapstore_batch_item_t *)AIRY_MALLOC(sizeof(heapstore_batch_item_t));
    if (!item) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    __builtin_memset(item, 0, sizeof(heapstore_batch_item_t));
    item->type = HEAPSTORE_BATCH_ITEM_IPC_BUFFER;
    __builtin_memcpy(&item->data.ipc_buffer, buffer, sizeof(heapstore_ipc_buffer_t));

    return batch_append_item(ctx, item);
}

heapstore_error_t heapstore_batch_add_span(heapstore_batch_context_t *ctx,
                                           const heapstore_span_t *span)
{
    if (!ctx || !span) {
        return heapstore_ERR_INVALID_PARAM;
    }
    /* span 使用 ns 时间戳，batch 内部按 us 存储（commit 时 *1000 还原为 ns）；
     * 此处换算，避免时间被放大 1000 倍。status 透传，不再硬编码 0。 */
    int status_int = 0;
    if (span->status[0] >= '0' && span->status[0] <= '9')
        status_int = span->status[0] - '0';
    else if (strcmp(span->status, "error") == 0)
        status_int = 2;
    else if (strcmp(span->status, "ok") == 0 || strcmp(span->status, "done") == 0)
        status_int = 1;
    return heapstore_batch_add_trace(ctx, span->trace_id, span->span_id, span->parent_span_id,
                                     span->name, span->start_time_ns / 1000,
                                     span->end_time_ns / 1000, status_int, span->attributes);
}

void heapstore_batch_rollback(heapstore_batch_context_t *ctx)
{
    if (!ctx) {
        return;
    }

    heapstore_batch_item_t *item;

    airy_mtx_lock(&ctx->lock);
    item = ctx->head;
    ctx->head = ctx->tail = NULL;
    ctx->count = 0;
    airy_mtx_unlock(&ctx->lock);

    while (item) {
        heapstore_batch_item_t *next = item->next;
        AIRY_FREE(item);
        item = next;
    }
}

void heapstore_batch_context_destroy(heapstore_batch_context_t *ctx)
{
    if (!ctx) {
        return;
    }

    heapstore_batch_rollback(ctx);
#ifdef _WIN32
    airy_mtx_destroy(&ctx->lock);
#else
    airy_mtx_destroy(&ctx->lock);
#endif
    AIRY_FREE(ctx);
}

size_t heapstore_batch_get_count(const heapstore_batch_context_t *ctx)
{
    if (!ctx) {
        return 0;
    }
    return ctx->count;
}

size_t heapstore_batch_get_capacity(const heapstore_batch_context_t *ctx)
{
    if (!ctx) {
        return 0;
    }
    return ctx->capacity;
}

/** 按条目类型落库（LOG/SPAN 走日志与追踪通道，其余走 registry/memory/ipc）。 */
static heapstore_error_t batch_commit_single_item(const heapstore_batch_item_t *item)
{
    switch (item->type) {
    case HEAPSTORE_BATCH_ITEM_LOG:
        if (!item->data.log.message[0]) {
            return heapstore_ERR_INVALID_PARAM;
        }
        heapstore_log_write((heapstore_log_level_t)item->data.log.level, item->data.log.service,
                            item->data.log.trace_id[0] ? item->data.log.trace_id : NULL, NULL, 0,
                            item->data.log.message);
        return heapstore_SUCCESS;
    case HEAPSTORE_BATCH_ITEM_SPAN: {
        heapstore_span_t span_rec;
        __builtin_memset(&span_rec, 0, sizeof(span_rec));
        AIRY_STRNCPY_TERM(span_rec.trace_id, item->data.span.trace_id,
                          sizeof(span_rec.trace_id));
        AIRY_STRNCPY_TERM(span_rec.span_id, item->data.span.span_id, sizeof(span_rec.span_id));
        AIRY_STRNCPY_TERM(span_rec.parent_span_id, item->data.span.parent_span_id,
                          sizeof(span_rec.parent_span_id));
        AIRY_STRNCPY_TERM(span_rec.name, item->data.span.name, sizeof(span_rec.name));
        span_rec.start_time_ns = (uint64_t)item->data.span.start_time_us * 1000ULL;
        span_rec.end_time_ns = (uint64_t)item->data.span.end_time_us * 1000ULL;
        snprintf(span_rec.status, sizeof(span_rec.status), "%d", item->data.span.status);
        if (item->data.span.attributes[0]) {
            span_rec.attributes = AIRY_STRDUP(item->data.span.attributes);
            if (!span_rec.attributes) {
                return heapstore_ERR_OUT_OF_MEMORY;
            }
            span_rec.attribute_count = 1;
        }
        heapstore_error_t err = heapstore_trace_write_span(&span_rec);
        if (span_rec.attributes) {
            AIRY_FREE(span_rec.attributes);
        }
        return err;
    }
    case HEAPSTORE_BATCH_ITEM_SESSION:
        return heapstore_registry_add_session(&item->data.session);
    case HEAPSTORE_BATCH_ITEM_AGENT:
        return heapstore_registry_add_agent(&item->data.agent);
    case HEAPSTORE_BATCH_ITEM_SKILL:
        return heapstore_registry_add_skill(&item->data.skill);
    case HEAPSTORE_BATCH_ITEM_MEMORY_POOL:
        return heapstore_memory_record_pool(&item->data.memory_pool);
    case HEAPSTORE_BATCH_ITEM_MEMORY_ALLOC:
        return heapstore_mem_record(&item->data.memory_alloc);
    case HEAPSTORE_BATCH_ITEM_IPC_CHANNEL:
        return heapstore_ipc_record_channel(&item->data.ipc_channel);
    case HEAPSTORE_BATCH_ITEM_IPC_BUFFER:
        return heapstore_ipc_record_buffer(&item->data.ipc_buffer);
    default:
        return heapstore_ERR_INVALID_PARAM;
    }
}

heapstore_error_t heapstore_batch_commit(heapstore_batch_context_t *ctx)
{
    if (!ctx) {
        return heapstore_ERR_INVALID_PARAM;
    }

    heapstore_error_t result = heapstore_SUCCESS;

    /* 锁内摘除整条链表并清零 count；并发 add 的条目计数从 0 重新累计，
     * 避免 commit 遍历期间 add 的条目计数被误清零导致 count 与链表长度不一致。 */
    airy_mtx_lock(&ctx->lock);
    heapstore_batch_item_t *item = ctx->head;
    ctx->head = ctx->tail = NULL;
    ctx->count = 0;
    airy_mtx_unlock(&ctx->lock);

    while (item) {
        heapstore_batch_item_t *next = item->next;
        heapstore_error_t err = batch_commit_single_item(item);
        if (err != heapstore_SUCCESS && result == heapstore_SUCCESS) {
            result = err;
        }
        AIRY_FREE(item);
        item = next;
    }

    return result;
}
