/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file heapstore_batch.h
 * @brief AgentRT heapstore batch write interface.
 */

/* @owner: team-B */
#ifndef AIRY_RT_HEAPSTORE_BATCH_H
#define AIRY_RT_HEAPSTORE_BATCH_H

#include "../../commons/platform/include/platform.h"
#include "heapstore.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Batch 上下文最大容纳条目数。 */
#define HEAPSTORE_BATCH_MAX_ITEMS 1024

/**
  * @brief Batch write item type
 */
typedef enum {
    HEAPSTORE_BATCH_ITEM_LOG = 0,
    HEAPSTORE_BATCH_ITEM_SPAN,
    HEAPSTORE_BATCH_ITEM_SESSION,
    HEAPSTORE_BATCH_ITEM_AGENT,
    HEAPSTORE_BATCH_ITEM_SKILL,
    HEAPSTORE_BATCH_ITEM_MEMORY_POOL,
    HEAPSTORE_BATCH_ITEM_MEMORY_ALLOC,
    HEAPSTORE_BATCH_ITEM_IPC_CHANNEL,
    HEAPSTORE_BATCH_ITEM_IPC_BUFFER
} heapstore_batch_item_type_t;

/**
  * @brief Batch write item node（数据载荷为匿名联合，与实现保持一致）
 */
typedef struct heapstore_batch_item {
    heapstore_batch_item_type_t type;
    union {
        struct {
            char service[128];
            int level;
            char trace_id[64];
            char message[1024];
        } log;
        struct {
            char trace_id[64];
            char span_id[64];
            char parent_span_id[64];
            char name[256];
            int64_t start_time_us;
            int64_t end_time_us;
            int status;
            char attributes[2048];
        } span;
        heapstore_session_record_t session;
        heapstore_agent_record_t agent;
        heapstore_skill_record_t skill;
        heapstore_memory_pool_t memory_pool;
        heapstore_memory_allocation_t memory_alloc;
        heapstore_ipc_channel_t ipc_channel;
        heapstore_ipc_buffer_t ipc_buffer;
    } data;
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
  * @brief Add a log entry to the batch buffer
 *
  * @param ctx [in] Batch write context
  * @param service [in] Service name
  * @param level [in] Log level
  * @param message [in] Log message
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns the lifetime of all parameters
 * @threadsafe yes
 * @reentrant yes

 * @since v0.1.0*/
heapstore_error_t heapstore_batch_add_log(heapstore_batch_context_t *ctx, const char *service,
                                          int level, const char *message);

/**
  * @brief Add a log entry with a trace ID to the batch buffer
 */
heapstore_error_t heapstore_batch_add_log_with_trace(heapstore_batch_context_t *ctx,
                                                     const char *service, int level,
                                                     const char *trace_id, const char *message);

/**
  * @brief Add a trace span to the batch buffer
 */
heapstore_error_t heapstore_batch_add_trace(heapstore_batch_context_t *ctx, const char *trace_id,
                                            const char *span_id, const char *parent_id,
                                            const char *name, int64_t start_time_us,
                                            int64_t end_time_us, int status,
                                            const char *attributes);

/**
  * @brief Add a session record to the batch buffer
 */
heapstore_error_t heapstore_batch_add_session(heapstore_batch_context_t *ctx,
                                              const heapstore_session_record_t *record);

/**
  * @brief Add an agent record to the batch buffer
 */
heapstore_error_t heapstore_batch_add_agent(heapstore_batch_context_t *ctx,
                                            const heapstore_agent_record_t *record);

/**
  * @brief Add a skill record to the batch buffer
 */
heapstore_error_t heapstore_batch_add_skill(heapstore_batch_context_t *ctx,
                                            const heapstore_skill_record_t *record);

/**
  * @brief Add a memory pool record to the batch buffer
 */
heapstore_error_t heapstore_batch_add_memory_pool(heapstore_batch_context_t *ctx,
                                                  const heapstore_memory_pool_t *pool);

/**
  * @brief Add a memory allocation record to the batch buffer
 */
heapstore_error_t heapstore_batch_add_allocation(heapstore_batch_context_t *ctx,
                                                 const heapstore_memory_allocation_t *allocation);

/**
  * @brief Add an IPC channel record to the batch buffer
 */
heapstore_error_t heapstore_batch_add_ipc_channel(heapstore_batch_context_t *ctx,
                                                  const heapstore_ipc_channel_t *channel);

/**
  * @brief Add an IPC buffer record to the batch buffer
 */
heapstore_error_t heapstore_batch_add_ipc_buffer(heapstore_batch_context_t *ctx,
                                                 const heapstore_ipc_buffer_t *buffer);

/**
  * @brief Add a span record to the batch buffer
 */
heapstore_error_t heapstore_batch_add_span(heapstore_batch_context_t *ctx,
                                           const heapstore_span_t *span);

/**
  * @brief Commit the batch write
 *
  * @param ctx [in/out] Batch write context
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant no

 * @since v0.1.0*/
heapstore_error_t heapstore_batch_commit(heapstore_batch_context_t *ctx);

/**
  * @brief Roll back the batch write
 *
  * @param ctx [in/out] Batch write context
 *
 * @threadsafe yes
 * @reentrant yes

 * @since v0.1.0*/
void heapstore_batch_rollback(heapstore_batch_context_t *ctx);

/**
  * @brief Destroy a batch write context
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

/**
  * @brief Get the batch context capacity
 *
  * @param ctx [in] Batch write context
  * @return size_t Capacity size
 */
size_t heapstore_batch_get_capacity(const heapstore_batch_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_RT_HEAPSTORE_BATCH_H */
