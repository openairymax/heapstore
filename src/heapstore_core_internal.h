// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_core_internal.h
 * @brief Data-partition core internal shared definitions: batch context private types.
 */

#ifndef AIRY_HEAPSTORE_CORE_INTERNAL_H
#define AIRY_HEAPSTORE_CORE_INTERNAL_H

#include "heapstore.h"
#include "heapstore_ipc.h"
#include "heapstore_memory.h"
#include "heapstore_registry.h"

#include "platform.h"

#define HEAPSTORE_BATCH_MAX_ITEMS 1024

typedef enum {
    HEAPSTORE_BATCH_ITEM_LOG,
    HEAPSTORE_BATCH_ITEM_SPAN,
    HEAPSTORE_BATCH_ITEM_SESSION,
    HEAPSTORE_BATCH_ITEM_AGENT,
    HEAPSTORE_BATCH_ITEM_SKILL,
    HEAPSTORE_BATCH_ITEM_MEMORY_POOL,
    HEAPSTORE_BATCH_ITEM_MEMORY_ALLOC,
    HEAPSTORE_BATCH_ITEM_IPC_CHANNEL,
    HEAPSTORE_BATCH_ITEM_IPC_BUFFER
} heapstore_batch_item_type_t;

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

struct heapstore_batch_context {
    size_t capacity;
    size_t count;
    heapstore_batch_item_t *head;
    heapstore_batch_item_t *tail;
#ifdef _WIN32
    airy_mtx_t lock;
#else
    airy_mtx_t lock;
#endif
};

#endif /* AIRY_HEAPSTORE_CORE_INTERNAL_H */
