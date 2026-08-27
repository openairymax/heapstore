// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_core_write.c
 * @brief heapstore read/write paths: fast/slow log writes, retention
 *        cleanup, path queries and disk usage statistics
 *        (functional domain after heapstore_core.c split).
 */

// @owner: team-B
#include "heapstore_core_internal.h"

#include "heapstore.h"
#include "heapstore_log.h"
#include "heapstore_trace.h"
#include "logging.h"
#include "logging_compat.h"
#include "private.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>

#include "airy_memory.h"

const char *heapstore_get_path(heapstore_path_type_t type)
{
    if (type < 0 || type >= heapstore_PATH_MAX) {
        return NULL;
    }
    return heapstore_core_path_names[type];
}

heapstore_error_t heapstore_get_full_path(heapstore_path_type_t type, char *buffer,
                                          size_t buffer_size)
{
    if (!heapstore_is_initialized()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    if (!buffer || buffer_size == 0) {
        return heapstore_ERR_INVALID_PARAM;
    }

    if (type < 0 || type >= heapstore_PATH_MAX) {
        return heapstore_ERR_INVALID_PARAM;
    }

    snprintf(buffer, buffer_size, "%s/%s", heapstore_get_root(),
             heapstore_core_path_names[type]);
    return heapstore_SUCCESS;
}

/**
  * @brief Get the name for a path type
 */
static const char *get_path_name(heapstore_path_type_t type)
{
    switch (type) {
    case heapstore_PATH_LOGS:
        return "logs";
    case heapstore_PATH_REGISTRY:
        return "registry";
    case heapstore_PATH_TRACES:
        return "traces";
    case heapstore_PATH_KERNEL_IPC:
        return "kernel/ipc";
    case heapstore_PATH_KERNEL_MEMORY:
        return "kernel/memory";
    default:
        return NULL;
    }
}

/**
  * @brief Update statistics
 */
static void update_stats_for_path(heapstore_stats_t *stats, heapstore_path_type_t type,
                                  uint64_t dir_size, uint32_t file_count)
{
    switch (type) {
    case heapstore_PATH_LOGS:
        stats->log_usage_bytes += dir_size;
        stats->log_file_count += file_count;
        break;
    case heapstore_PATH_REGISTRY:
        stats->registry_usage_bytes += dir_size;
        break;
    case heapstore_PATH_TRACES:
        stats->trace_usage_bytes += dir_size;
        stats->trace_file_count += file_count;
        break;
    case heapstore_PATH_KERNEL_IPC:
        stats->ipc_usage_bytes += dir_size;
        break;
    case heapstore_PATH_KERNEL_MEMORY:
        stats->memory_usage_bytes += dir_size;
        break;
    default:
        break;
    }
}

heapstore_error_t heapstore_get_stats(heapstore_stats_t *stats)
{
    if (!heapstore_is_initialized()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    if (!stats) {
        return heapstore_ERR_INVALID_PARAM;
    }

    AIRY_MEMSET(stats, 0, sizeof(*stats));

    for (size_t i = 0; i < heapstore_CORE_PATH_COUNT; i++) {
        uint64_t dir_size = 0;
        uint32_t file_count = 0;
        heapstore_path_type_t path_type = heapstore_core_path_order[i];

        const char *path_name = get_path_name(path_type);
        if (!path_name) {
            continue;
        }

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", heapstore_get_root(), path_name);

        heapstore_calculate_directory_size(full_path, &dir_size, &file_count);

        update_stats_for_path(stats, path_type, dir_size, file_count);
        stats->total_disk_usage_bytes += dir_size;
    }

    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_log_write_fast(const char *service, int level, const char *message)
{
    if (!heapstore_is_initialized()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    if (!message) {
        return heapstore_ERR_INVALID_PARAM;
    }

    if (heapstore_core_circuit_is_open()) {
        return heapstore_ERR_CIRCUIT_OPEN;
    }

    bool is_failed = false;

    if (!heapstore_is_initialized()) {
        is_failed = true;
        heapstore_core_circuit_record_failure();
    } else {
        heapstore_log_write(level, service, NULL, NULL, 0, message);
        heapstore_core_circuit_record_success();
    }

    heapstore_core_metrics_update(0, true, is_failed);

    return is_failed ? heapstore_ERR_NOT_INITIALIZED : heapstore_SUCCESS;
}

heapstore_error_t heapstore_log_write_slow(const char *service, int level, const char *message,
                                           const char *trace_id, uint32_t timeout_ms)
{
    if (!heapstore_is_initialized()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    if (!message) {
        return heapstore_ERR_INVALID_PARAM;
    }

    if (heapstore_core_circuit_is_open()) {
        return heapstore_ERR_CIRCUIT_OPEN;
    }

    bool is_failed = false;

    if (!heapstore_is_initialized()) {
        is_failed = true;
        heapstore_core_circuit_record_failure();
    } else {
        heapstore_log_write(level, service, trace_id, NULL, 0, message);
        heapstore_core_circuit_record_success();
    }

    heapstore_core_metrics_update(0, false, is_failed);

    return is_failed ? heapstore_ERR_NOT_INITIALIZED : heapstore_SUCCESS;
}

heapstore_error_t heapstore_cleanup(bool dry_run, uint64_t *freed_bytes)
{
    if (!heapstore_is_initialized()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    bool enable_auto_cleanup = false;
    uint32_t log_retention_days = 0;
    uint32_t trace_retention_days = 0;
    heapstore_core_get_cleanup_config(&enable_auto_cleanup, &log_retention_days,
                                      &trace_retention_days);

    if (!enable_auto_cleanup) {
        if (freed_bytes) {
            *freed_bytes = 0;
        }
        return heapstore_SUCCESS;
    }

    uint64_t total_freed = 0;
    heapstore_error_t result = heapstore_SUCCESS;

    uint64_t log_freed = 0;
    heapstore_error_t log_err = heapstore_log_cleanup(log_retention_days, &log_freed);
    if (log_err == heapstore_SUCCESS) {
        total_freed += log_freed;
    } else {
        result = log_err;
    }

    uint64_t trace_freed = 0;
    heapstore_error_t trace_err = heapstore_trace_cleanup(trace_retention_days, &trace_freed);
    if (trace_err == heapstore_SUCCESS) {
        total_freed += trace_freed;
    } else {
        if (result == heapstore_SUCCESS) {
            result = trace_err;
        }
    }

    if (freed_bytes) {
        *freed_bytes = dry_run ? 0 : total_freed;
    }

    return result;
}
