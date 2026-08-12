/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file heapstore_token.h
 * @brief AgentRT heapstore token accounting interface.
 *
 * @note Implements token usage statistics and monitoring per the E-2
 *       observability principle in ARCHITECTURAL_PRINCIPLES.md.
 */

/* @owner: team-B */
#ifndef AIRY_RT_HEAPSTORE_TOKEN_H
#define AIRY_RT_HEAPSTORE_TOKEN_H

#include "heapstore.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief Token statistics data type
 */
typedef enum {
    HEAPSTORE_TOKEN_TYPE_PROMPT = 0,
    HEAPSTORE_TOKEN_TYPE_COMPLETION = 1,
    HEAPSTORE_TOKEN_TYPE_SYSTEM = 2,
    HEAPSTORE_TOKEN_TYPE_USER = 3,
    HEAPSTORE_TOKEN_TYPE_CACHE_HIT = 4,
    HEAPSTORE_TOKEN_TYPE_TOTAL = 5
} heapstore_token_type_t;

/**
  * @brief Token operation type
 */
typedef enum {
    HEAPSTORE_TOKEN_OP_WRITE = 0,
    HEAPSTORE_TOKEN_OP_READ = 1,
    HEAPSTORE_TOKEN_OP_BATCH = 2
} heapstore_token_operation_t;

/**
  * @brief Token statistics data structure
 */
typedef struct {
    uint64_t total_prompt_tokens;
    uint64_t total_completion_tokens;
    uint64_t total_system_tokens;
    uint64_t total_user_tokens;
    uint64_t tokens_saved_by_cache;
    uint64_t total_write_operations;
    uint64_t total_read_operations;
    uint64_t total_batch_operations;
    uint64_t last_operation_time;
    double average_tokens_per_operation;
} heapstore_token_stats_t;

/**
  * @brief Token budget configuration
 */
typedef struct {
    uint64_t max_tokens_per_task;
    uint64_t warning_threshold_percent;
    uint64_t critical_threshold_percent;
    bool enable_budget_enforcement;
} heapstore_token_budget_t;

/**
  * @brief Initialize the token counter
 *
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant yes
 *
 * @see heapstore_token_shutdown()
 * @since v0.1.0
 */
heapstore_error_t heapstore_token_init(void);

/**
  * @brief Shut down the token counter
 *
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant yes
 *
 * @see heapstore_token_init()
 * @since v0.1.0
 */
heapstore_error_t heapstore_token_shutdown(void);

/**
  * @brief Record token usage
 *
  * @param type [in] Token type
  * @param count [in] Token count
  * @param operation [in] Operation type
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant yes
 *
 * @see heapstore_token_get_stats()
 * @since v0.1.0
 */
heapstore_error_t heapstore_token_record(heapstore_token_type_t type, uint64_t count,
                                         heapstore_token_operation_t operation);

/**
  * @brief Get token statistics
 *
  * @param out_stats [out] Output statistics
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant yes
 *
 * @see heapstore_token_record()
 * @since v0.1.0
 */
heapstore_error_t heapstore_token_get_stats(heapstore_token_stats_t *out_stats);

/**
  * @brief Reset token statistics
 *
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant yes
 */
heapstore_error_t heapstore_token_reset_stats(void);

/**
  * @brief Set a task token budget
 *
  * @param task_id [in] Task ID
  * @param budget [in] Budget configuration
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant yes
 *
 * @see heapstore_token_check_budget()
 * @since v0.1.0
 */
heapstore_error_t heapstore_token_set_budget(const char *task_id,
                                             const heapstore_token_budget_t *budget);

/**
  * @brief Check a task's token budget
 *
  * @param task_id [in] Task ID
  * @param requested_tokens [in] Requested token count
  * @param allowed [out] Whether allowed
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant yes
 *
 * @see heapstore_token_set_budget()
 * @since v0.1.0
 */
heapstore_error_t heapstore_token_check_budget(const char *task_id, uint64_t requested_tokens,
                                               bool *allowed);

/**
  * @brief Get a task's used token count
 *
  * @param task_id [in] Task ID
  * @param out_used [out] Used token count
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant yes
 */
heapstore_error_t heapstore_token_get_task_usage(const char *task_id, uint64_t *out_used);

/**
  * @brief Convert a token type to a string
 *
  * @param type [in] Token type
  * @return const char* Type string
 *
 * @threadsafe yes
 * @reentrant yes
 */
const char *heapstore_token_type_to_string(heapstore_token_type_t type);

/**
  * @brief Convert a token operation to a string
 *
  * @param operation [in] Operation type
  * @return const char* Operation string
 *
 * @threadsafe yes
 * @reentrant yes
 */
const char *heapstore_token_op_to_string(heapstore_token_operation_t operation);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_RT_HEAPSTORE_TOKEN_H */
