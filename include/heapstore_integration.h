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

#ifdef __cplusplus
}
#endif

#endif /* AIRY_heapstore_INTEGRATION_H */
