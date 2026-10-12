// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_core_batch.c
 * @brief Batch-write domain: session batch context and registry commit.
 */

// @owner: team-B
#include "heapstore_core_internal.h"
#include "heapstore_batch.h"

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

/** 分配零初始化条目；容量满或分配失败均返回 NULL。 */
static heapstore_batch_item_t *batch_item_new(heapstore_batch_context_t *ctx)
{
    if (!ctx || ctx->count >= ctx->capacity) {
        return NULL;
    }

    heapstore_batch_item_t *item =
        (heapstore_batch_item_t *)AIRY_MALLOC(sizeof(heapstore_batch_item_t));
    if (!item) {
        return NULL;
    }
    __builtin_memset(item, 0, sizeof(heapstore_batch_item_t));
    return item;
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
    airy_mtx_init(&ctx->lock);
    return ctx;
}

heapstore_error_t heapstore_batch_add_session(heapstore_batch_context_t *ctx,
                                              const heapstore_session_record_t *record)
{
    if (!ctx || !record) {
        return heapstore_ERR_INVALID_PARAM;
    }

    heapstore_batch_item_t *item = batch_item_new(ctx);
    if (!item) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    item->session = *record;

    return batch_append_item(ctx, item);
}

/** 丢弃未提交条目（context_destroy 复用同一清空路径）。 */
static void batch_discard(heapstore_batch_context_t *ctx)
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

    batch_discard(ctx);
    airy_mtx_destroy(&ctx->lock);
    AIRY_FREE(ctx);
}

size_t heapstore_batch_get_count(const heapstore_batch_context_t *ctx)
{
    if (!ctx) {
        return 0;
    }
    return ctx->count;
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
        heapstore_error_t err = heapstore_registry_add_session(&item->session);
        if (err != heapstore_SUCCESS && result == heapstore_SUCCESS) {
            result = err;
        }
        AIRY_FREE(item);
        item = next;
    }

    return result;
}
