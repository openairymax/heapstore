/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file heapstore.h
 * @brief AgentRT data partition core interface.
 */

/* @owner: team-B */
#ifndef AIRY_heapstore_H
#define AIRY_heapstore_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
  * @brief Error code definitions
 */
typedef enum {
    heapstore_SUCCESS = 0,
    heapstore_ERR_INVALID_PARAM = -1,
    heapstore_ERR_NOT_INITIALIZED = -2,
    heapstore_ERR_ALREADY_INITIALIZED = -3,
    heapstore_ERR_DIR_CREATE_FAILED = -4,
    heapstore_ERR_DIR_NOT_FOUND = -5,
    heapstore_ERR_PERMISSION_DENIED = -6,
    heapstore_ERR_OUT_OF_MEMORY = -7,
    heapstore_ERR_DB_INIT_FAILED = -8,
    heapstore_ERR_DB_QUERY_FAILED = -9,
    heapstore_ERR_FILE_OPEN_FAILED = -10,
    heapstore_ERR_CONFIG_INVALID = -11,
    heapstore_ERR_NOT_FOUND = -12,
    heapstore_ERR_FILE_OPERATION_FAILED = -13,
    heapstore_ERR_FILE_NOT_FOUND = -14,
    heapstore_ERR_CIRCUIT_OPEN = -15,
    heapstore_ERR_TIMEOUT = -16,
    heapstore_ERR_NO_SPACE = -17,
    heapstore_ERR_NOT_SUPPORTED = -18,
    heapstore_ERR_FILE_CORRUPT = -19,
    /* 0.1.21 D-21：显式事务边界的状态机错误。 */
    heapstore_ERR_TXN_ACTIVE = -20,
    heapstore_ERR_TXN_NOT_ACTIVE = -21,
    heapstore_ERR_INTERNAL = -99
} heapstore_error_t;


#include "heapstore_types.h"


#include "heapstore_memory.h"
#include "heapstore_registry.h"
#include "heapstore_trace.h"
#include "heapstore_batch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief Data partition path type
 */
typedef enum {
    heapstore_PATH_KERNEL,
    heapstore_PATH_LOGS,
    heapstore_PATH_REGISTRY,
    heapstore_PATH_SERVICES,
    heapstore_PATH_TRACES,
    heapstore_PATH_KERNEL_IPC,
    heapstore_PATH_KERNEL_MEMORY,
    heapstore_PATH_MAX
} heapstore_path_type_t;

/**
  * @brief Circuit breaker state
 */
typedef enum {
    heapstore_CIRCUIT_CLOSED = 0,
    heapstore_CIRCUIT_OPEN,
    heapstore_CIRCUIT_HALF_OPEN
} heapstore_circuit_state_t;

/**
  * @brief Configuration item structure
 */
typedef struct heapstore_config {
    const char *root_path;
    size_t max_log_size_mb;
    int log_retention_days;
    int trace_retention_days;
    bool enable_auto_cleanup;
    bool enable_log_rotation;
    bool enable_trace_export;
    int db_vacuum_interval_days;
    uint32_t circuit_breaker_threshold;
    uint32_t circuit_breaker_timeout_sec;
} heapstore_config_t;

/**
 * @brief Build the default configuration with the given root path
 *
 * @param root_path [in] storage root directory (stored as-is, not copied)
 * @return heapstore_config_t default configuration
 *
 * @ownership caller owns the lifetime of root_path
 * @threadsafe yes
 * @reentrant yes
 *
 * @note Production defaults: 100MB log cap, 7d log / 3d trace retention,
 *       auto cleanup, log rotation and trace export enabled, 7d vacuum,
 *       circuit breaker 5 failures / 30s timeout
 * @since v0.1.19
 */
heapstore_config_t heapstore_def_config(const char *root_path);

/**
  * @brief Statistics structure
 */
typedef struct heapstore_stats {
    uint64_t total_disk_usage_bytes;
    uint64_t log_usage_bytes;
    uint64_t registry_usage_bytes;
    uint64_t trace_usage_bytes;
    uint64_t ipc_usage_bytes;
    uint64_t memory_usage_bytes;
    uint32_t log_file_count;
    uint32_t trace_file_count;
} heapstore_stats_t;

/**
  * @brief Performance metrics structure
 */
typedef struct heapstore_metrics {
    uint64_t total_operations;
    uint64_t failed_operations;
    uint64_t fast_path_operations;
    uint64_t slow_path_operations;
    uint64_t circuit_breaker_trips;
    double avg_operation_time_ns;
    uint64_t peak_concurrent_ops;
} heapstore_metrics_t;

/**
  * @brief Circuit breaker status information
 */
typedef struct heapstore_circuit_info {
    heapstore_circuit_state_t state;
    uint32_t failure_count;
    uint64_t last_failure_time;
    uint32_t threshold;
    uint32_t timeout_sec;
} heapstore_circuit_info_t;

/**
  * @brief Initialize the data partition
 *
  * @param manager [in] Configuration parameters (NULL for defaults)
  * @return heapstore_error_t Error code
 *
 * @ownership manager: BORROW
 * @threadsafe no (not safe for concurrent calls)
 * @reentrant no
 *
 * @note must be called before any other API
 *
 * @see heapstore_shutdown()
 * @since v1.0.0
 */
heapstore_error_t heapstore_init(const heapstore_config_t *manager);

/**
  * @brief Shut down the data partition and free resources
 *
 * @ownership N/A (no pointer parameters)
 * @threadsafe no (not safe for concurrent calls)
 * @reentrant no
 *
  * @note All APIs return heapstore_ERR_NOT_INITIALIZED afterwards
 *
 * @see heapstore_init()
 * @since v1.0.0
 */
void heapstore_shutdown(void);

/**
  * @brief Check whether the data partition is initialized
 *
  * @return bool true if initialized
 *
 * @threadsafe yes
 * @reentrant yes
 * @since v1.0.0
 */
bool heapstore_ready(void);

/**
  * @brief Get the data partition root path
 *
  * @return const char* Root path string
 *
 * @ownership return: BORROW (internal string, do not free)
 * @threadsafe yes
 * @reentrant yes
 *
  * @note Returns an empty string when uninitialized
 * @since v1.0.0
 */
const char *heapstore_get_root(void);

/**
  * @brief Get the path for a given type
 *
  * @param type [in] Path type
  * @return const char* Path string (without the root path prefix)
 *
 * @ownership return: BORROW (internal string, do not free)
 * @threadsafe yes
 * @reentrant yes
 *
  * @note NULL for an invalid type
 */
const char *heapstore_get_path(heapstore_path_type_t type);

/**
  * @brief Get the full path
 *
  * @param type [in] Path type
  * @param buffer [out] Output buffer
  * @param buffer_size [in] Buffer size
  * @return heapstore_error_t Error code
 *
 * @ownership buffer: BORROW (caller-owned buffer, function writes to it)
 * @threadsafe yes
 * @reentrant yes
 *
  * @note Returns heapstore_ERR_BUFFER_TOO_SMALL when the buffer is too small
 */
heapstore_error_t heapstore_get_full_path(heapstore_path_type_t type, char *buffer,
                                          size_t buffer_size);

/**
  * @brief Get statistics
 *
  * @param stats [out] Output statistics structure
  * @return heapstore_error_t Error code
 *
 * @ownership stats: BORROW (caller-owned buffer, function writes to it)
 * @threadsafe yes
 * @reentrant yes
 */
heapstore_error_t heapstore_get_stats(heapstore_stats_t *stats);

/**
  * @brief Fast path: asynchronous log write (lock-free)
 *
  * @param service [in] Service name
  * @param level [in] Log level
  * @param message [in] Log message
  * @return heapstore_error_t Error code
 *
 * @ownership message: BORROW
 * @threadsafe yes
 * @reentrant yes
 *
  * @note Fast path for high-frequency log writes
  * @note Returns heapstore_ERR_CIRCUIT_OPEN when the circuit breaker is open
 *
 * @see heapstore_log_write_slow()
 */
heapstore_error_t heapstore_log_write_fast(const char *service, int level, const char *message);

/**
  * @brief Slow path: synchronous log write (full validation)
 *
  * @param service [in] Service name
  * @param level [in] Log level
  * @param message [in] Log message
  * @param trace_id [in] Trace ID (may be NULL)
  * @param timeout_ms [in] Timeout (milliseconds)
  * @return heapstore_error_t Error code
 *
 * @ownership message: BORROW
 * @threadsafe yes
 * @reentrant no
 *
  * @note Slow path for important log writes
  * @note Includes full parameter validation and error handling
 *
 * @see heapstore_log_write_fast()
 * @since v1.0.0
 */
heapstore_error_t heapstore_log_write_slow(const char *service, int level, const char *message,
                                           const char *trace_id, uint32_t timeout_ms);

/**
  * @brief Clean up expired data
 *
  * @param dry_run [in] If true, only report what would be cleaned without deleting
  * @param freed_bytes [out] Output: bytes actually freed (may be NULL)
  * @return heapstore_error_t Error code
 *
 * @ownership freed_bytes: BORROW (caller-owned buffer, function writes to it, may be NULL)
 * @threadsafe yes
 * @reentrant no
 *
  * @note Cleanup follows log_retention_days and trace_retention_days in the config
 */
heapstore_error_t heapstore_cleanup(bool dry_run, uint64_t *freed_bytes);

/**
  * @brief Get the description string for an error code
 *
  * @param err [in] Error code
  * @return const char* Error description
 *
 * @ownership return: BORROW (internal string, do not free)
 * @threadsafe yes
 * @reentrant yes
 */
const char *heapstore_strerror(heapstore_error_t err);

/**
  * @brief Reload the configuration
 *
  * @param manager [in] New configuration
  * @return heapstore_error_t Error code
 *
 * @ownership manager: BORROW
 * @threadsafe no
 * @reentrant no
 *
  * @note Only updates the config; does not touch initialized resources
 */
heapstore_error_t heapstore_reload_config(const heapstore_config_t *manager);

/**
  * @brief Force-flush all pending data
 *
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant no
 */
heapstore_error_t heapstore_flush(void);

/**
  * @brief Begin an explicit transaction around registry writes
  *
  * Issues SQLite BEGIN IMMEDIATE on the registry database. While the
  * transaction is open, subsequent registry write calls (for example
  * heapstore_registry_add_session()) participate in it until
  * heapstore_txn_commit() or heapstore_txn_rollback() is called.
  *
  * @return heapstore_error_t Error code
  *         - heapstore_SUCCESS: transaction opened
  *         - heapstore_ERR_NOT_INITIALIZED: heapstore not initialized
  *         - heapstore_ERR_TXN_ACTIVE: a transaction is already open
  *         - heapstore_ERR_NOT_SUPPORTED: build has no SQLite backend
  *         - heapstore_ERR_DB_QUERY_FAILED: SQLite refused BEGIN
  *
  * @pre heapstore_init() succeeded
  * @post On success the caller must finish with commit or rollback
  * @threadsafe yes (serialized by the registry mutex)
  * @reentrant no
  *
  * @note Do not combine with the batch APIs (heapstore_batch_*) or with
  *       journal flush helpers that open their own SQLite transaction;
  *       SQLite does not allow nested BEGIN on the same connection.
  * @note Added in v0.1.21 (review item D-21: transaction/replication).
  * @since v0.1.21
 */
heapstore_error_t heapstore_txn_begin(void);

/**
  * @brief Commit the transaction opened by heapstore_txn_begin()
  *
  * @return heapstore_error_t Error code
  *         - heapstore_SUCCESS: transaction committed
  *         - heapstore_ERR_NOT_INITIALIZED: heapstore not initialized
  *         - heapstore_ERR_TXN_NOT_ACTIVE: no transaction is open
  *         - heapstore_ERR_NOT_SUPPORTED: build has no SQLite backend
  *         - heapstore_ERR_DB_QUERY_FAILED: SQLite refused COMMIT
  *
  * @pre heapstore_txn_begin() succeeded
  * @threadsafe yes (serialized by the registry mutex)
  * @reentrant no
  *
  * @note Added in v0.1.21 (review item D-21: transaction/replication).
  * @since v0.1.21
 */
heapstore_error_t heapstore_txn_commit(void);

/**
  * @brief Roll back the transaction opened by heapstore_txn_begin()
  *
  * @return heapstore_error_t Error code
  *         - heapstore_SUCCESS: transaction rolled back
  *         - heapstore_ERR_NOT_INITIALIZED: heapstore not initialized
  *         - heapstore_ERR_TXN_NOT_ACTIVE: no transaction is open
  *         - heapstore_ERR_NOT_SUPPORTED: build has no SQLite backend
  *         - heapstore_ERR_DB_QUERY_FAILED: SQLite refused ROLLBACK
  *
  * @pre heapstore_txn_begin() succeeded
  * @threadsafe yes (serialized by the registry mutex)
  * @reentrant no
  *
  * @note Added in v0.1.21 (review item D-21: transaction/replication).
  * @since v0.1.21
 */
heapstore_error_t heapstore_txn_rollback(void);

/**
  * @brief Back up the initialized SQLite databases into another root
  *
  * Uses the SQLite online backup API (sqlite3_backup_*) to copy every
  * initialized SQLite database under the current heapstore root into
  * dst_root_path, keeping the same relative layout (currently
  * registry/registry.db; the trace store is file-backed, not SQLite).
  * Missing destination directories are created. The destination is a
  * usable heapstore root after the call: heapstore_init() can be called
  * on dst_root_path directly.
  *
  * @param dst_root_path [in] Destination heapstore root (NUL-terminated)
  * @return heapstore_error_t Error code
  *         - heapstore_SUCCESS: backup completed
  *         - heapstore_ERR_INVALID_PARAM: NULL/empty path or path too long
  *         - heapstore_ERR_NOT_INITIALIZED: heapstore not initialized
  *         - heapstore_ERR_DIR_CREATE_FAILED: destination directory failed
  *         - heapstore_ERR_DB_INIT_FAILED: destination DB could not be opened
  *         - heapstore_ERR_DB_QUERY_FAILED: SQLite backup failed (partial
  *           destination file is removed)
  *         - heapstore_ERR_NOT_SUPPORTED: build has no SQLite backend
  *
  * @ownership dst_root_path: BORROW
  * @pre heapstore_init() succeeded
  * @threadsafe yes (serialized by the registry mutex)
  * @reentrant no
  *
  * @note The backup is taken from the live registry connection, so it is
  *       consistent even with WAL content; concurrent writers are blocked
  *       for the duration of the copy.
  * @note Added in v0.1.21 (review item D-21: transaction/replication).
  * @since v0.1.21
 */
heapstore_error_t heapstore_backup(const char *dst_root_path);

/**
  * @brief Health check for all subsystems
 *
  * @param registry_ok [out] Whether the registry subsystem is healthy (may be NULL)
  * @param trace_ok [out] Whether the trace subsystem is healthy (may be NULL)
  * @param log_ok [out] Whether the log subsystem is healthy (may be NULL)
  * @param ipc_ok [out] Compatibility slot, always true (IPC subsystem retired; may be NULL)
  * @param memory_ok [out] Whether the memory subsystem is healthy (may be NULL)
  * @return heapstore_error_t Error code; heapstore_SUCCESS means overall health
 *
  * @ownership All output params: BORROW (caller-owned buffers, function writes to them)
 * @threadsafe yes
 * @reentrant yes
 *
  * @note All output params are optional; NULL skips that subsystem
 */
heapstore_error_t heapstore_health_check(bool *registry_ok, bool *trace_ok, bool *log_ok,
                                         bool *ipc_ok, bool *memory_ok);

/**
  * @brief Get performance metrics
 *
  * @param metrics [out] Output metrics structure
  * @return heapstore_error_t Error code
 *
 * @ownership metrics: BORROW (caller-owned buffer, function writes to it)
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
heapstore_error_t heapstore_get_metrics(heapstore_metrics_t *metrics);

/**
  * @brief Reset performance metrics
 *
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_reset_metrics(void);

/**
  * @brief Get the circuit breaker status
 *
  * @param info [out] Output circuit breaker status
  * @return heapstore_error_t Error code
 *
 * @ownership info: BORROW (caller-owned buffer, function writes to it)
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
heapstore_error_t heapstore_get_circuit_state(heapstore_circuit_info_t *info);

/**
  * @brief Manually reset the circuit breaker
 *
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant no
 *
  * @note Usually called manually after a problem is fixed
 */
heapstore_error_t heapstore_reset_circuit(void);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_heapstore_H */
