// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

// @owner: team-B
#include "error.h"
#include "logging_compat.h"
/**
 * @file trace_store_service.c
  * @brief Kernel trace data storage service implementation
 *
 */

#include "../../include/heapstore.h"
#include "../../include/heapstore_trace.h"
#include "../../include/utils.h"
#include "airy_dirent.h"
#include "atomic_compat.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "airy_memory.h"

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static void trace_store_service_check_storage_limit(const char *current_file);
static int trace_store_service_cleanup_old_files(int max_files);

#ifndef HEAPSTORE_TRACE_POINT_DEFINED
#define HEAPSTORE_TRACE_POINT_DEFINED

typedef struct {
    uint64_t timestamp_ns;
    char component[64];
    char operation[128];
    int64_t duration_ns;
    bool success;
    char trace_id[64];
    char metadata[256];
} heapstore_trace_point_t;

#endif

#ifndef HEAPSTORE_TRACE_QUERY_DEFINED
#define HEAPSTORE_TRACE_QUERY_DEFINED

typedef struct {
    time_t start_time;
    time_t end_time;
    char component_filter[64];
    char operation_filter[128];
    bool success_only;
    int max_results;
} heapstore_trace_query_t;

#endif

/**
  * @brief Trace storage service context
 */
typedef struct {
    char storage_path[512];
    uint64_t max_storage_bytes;
    uint32_t sampling_rate;
    atomic_uint_fast32_t is_initialized;
    uint64_t total_traces_stored;
    uint64_t total_bytes_stored;
} trace_store_service_ctx_t;

static trace_store_service_ctx_t g_ctx = {0};

/**
  * @brief Initialize the trace storage service
 *
  * @param storage_path Storage path
  * @param max_storage_bytes Maximum storage bytes
  * @param sampling_rate Sampling rate (1 stores every trace point)
  * @return int: 0 on success, non-zero error code
 */
int trace_store_service_init(const char *storage_path, uint64_t max_storage_bytes,
                             uint32_t sampling_rate)
{
    if (!storage_path) {
        return AIRY_EINVAL;
    }

    if (atomic_load_explicit(&g_ctx.is_initialized, memory_order_acquire)) {
        return 0;
    }

    AIRY_STRNCPY_TERM(g_ctx.storage_path, storage_path, sizeof(g_ctx.storage_path));

    g_ctx.max_storage_bytes = max_storage_bytes > 0 ? max_storage_bytes : 500 * 1024 * 1024;
    g_ctx.sampling_rate = sampling_rate > 0 ? sampling_rate : 1;
    g_ctx.total_traces_stored = 0;
    g_ctx.total_bytes_stored = 0;

#ifdef _WIN32
    if (_mkdir(g_ctx.storage_path) != 0) {
        if (errno != EEXIST) {
            return AIRY_ERR_NOT_FOUND;
        }
    }
#else
    if (mkdir(g_ctx.storage_path, 0755) != 0) {
        if (errno != EEXIST) {
            return AIRY_ERR_NOT_FOUND;
        }
    }
#endif

    atomic_store_explicit(&g_ctx.is_initialized, 1, memory_order_release);
    return 0;
}

/**
  * @brief Store a trace point
 *
  * @param trace_point Trace point data
  * @return int: 0 on success, non-zero error code
 */
int trace_store_point(const heapstore_trace_point_t *trace_point)
{
    if (!atomic_load_explicit(&g_ctx.is_initialized, memory_order_acquire) || !trace_point) {
        return AIRY_EINVAL;
    }

    static uint32_t counter = 0;
    counter++;
    if (counter % g_ctx.sampling_rate != 0) {
        return 0;
    }

    time_t now = time(NULL);
    struct tm tm_buf;
    struct tm *tm_info = localtime_r(&now, &tm_buf);
    if (!tm_info) {
        return AIRY_ERR_INVALID_PARAM;
    }

    char filename[512];
    snprintf(filename, sizeof(filename), "%s/trace_%04d%02d%02d.bin", g_ctx.storage_path,
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday);

    FILE *f = fopen(filename, "ab");
    if (!f) {
        return AIRY_ERR_NULL_POINTER;
    }

    size_t written = fwrite(trace_point, sizeof(heapstore_trace_point_t), 1, f);
    fclose(f);

    if (written != 1) {
        return AIRY_ERR_OUT_OF_MEMORY;
    }

    g_ctx.total_traces_stored++;
    g_ctx.total_bytes_stored += sizeof(heapstore_trace_point_t);

    trace_store_service_check_storage_limit(filename);

    return 0;
}

/**
  * @brief Batch-store trace points
 *
  * @param trace_points Trace point array
  * @param count Trace point count
  * @return int: count stored, or an error code
 */
int trace_store_service_store_batch(const heapstore_trace_point_t *trace_points, int count)
{
    if (!atomic_load_explicit(&g_ctx.is_initialized, memory_order_acquire) || !trace_points ||
        count <= 0) {
        return AIRY_EINVAL;
    }

    int stored = 0;
    for (int i = 0; i < count; i++) {
        if (trace_store_point(&trace_points[i]) == 0) {
            stored++;
        }
    }

    return stored;
}

/**
  * @brief Check storage limits and clean up
 *
  * @param current_file Current trace file
 */
static void trace_store_service_check_storage_limit(const char *current_file)
{
    if (g_ctx.total_bytes_stored <= g_ctx.max_storage_bytes) {
        return;
    }

    (void)current_file;
    int deleted = trace_store_service_cleanup_old_files(1);
    if (deleted > 0) {
        AIRY_LOG_WARN("trace_store: storage limit exceeded, cleaned %d old files", deleted);
    } else {
        AIRY_LOG_WARN(
            "trace_store: storage limit exceeded (%llu bytes > %llu bytes), no old files to clean",
            (unsigned long long)g_ctx.total_bytes_stored,
            (unsigned long long)g_ctx.max_storage_bytes);
    }
}

/**
  * @brief Query trace data
 *
  * @param query Query conditions
  * @param out_traces Output trace point array
  * @param max_traces Maximum trace point count
  * @return int: trace points returned, or an error code
 */
int trace_store_service_query_traces(const heapstore_trace_query_t *query,
                                     heapstore_trace_point_t **out_traces, int max_traces)
{
    if (!query || !out_traces || max_traces <= 0) {
        return AIRY_EINVAL;
    }

    if (!atomic_load_explicit(&g_ctx.is_initialized, memory_order_acquire)) {
        return AIRY_ERR_OVERFLOW;
    }

    *out_traces = NULL;

    DIR *dir = opendir(g_ctx.storage_path);
    if (!dir) {
        return AIRY_ERR_INVALID_PARAM;
    }

    heapstore_trace_point_t *results = NULL;
    SAFE_MALLOC_ARRAY(results, max_traces, sizeof(heapstore_trace_point_t));
    if (!results) {
        closedir(dir);
        return AIRY_ERR_OUT_OF_MEMORY;
    }

    int found_count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL && found_count < max_traces) {
        if (strncmp(entry->d_name, "trace_", 6) != 0) {
            continue;
        }

        char filepath[768];
        snprintf(filepath, sizeof(filepath), "%s/%s", g_ctx.storage_path, entry->d_name);

        FILE *f = fopen(filepath, "rb");
        if (!f) {
            continue;
        }

        heapstore_trace_point_t point;
        while (fread(&point, sizeof(heapstore_trace_point_t), 1, f) == 1 &&
               found_count < max_traces) {
            time_t point_time = (time_t)(point.timestamp_ns / 1000000000ULL);

            if (query->start_time > 0 && point_time < query->start_time) {
                continue;
            }
            if (query->end_time > 0 && point_time > query->end_time) {
                continue;
            }
            if (query->component_filter[0] != '\0' &&
                strstr(point.component, query->component_filter) == NULL) {
                continue;
            }
            if (query->operation_filter[0] != '\0' &&
                strstr(point.operation, query->operation_filter) == NULL) {
                continue;
            }
            if (query->success_only && !point.success) {
                continue;
            }

            results[found_count++] = point;
        }

        fclose(f);
    }

    closedir(dir);

    if (found_count == 0) {
        AIRY_FREE(results);
        *out_traces = NULL;
        return 0;
    }

    heapstore_trace_point_t *final_results = NULL;
    SAFE_MALLOC_ARRAY(final_results, found_count, sizeof(heapstore_trace_point_t));
    if (!final_results) {
        AIRY_FREE(results);
        return AIRY_ERR_OUT_OF_MEMORY;
    }

    __builtin_memcpy(final_results, results, found_count * sizeof(heapstore_trace_point_t));
    AIRY_FREE(results);

    *out_traces = final_results;
    return found_count;
}

/**
  * @brief Free query results
 *
  * @param traces Trace point array
 * @param count number of trace points
 */
void trace_store_service_free_traces(heapstore_trace_point_t *traces, int count)
{
    if (!traces) {
        return;
    }
    AIRY_FREE(traces);
}

/**
  * @brief Export trace data
 *
  * @param start_time Start time
  * @param end_time End time
  * @param export_format Export format ("json", "csv", "binary")
  * @param export_path Export path
  * @return int: bytes exported, or an error code
 */
int trace_store_service_export_traces(const time_t *start_time, const time_t *end_time,
                                      const char *export_format, const char *export_path)
{
    if (!export_format || !export_path)
        return AIRY_EINVAL;
    if (!atomic_load_explicit(&g_ctx.is_initialized, memory_order_acquire))
        return AIRY_EINVAL;

    FILE *f = fopen(export_path, "w");
    if (!f)
        return AIRY_ERR_SYS_NOT_INIT;

    int exported = 0;
    DIR *dir = opendir(g_ctx.storage_path);
    if (!dir) {
        fclose(f);
        return AIRY_ERR_NULL_POINTER;
    }

    time_t t_start = start_time ? *start_time : 0;
    time_t t_end = end_time ? *end_time : time(NULL);

    if (strcmp(export_format, "json") == 0) {
        fputs("{\"traces\": [\n", f);
    } else if (strcmp(export_format, "csv") == 0) {
        fputs("timestamp_ns,component,operation,duration_ns,success,trace_id,metadata\n", f);
    }

    struct dirent *entry;
    bool first_json = true;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", g_ctx.storage_path, entry->d_name);

        struct stat file_stat;
        if (stat(filepath, &file_stat) != 0)
            continue;
        if (S_ISDIR(file_stat.st_mode))
            continue;
        if (file_stat.st_mtime < t_start || file_stat.st_mtime > t_end)
            continue;

        FILE *tf = fopen(filepath, "r");
        if (!tf)
            continue;

        char line[1024];
        while (fgets(line, sizeof(line), tf)) {
            line[strcspn(line, "\n")] = '\0';
            if (strlen(line) == 0)
                continue;

            if (strcmp(export_format, "json") == 0) {
                if (!first_json)
                    fputs(",\n", f);
                {
                    char _buf[2048];
                    snprintf(_buf, sizeof(_buf), "  %s", line);
                    fputs(_buf, f);
                }
                first_json = false;
            } else if (strcmp(export_format, "csv") == 0) {
                {
                    char _buf[2048];
                    snprintf(_buf, sizeof(_buf), "%s\n", line);
                    fputs(_buf, f);
                }
            }
            exported++;
        }
        fclose(tf);
    }

    if (strcmp(export_format, "json") == 0) {
        fputs("\n]}\n", f);
    }

    closedir(dir);
    fclose(f);

    f = fopen(export_path, "rb");
    if (!f)
        return exported;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);

    (void)size;
    return exported;
}

/**
  * @brief Get trace storage statistics
 *
  * @param out_total_traces Output total trace point count
  * @param out_total_bytes Output total stored bytes
  * @param out_sampling_rate Output sampling rate
  * @return int: 0 on success, non-zero error code
 */
int trace_store_service_get_stats(uint64_t *out_total_traces, uint64_t *out_total_bytes,
                                  uint32_t *out_sampling_rate)
{
    if (!atomic_load_explicit(&g_ctx.is_initialized, memory_order_acquire)) {
        return AIRY_EINVAL;
    }

    if (out_total_traces)
        *out_total_traces = g_ctx.total_traces_stored;
    if (out_total_bytes)
        *out_total_bytes = g_ctx.total_bytes_stored;
    if (out_sampling_rate)
        *out_sampling_rate = g_ctx.sampling_rate;

    return 0;
}

/**
  * @brief Clean up old trace data
 *
  * @param days_to_keep Retention days
  * @return int: number of files deleted
 */
int trace_store_service_cleanup_old_files(int days_to_keep)
{
    if (!atomic_load_explicit(&g_ctx.is_initialized, memory_order_acquire))
        return AIRY_EINVAL;
    if (days_to_keep <= 0) {
        days_to_keep = 7;
    }

    time_t cutoff = time(NULL) - ((time_t)days_to_keep * 86400);
    int deleted_count = 0;

    DIR *dir = opendir(g_ctx.storage_path);
    if (!dir)
        return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", g_ctx.storage_path, entry->d_name);

        struct stat file_stat;
        if (stat(filepath, &file_stat) != 0)
            continue;
        if (S_ISDIR(file_stat.st_mode))
            continue;

        if (file_stat.st_mtime < cutoff) {
            if (unlink(filepath) == 0) {
                g_ctx.total_bytes_stored -= (uint64_t)file_stat.st_size;
                deleted_count++;
            }
        }
    }

    closedir(dir);
    return deleted_count;
}

/**
  * @brief Shut down the trace storage service
 */
void trace_store_service_shutdown(void)
{
    if (!atomic_load_explicit(&g_ctx.is_initialized, memory_order_acquire)) {
        return;
    }

    __builtin_memset(&g_ctx, 0, sizeof(g_ctx));
}
