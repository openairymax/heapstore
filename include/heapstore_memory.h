/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file heapstore_memory.h
 * @brief AgentRT data partition memory storage interface.
 */

/* @owner: team-B */
#ifndef AIRY_heapstore_MEMORY_H
#define AIRY_heapstore_MEMORY_H

#include "heapstore.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
  * @brief Initialize the memory data store
 *
  * @return heapstore_error_t Error code
 *
  * @ownership Manages all resources internally
 * @threadsafe no (not safe for concurrent calls)
 * @reentrant no
 *
 * @see heapstore_memory_shutdown()
 * @since v1.0.0
 */
heapstore_error_t heapstore_memory_init(void);

/**
  * @brief Shut down the memory data store
 *
  * @ownership Releases all resources internally
 * @threadsafe no
 * @reentrant no
 *
 * @see heapstore_memory_init()
 * @since v1.0.0
 */
void heapstore_memory_shutdown(void);

/**
  * @brief Record memory pool information
 *
  * @param pool [in] Memory pool information
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns the lifetime of pool
 * @threadsafe yes
 * @reentrant no
 */
heapstore_error_t heapstore_memory_record_pool(const heapstore_memory_pool_t *pool);

/**
  * @brief Get memory pool information
 *
  * @param pool_id [in] Memory pool ID
  * @param pool [out] Output memory pool information
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns allocation and freeing of pool
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
heapstore_error_t heapstore_memory_get_pool(const char *pool_id, heapstore_memory_pool_t *pool);

/**
  * @brief Update memory pool usage
 *
  * @param pool_id [in] Memory pool ID
  * @param used_size [in] Current used size
  * @param free_block_count [in] Free block count
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_memory_update_pool_usage(const char *pool_id, size_t used_size,
                                                     uint32_t free_block_count);

/**
  * @brief Record a memory allocation
 *
  * @param allocation [in] Allocation record
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns the lifetime of allocation
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_mem_record(
    const heapstore_memory_allocation_t *allocation);

/**
  * @brief Get a memory allocation record
 *
  * @param allocation_id [in] Allocation ID
  * @param allocation [out] Output allocation record
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns allocation and freeing of allocation
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
heapstore_error_t heapstore_memory_get_allocation(const char *allocation_id,
                                                  heapstore_memory_allocation_t *allocation);

/**
  * @brief Update allocation state (free)
 *
  * @param allocation_id [in] Allocation ID
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_memory_free_allocation(const char *allocation_id);

/**
  * @brief Get memory store statistics
 *
  * @param pool_count [out] Output memory pool count
  * @param total_allocations [out] Output total allocation count
  * @param total_size [out] Output total size
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns allocation and freeing of all output params
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
heapstore_error_t heapstore_memory_get_stats(uint32_t *pool_count, uint32_t *total_allocations,
                                             uint64_t *total_size);

/**
  * @brief Check whether the memory subsystem is healthy
 *
  * @return bool true if healthy
 *
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
bool heapstore_memory_is_healthy(void);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_heapstore_MEMORY_H */
