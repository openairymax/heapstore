/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file heapstore_ipc.h
 * @brief AgentRT data partition IPC storage interface.
 */

/* @owner: team-B */
#ifndef AIRY_heapstore_IPC_H
#define AIRY_heapstore_IPC_H

#include "heapstore.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
  * @brief Initialize the IPC data store
 *
  * @return heapstore_error_t Error code
 *
  * @ownership Manages all resources internally
 * @threadsafe no (not safe for concurrent calls)
 * @reentrant no
 *
 * @see heapstore_ipc_shutdown()
 * @since v1.0.0
 */
heapstore_error_t heapstore_ipc_init(void);

/**
  * @brief Shut down the IPC data store
 *
  * @ownership Releases all resources internally
 * @threadsafe no
 * @reentrant no
 *
 * @see heapstore_ipc_init()
 * @since v1.0.0
 */
void heapstore_ipc_shutdown(void);

/**
  * @brief Record channel information
 *
  * @param channel [in] Channel information
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns the lifetime of channel
 * @threadsafe yes
 * @reentrant no
 */
heapstore_error_t heapstore_ipc_record_channel(const heapstore_ipc_channel_t *channel);

/**
  * @brief Get channel information
 *
  * @param channel_id [in] Channel ID
  * @param channel [out] Output channel information
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns allocation and freeing of channel
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
heapstore_error_t heapstore_ipc_get_channel(const char *channel_id,
                                            heapstore_ipc_channel_t *channel);

/**
  * @brief Update channel activity
 *
  * @param channel_id [in] Channel ID
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_ipc_update_channel_activity(const char *channel_id);

/**
  * @brief Record buffer information
 *
  * @param buffer [in] Buffer information
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns the lifetime of buffer
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_ipc_record_buffer(const heapstore_ipc_buffer_t *buffer);

/**
  * @brief Get buffer information
 *
  * @param buffer_id [in] Buffer ID
  * @param buffer [out] Output buffer information
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns allocation and freeing of buffer
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
heapstore_error_t heapstore_ipc_get_buffer(const char *buffer_id, heapstore_ipc_buffer_t *buffer);

/**
  * @brief Get IPC store statistics
 *
  * @param channel_count [out] Output channel count
  * @param buffer_count [out] Output buffer count
  * @param total_size [out] Output total size
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns allocation and freeing of all output params
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
heapstore_error_t heapstore_ipc_get_stats(uint32_t *channel_count, uint32_t *buffer_count,
                                          uint64_t *total_size);

/**
  * @brief Check whether the IPC subsystem is healthy
 *
  * @return bool true if healthy
 *
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
bool heapstore_ipc_is_healthy(void);

heapstore_error_t heapstore_ipc_send(const char *channel_id, const void *data, size_t len);
heapstore_error_t heapstore_ipc_receive(const char *channel_id, void **data, size_t *len);
heapstore_error_t heapstore_ipc_create_channel(const char *channel_id, const char *name,
                                               const char *type, size_t buffer_size);
heapstore_error_t heapstore_ipc_destroy_channel(const char *channel_id);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_heapstore_IPC_H */
