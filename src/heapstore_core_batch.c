// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_core_batch.c
 * @brief Batch-write domain: batch context management and record enqueueing.
 */

// @owner: team-B
#include "heapstore.h"
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

#include "heapstore_core_internal.h"

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

    if (ctx->tail) {
        ctx->tail->next = item;
        ctx->tail = item;
    } else {
        ctx->head = ctx->tail = item;
    }
    ctx->count++;
    return heapstore_SUCCESS;
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

    if (ctx->tail) {
        ctx->tail->next = item;
        ctx->tail = item;
    } else {
        ctx->head = ctx->tail = item;
    }
    ctx->count++;
    return heapstore_SUCCESS;
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

    if (ctx->tail) {
        ctx->tail->next = item;
        ctx->tail = item;
    } else {
        ctx->head = ctx->tail = item;
    }
    ctx->count++;
    return heapstore_SUCCESS;
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

    if (ctx->tail) {
        ctx->tail->next = item;
        ctx->tail = item;
    } else {
        ctx->head = ctx->tail = item;
    }
    ctx->count++;
    return heapstore_SUCCESS;
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

    if (ctx->tail) {
        ctx->tail->next = item;
        ctx->tail = item;
    } else {
        ctx->head = ctx->tail = item;
    }
    ctx->count++;
    return heapstore_SUCCESS;
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

    if (ctx->tail) {
        ctx->tail->next = item;
        ctx->tail = item;
    } else {
        ctx->head = ctx->tail = item;
    }
    ctx->count++;
    return heapstore_SUCCESS;
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

    if (ctx->tail) {
        ctx->tail->next = item;
        ctx->tail = item;
    } else {
        ctx->head = ctx->tail = item;
    }
    ctx->count++;
    return heapstore_SUCCESS;
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

    if (ctx->tail) {
        ctx->tail->next = item;
        ctx->tail = item;
    } else {
        ctx->head = ctx->tail = item;
    }
    ctx->count++;
    return heapstore_SUCCESS;
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

    if (ctx->tail) {
        ctx->tail->next = item;
        ctx->tail = item;
    } else {
        ctx->head = ctx->tail = item;
    }
    ctx->count++;
    return heapstore_SUCCESS;
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

    if (ctx->tail) {
        ctx->tail->next = item;
        ctx->tail = item;
    } else {
        ctx->head = ctx->tail = item;
    }
    ctx->count++;
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_batch_add_span(heapstore_batch_context_t *ctx,
                                           const heapstore_span_t *span)
{
    if (!ctx || !span) {
        return heapstore_ERR_INVALID_PARAM;
    }
    return heapstore_batch_add_trace(ctx, span->trace_id, span->span_id, span->parent_span_id,
                                     span->name, span->start_time_ns, span->end_time_ns, 0,
                                     span->attributes);
}

void heapstore_batch_rollback(heapstore_batch_context_t *ctx)
{
    if (!ctx) {
        return;
    }

    heapstore_batch_item_t *item = ctx->head;
    while (item) {
        heapstore_batch_item_t *next = item->next;
        AIRY_FREE(item);
        item = next;
    }

    ctx->head = ctx->tail = NULL;
    ctx->count = 0;
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
