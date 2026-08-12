/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file heapstore_integration.h
 * @brief Integration interface between heapstore and AgentRT core modules.
 *
 * @details
 * Defines how the heapstore data-partition storage system integrates with
 * AgentRT core modules, following the architecture principles:
 * - S-2 layered decomposition: heapstore is the underlying storage engine
 * - K-2 contract interfaces: every interface has a full contract
 * - E-2 observability: integrated observability data collection
 *
 * Integration architecture:
 * ```
 * syscall/ -------> heapstore (registry, trace storage)
 * memory/ --------> heapstore (memory data persistence)
 * corekern/ipc/ --> heapstore (IPC data storage)
 * commons/logging/ -> heapstore (log storage)
 * ```
 */

/* @owner: team-B */
#ifndef AIRY_heapstore_INTEGRATION_H
#define AIRY_heapstore_INTEGRATION_H

#include "airy_rt.h"
#include "heapstore.h"
#include "heapstore_ipc.h"
#include "heapstore_log.h"
#include "heapstore_memory.h"
#include "heapstore_registry.h"
#include "heapstore_trace.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
  * @brief Initialize heapstore and integrate with the AgentRT core
 *
  * @param root_path [in] Data partition root path; NULL for the default
  * @return airy_err_t Error code
 *
  * @ownership Manages all resources internally
  * @threadsafe no; call at program startup
 * @reentrant no
 *
  * @note Call after airy_init()
  * @note Automatically initializes all subsystems (log, registry, trace, IPC, memory)
 *
 * @see heapstore_integration_shutdown()
 */
AIRY_API airy_err_t heapstore_integration_init(const char *root_path);

/**
  * @brief Shut down the heapstore integration and free resources
 *
  * @ownership Releases all resources internally
  * @threadsafe no; call before program exit
 * @reentrant no
 *
  * @note Call before airy_shutdown()
 *
 * @see heapstore_integration_init()
 */
AIRY_API void heapstore_integration_shutdown(void);


/**
  * @brief Provide session persistence for the syscall layer
 *
  * @param session_id [in] Session ID
  * @param metadata [in] Session metadata (JSON format)
  * @param created_ns [in] Creation time (nanoseconds)
  * @param last_active_ns [in] Last active time (nanoseconds)
  * @return airy_err_t Error code
 *
  * @ownership Caller owns the lifetime of all parameters
 * @threadsafe yes
 * @reentrant yes
 */
AIRY_API airy_err_t heapstore_syscall_session_save(const char *session_id, const char *metadata,
                                                   uint64_t created_ns, uint64_t last_active_ns);

/**
  * @brief Provide session loading for the syscall layer
 *
  * @param session_id [in] Session ID
  * @param out_metadata [out] Output metadata (caller frees)
  * @param out_created_ns [out] Output creation time
  * @param out_last_active_ns [out] Output last active time
  * @return airy_err_t Error code
 *
  * @ownership Caller frees out_metadata
 * @threadsafe yes
 * @reentrant yes
 */
AIRY_API airy_err_t heapstore_syscall_session_load(const char *session_id, char **out_metadata,
                                                   uint64_t *out_created_ns,
                                                   uint64_t *out_last_active_ns);

/**
  * @brief Provide session deletion for the syscall layer
 *
  * @param session_id [in] Session ID
  * @return airy_err_t Error code
 *
 * @threadsafe yes
 * @reentrant yes
 */
AIRY_API airy_err_t heapstore_syscall_session_delete(const char *session_id);

/**
  * @brief Provide session listing for the syscall layer
 *
  * @param out_sessions [out] Output session ID array (caller frees)
  * @param out_count [out] Output session count
  * @return airy_err_t Error code
 *
  * @ownership Caller frees out_sessions and its elements
 * @threadsafe yes
 * @reentrant yes
 */
AIRY_API airy_err_t heapstore_syscall_session_list(char ***out_sessions, size_t *out_count);


/**
  * @brief Provide trace data storage for the telemetry layer
 *
  * @param trace_id [in] Trace ID
 * @param span_id [in] Span ID
  * @param parent_id [in] Parent span ID (may be NULL)
  * @param name [in] Span name
  * @param start_time_us [in] Start time (microseconds)
  * @param end_time_us [in] End time (microseconds)
  * @param status [in] Status (0=running, 1=done, 2=error)
  * @param events_json [in] Event JSON array (may be NULL)
  * @return airy_err_t Error code
 *
  * @ownership Caller owns the lifetime of all parameters
 * @threadsafe yes
 * @reentrant yes
 */
AIRY_API airy_err_t heapstore_syscall_trace_save(const char *trace_id, const char *span_id,
                                                 const char *parent_id, const char *name,
                                                 int64_t start_time_us, int64_t end_time_us,
                                                 int status, const char *events_json);

/**
  * @brief Provide trace data export for the telemetry layer
 *
  * @param out_traces [out] Output trace JSON array (caller frees)
  * @return airy_err_t Error code
 *
  * @ownership Caller frees out_traces
 * @threadsafe yes
 * @reentrant yes
 */
AIRY_API airy_err_t heapstore_syscall_trace_export(char **out_traces);


/**
  * @brief Provide raw memory storage for the built-in memory layer
 *
  * @param data [in] Raw data
  * @param len [in] Data length
  * @param metadata [in] Metadata (JSON format; may be NULL)
  * @param out_record_id [out] Output record ID (caller frees)
  * @return airy_err_t Error code
 *
  * @ownership Caller frees out_record_id
 * @threadsafe yes
 * @reentrant yes
 */
AIRY_API airy_err_t heapstore_memory_raw_save(const void *data, size_t len, const char *metadata,
                                              char **out_record_id);

/**
  * @brief Provide raw memory loading for the built-in memory layer
 *
  * @param record_id [in] Record ID
  * @param out_data [out] Output data (caller frees)
  * @param out_len [out] Output data length
  * @param out_metadata [out] Output metadata (caller frees; may be NULL)
  * @return airy_err_t Error code
 *
  * @ownership Caller frees out_data and out_metadata
 * @threadsafe yes
 * @reentrant yes
 */
AIRY_API airy_err_t heapstore_memory_raw_load(const char *record_id, void **out_data,
                                              size_t *out_len, char **out_metadata);

/**
  * @brief Provide raw memory deletion for the built-in memory layer
 *
  * @param record_id [in] Record ID
  * @return airy_err_t Error code
 *
 * @threadsafe yes
 * @reentrant yes
 */
AIRY_API airy_err_t heapstore_memory_raw_delete(const char *record_id);


/**
  * @brief Provide channel state storage for the corekern IPC layer
 *
  * @param channel_id [in] Channel ID
  * @param state_json [in] Channel state JSON
  * @return airy_err_t Error code
 *
  * @ownership Caller owns the lifetime of all parameters
 * @threadsafe yes
 * @reentrant yes
 */
AIRY_API airy_err_t heapstore_ipc_channel_save(const char *channel_id, const char *state_json);

/**
  * @brief Provide channel state loading for the corekern IPC layer
 *
  * @param channel_id [in] Channel ID
  * @param out_state [out] Output channel state JSON (caller frees)
  * @return airy_err_t Error code
 *
  * @ownership Caller frees out_state
 * @threadsafe yes
 * @reentrant yes
 */
AIRY_API airy_err_t heapstore_ipc_channel_load(const char *channel_id, char **out_state);


/**
  * @brief Provide log storage for the commons logging layer
 *
  * @param module [in] Module name
  * @param level [in] Log level
  * @param trace_id [in] Trace ID (may be NULL)
  * @param message [in] Log message
  * @param timestamp_ns [in] Timestamp (nanoseconds)
  * @return airy_err_t Error code
 *
  * @ownership Caller owns the lifetime of all parameters
 * @threadsafe yes
 * @reentrant yes
 *
  * @note Supports both the fast and slow paths
 */
AIRY_API airy_err_t heapstore_logging_write(const char *module, int level, const char *trace_id,
                                            const char *message, uint64_t timestamp_ns);


/**
  * @brief Get the heapstore integration health status
 *
  * @param out_health_json [out] Output health JSON (caller frees)
  * @return airy_err_t Error code
 *
  * @ownership Caller frees out_health_json
 * @threadsafe yes
 * @reentrant yes
 */
AIRY_API airy_err_t heapstore_integration_health_check(char **out_health_json);

/**
  * @brief Get heapstore integration statistics
 *
  * @param out_stats_json [out] Output statistics JSON (caller frees)
  * @return airy_err_t Error code
 *
  * @ownership Caller frees out_stats_json
 * @threadsafe yes
 * @reentrant yes
 */
AIRY_API airy_err_t heapstore_integration_get_stats(char **out_stats_json);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_heapstore_INTEGRATION_H */
