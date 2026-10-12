/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file heapstore_batch.h
 * @brief AgentRT heapstore session batch write interface.
 */

/* @owner: team-B */
#ifndef AIRY_RT_HEAPSTORE_BATCH_H
#define AIRY_RT_HEAPSTORE_BATCH_H

#include "../../commons/platform/include/platform.h"
#include "heapstore.h"

#include "airy_abi.h"

AIRY_ABI_BEGIN

/** Batch 上下文最大容纳条目数。 */
#define HEAPSTORE_BATCH_MAX_ITEMS 1024

/**
  * @brief Batch write item node（载荷为会话记录）
 */
typedef struct heapstore_batch_item {
    heapstore_session_record_t session;
    struct heapstore_batch_item *next;
} heapstore_batch_item_t;

/**
  * @brief Batch write context
 */
typedef struct heapstore_batch_context {
    size_t capacity;
    size_t count;
    heapstore_batch_item_t *head;
    heapstore_batch_item_t *tail;
    airy_mtx_t lock;
} heapstore_batch_context_t;

/**
  * @brief Create a batch write context
  *
  * @param batch_size [in] Batch size (default 100)
  * @return heapstore_batch_context_t* Batch write context pointer
  *
  * @ownership Caller owns the returned context
 * @threadsafe yes
 * @reentrant yes
 */
heapstore_batch_context_t *heapstore_batch_begin(size_t batch_size);

/**
  * @brief Add a session record to the batch buffer
  *
  * @param ctx [in] Batch write context
  * @param record [in] Session record to enqueue
  * @return heapstore_error_t Error code
  *
  * @ownership Caller owns the lifetime of all parameters
 * @threadsafe yes
 * @reentrant yes
 */
heapstore_error_t heapstore_batch_add_session(heapstore_batch_context_t *ctx,
                                              const heapstore_session_record_t *record);

/**
  * @brief Commit the batch write（逐条写入 session registry）
  *
  * @param ctx [in/out] Batch write context
  * @return heapstore_error_t Error code
  *
 * @threadsafe yes
 * @reentrant no

 * @since v0.1.0*/
heapstore_error_t heapstore_batch_commit(heapstore_batch_context_t *ctx);

/**
  * @brief Destroy a batch write context（丢弃未提交条目）
  *
  * @param ctx [in] Batch write context
  *
 * @threadsafe yes
 * @reentrant yes

 * @since v0.1.0*/
void heapstore_batch_context_destroy(heapstore_batch_context_t *ctx);

/**
  * @brief Get the current batch item count
  *
  * @param ctx [in] Batch write context
  * @return size_t Item count
 */
size_t heapstore_batch_get_count(const heapstore_batch_context_t *ctx);

AIRY_ABI_END

#endif /* AIRY_RT_HEAPSTORE_BATCH_H */
