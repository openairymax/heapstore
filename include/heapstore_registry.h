/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file heapstore_registry.h
 * @brief AgentRT data partition registry interface.
 */

/* @owner: team-B */
#ifndef AIRY_heapstore_REGISTRY_H
#define AIRY_heapstore_REGISTRY_H

#include "heapstore.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
  * @brief Registry type
 */
typedef enum {
    heapstore_REG_AGENTS,
    heapstore_REG_SKILLS,
    heapstore_REG_SESSIONS,
    heapstore_REG_MAX
} heapstore_registry_type_t;

/**
  * @brief Registry iterator
 */
typedef struct heapstore_registry_iter heapstore_registry_iter_t;

/**
  * @brief Initialize the registry subsystem
 *
  * @return heapstore_error_t Error code
 *
  * @ownership Manages all resources internally
 * @threadsafe no (not safe for concurrent calls)
 * @reentrant no
 *
 * @see heapstore_registry_shutdown()
 * @since v1.0.0
 */
heapstore_error_t heapstore_registry_init(void);

/**
  * @brief Shut down the registry subsystem
 *
  * @ownership Releases all resources internally
 * @threadsafe no
 * @reentrant no
 *
 * @see heapstore_registry_init()
 * @since v1.0.0
 */
void heapstore_registry_shutdown(void);

/**
  * @brief Add an agent record
 *
  * @param record [in] Agent record
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns the lifetime of record
 * @threadsafe yes
 * @reentrant no
 */
heapstore_error_t heapstore_registry_add_agent(const heapstore_agent_record_t *record);

/**
  * @brief Get an agent record
 *
 * @param id [in] Agent ID
  * @param record [out] Output record
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns allocation and freeing of record
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
heapstore_error_t heapstore_registry_get_agent(const char *id, heapstore_agent_record_t *record);

/**
  * @brief Update an agent record
 *
  * @param record [in] Agent record
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns the lifetime of record
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_registry_update_agent(const heapstore_agent_record_t *record);

/**
  * @brief Delete an agent record
 *
 * @param id [in] Agent ID
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_registry_delete_agent(const char *id);

/**
  * @brief Query agent records
 *
  * @param filter_type [in] Filter by type (NULL for no filter)
  * @param filter_status [in] Filter by status (NULL for no filter)
  * @param iter [out] Output iterator
  * @return heapstore_error_t Error code
 *
  * @ownership caller must release the Iterator with heapstore_registry_iter_destroy()
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_registry_query_agents(const char *filter_type,
                                                  const char *filter_status,
                                                  heapstore_registry_iter_t **iter);

/**
  * @brief Add a skill record
 *
  * @param record [in] Skill record
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns the lifetime of record
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_registry_add_skill(const heapstore_skill_record_t *record);

/**
  * @brief Get a skill record
 *
  * @param id [in] Skill ID
  * @param record [out] Output record
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns allocation and freeing of record
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
heapstore_error_t heapstore_registry_get_skill(const char *id, heapstore_skill_record_t *record);

/**
  * @brief Delete a skill record
 *
  * @param id [in] Skill ID
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_registry_delete_skill(const char *id);

/**
  * @brief Query skill records
 *
  * @param iter [out] Output iterator
  * @return heapstore_error_t Error code
 *
  * @ownership caller must release the Iterator with heapstore_registry_iter_destroy()
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_registry_query_skills(heapstore_registry_iter_t **iter);

/**
  * @brief Add a session record
 *
  * @param record [in] Session record
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns the lifetime of record
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_registry_add_session(const heapstore_session_record_t *record);

/**
  * @brief Get a session record
 *
  * @param id [in] Session ID
  * @param record [out] Output record
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns allocation and freeing of record
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
heapstore_error_t heapstore_registry_get_session(const char *id,
                                                 heapstore_session_record_t *record);

/**
  * @brief Update a session record
 *
  * @param record [in] Session record
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns the lifetime of record
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_registry_update_session(const heapstore_session_record_t *record);

/**
  * @brief Delete a session record
 *
  * @param id [in] Session ID
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_registry_delete_session(const char *id);

/**
  * @brief Query session records
 *
  * @param filter_status [in] Filter by status (NULL for no filter)
  * @param iter [out] Output iterator
  * @return heapstore_error_t Error code
 *
  * @ownership caller must release the Iterator with heapstore_registry_iter_destroy()
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_registry_query_sessions(const char *filter_status,
                                                    heapstore_registry_iter_t **iter);

/**
  * @brief Iterate to the next record
 *
  * @param iter [in] Iterator
  * @param record [out] Output record
  * @return heapstore_error_t Error code，heapstore_ERR_NOT_FOUND marks the end of iteration
 *
  * @ownership Caller owns allocation and freeing of record
 * @threadsafe no
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_registry_iter_next(heapstore_registry_iter_t *iter, void *record);

/**
  * @brief Destroy an iterator
 *
  * @param iter [in] Iterator
 *
  * @ownership Caller must pass a valid iterator
 * @threadsafe no
 * @reentrant no

 * @since v1.0.0*/
void heapstore_registry_iter_destroy(heapstore_registry_iter_t *iter);

/**
  * @brief Run a database VACUUM
 *
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant no
 * @since v1.0.0
 */
heapstore_error_t heapstore_registry_vacuum(void);

/**
  * @brief Batch-insert agent records (transaction-optimized)
 *
  * Wraps multiple INSERTs in one transaction for much better batch throughput.
  * 5-10x faster than calling heapstore_registry_add_agent() per record.
 *
  * @param records [in] Agent record array
  * @param count [in] Record count
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns the lifetime of the records array
 * @threadsafe yes
 * @reentrant no
 *
 * @note
  * - All records insert or all roll back (atomic)
  * - Any failed record rolls back the whole transaction
  * - Suitable for bulk import
 *
 * @see heapstore_registry_add_agent()
 * @since v0.1.0
 *
 * @example
 * @code
 * heapstore_agent_record_t records[100];
 *
 * heapstore_error_t err = heapstore_registry_batch_insert_agents(records, 100);
 * if (err == heapstore_SUCCESS) {
 *
 * }
 * @endcode
 */
heapstore_error_t heapstore_registry_batch_insert_agents(const heapstore_agent_record_t *records,
                                                         size_t count);

/**
  * @brief Batch-insert session records (transaction-optimized)
 *
  * @param records [in] Session record array
  * @param count [in] Record count
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns the lifetime of the records array
 * @threadsafe yes
 * @reentrant no
 *
  * @note All records insert or all roll back
 * @see heapstore_registry_add_session()
 * @since v0.1.0
 */
heapstore_error_t heapstore_registry_batch_insert_sessions(
    const heapstore_session_record_t *records, size_t count);

/**
  * @brief Batch-insert skill records (transaction-optimized)
 *
  * @param records [in] Skill record array
  * @param count [in] Record count
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns the lifetime of the records array
 * @threadsafe yes
 * @reentrant no
 *
  * @note All records insert or all roll back
 * @see heapstore_registry_add_skill()
 * @since v0.1.0
 */
heapstore_error_t heapstore_registry_batch_insert_skills(const heapstore_skill_record_t *records,
                                                         size_t count);

/**
  * @brief Check whether the registry subsystem is healthy
 *
  * @return bool true if healthy
 *
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
bool heapstore_registry_is_healthy(void);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_heapstore_REGISTRY_H */
