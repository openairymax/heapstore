// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_integration.c
 * @brief Integration of heapstore with AgentRT core modules.
 */

// @owner: team-B
#include "heapstore_integration.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "airy_memory.h"

#ifdef _WIN32
#else
#include "platform.h"

#include <sys/stat.h>
#include <unistd.h>
#endif

static bool g_integration_initialized = false;
static char g_root_path[512] = {0};

#ifdef _WIN32
static airy_mtx_t g_integration_mutex;
#else
static airy_mtx_t g_integration_mutex = {0};
#endif

/**
  * @brief Initialize the integration-layer mutex
 */
static void __attribute__((unused)) integration_lock_init(void)
{
#ifdef _WIN32
    airy_mtx_init(&g_integration_mutex);
#endif
}

/**
  * @brief Clean up the integration-layer mutex
 */
static void __attribute__((unused)) integration_lock_cleanup(void)
{
#ifdef _WIN32
    airy_mtx_destroy(&g_integration_mutex);
#endif
}

/**
  * @brief Acquire the integration-layer mutex
 */
static void integration_lock(void)
{
#ifdef _WIN32
    airy_mtx_lock(&g_integration_mutex);
#else
    airy_mtx_lock(&g_integration_mutex);
#endif
}

/**
  * @brief Release the integration-layer mutex
 */
static void integration_unlock(void)
{
#ifdef _WIN32
    airy_mtx_unlock(&g_integration_mutex);
#else
    airy_mtx_unlock(&g_integration_mutex);
#endif
}

airy_err_t heapstore_integration_init(const char *root_path)
{

    integration_lock_init();

    integration_lock();

    if (g_integration_initialized) {
        integration_unlock();
        return AIRY_SUCCESS;
    }

    const char *effective_root = root_path;
    char auto_root[512];
    if (!effective_root) {
        const char *env = getenv("AIRY_HEAPSTORE_ROOT");
        if (env && env[0]) {
            effective_root = env;
        } else {
            const char *data_dir = airy_data_dir();
            if (data_dir && data_dir[0]) {
                snprintf(auto_root, sizeof(auto_root), "%s/agentrt/heapstore", data_dir);
            } else {
                snprintf(auto_root, sizeof(auto_root), "/tmp/agentrt/heapstore");
            }
            effective_root = auto_root;
        }
    }

    heapstore_config_t config = {.root_path = effective_root,
                                 .max_log_size_mb = 100,
                                 .log_retention_days = 7,
                                 .trace_retention_days = 3,
                                 .enable_auto_cleanup = true,
                                 .enable_log_rotation = true,
                                 .enable_trace_export = true,
                                 .db_vacuum_interval_days = 7,
                                 .circuit_breaker_threshold = 5,
                                 .circuit_breaker_timeout_sec = 30};

    heapstore_error_t err = heapstore_init(&config);
    if (err != heapstore_SUCCESS) {
        integration_unlock();
        return AIRY_EIO;
    }

    if (root_path) {
        AIRY_STRNCPY_TERM(g_root_path, root_path, sizeof(g_root_path));
    } else {
        const char *env = getenv("AIRY_HEAPSTORE_ROOT");
        if (env && env[0]) {
            AIRY_STRNCPY_TERM(g_root_path, env, sizeof(g_root_path));
        } else {
            const char *data_dir = airy_data_dir();
            if (data_dir && data_dir[0]) {
                snprintf(g_root_path, sizeof(g_root_path), "%s/agentrt/heapstore",
                         data_dir);
            } else {
                snprintf(g_root_path, sizeof(g_root_path), "/tmp/agentrt/heapstore");
            }
        }
        g_root_path[sizeof(g_root_path) - 1] = '\0';
    }

    g_integration_initialized = true;
    integration_unlock();

    return AIRY_SUCCESS;
}

void heapstore_integration_shutdown(void)
{
    integration_lock();

    if (!g_integration_initialized) {
        integration_unlock();
        return;
    }

    heapstore_shutdown();
    g_integration_initialized = false;
    g_root_path[0] = '\0';

    integration_unlock();
}

airy_err_t heapstore_syscall_session_save(const char *session_id, const char *metadata,
                                          uint64_t created_ns, uint64_t last_active_ns)
{

    if (!g_integration_initialized) {
        return AIRY_ENOTINIT;
    }
    if (!session_id) {
        return AIRY_EINVAL;
    }

    heapstore_session_record_t record;
    __builtin_memset(&record, 0, sizeof(record));
    AIRY_STRNCPY_TERM(record.id, session_id, sizeof(record.id));
    if (metadata) {
        AIRY_STRNCPY_TERM(record.user_id, metadata, sizeof(record.user_id));
    }
    record.created_at = created_ns;
    record.last_active_at = last_active_ns;
    record.ttl_seconds = 0;
    AIRY_STRNCPY_TERM(record.status, "active", sizeof(record.status));

    heapstore_error_t err = heapstore_registry_add_session(&record);
    return (err == heapstore_SUCCESS) ? AIRY_SUCCESS : AIRY_EIO;
}

airy_err_t heapstore_syscall_session_load(const char *session_id, char **out_metadata,
                                          uint64_t *out_created_ns, uint64_t *out_last_active_ns)
{

    if (!g_integration_initialized) {
        return AIRY_ENOTINIT;
    }
    if (!session_id || !out_metadata) {
        return AIRY_EINVAL;
    }

    heapstore_session_record_t record;
    __builtin_memset(&record, 0, sizeof(record));

    heapstore_error_t err = heapstore_registry_get_session(session_id, &record);
    if (err != heapstore_SUCCESS) {
        return (err == heapstore_ERR_NOT_FOUND) ? AIRY_ENOENT : AIRY_EIO;
    }

    if (out_metadata) {
        *out_metadata = AIRY_STRDUP(record.user_id);
        if (!*out_metadata) {
            return AIRY_ENOMEM;
        }
    }
    if (out_created_ns) {
        *out_created_ns = record.created_at;
    }
    if (out_last_active_ns) {
        *out_last_active_ns = record.last_active_at;
    }

    return AIRY_SUCCESS;
}

airy_err_t heapstore_syscall_session_delete(const char *session_id)
{
    if (!g_integration_initialized) {
        return AIRY_ENOTINIT;
    }
    if (!session_id) {
        return AIRY_EINVAL;
    }

    heapstore_error_t err = heapstore_registry_delete_session(session_id);
    return (err == heapstore_SUCCESS) ? AIRY_SUCCESS : AIRY_EIO;
}

airy_err_t heapstore_syscall_session_list(char ***out_sessions, size_t *out_count)
{

    if (!g_integration_initialized) {
        return AIRY_ENOTINIT;
    }
    if (!out_sessions || !out_count) {
        return AIRY_EINVAL;
    }

    *out_sessions = NULL;
    *out_count = 0;

    heapstore_registry_iter_t *iter = NULL;
    heapstore_error_t err = heapstore_registry_query_sessions(NULL, &iter);
    if (err != heapstore_SUCCESS || !iter) {
        return AIRY_EIO;
    }

    size_t count = 0;
    size_t capacity = 16;
    if (capacity > SIZE_MAX / sizeof(char *)) {
        heapstore_registry_iter_destroy(iter);
        return AIRY_EOVERFLOW;
    }
    char **sessions = (char **)AIRY_MALLOC(capacity * sizeof(char *));
    if (!sessions) {
        heapstore_registry_iter_destroy(iter);
        return AIRY_ENOMEM;
    }

    heapstore_session_record_t record;
    while (true) {
        err = heapstore_registry_iter_next(iter, &record);
        if (err == heapstore_ERR_NOT_FOUND) {
            break;
        }
        if (err != heapstore_SUCCESS) {
            goto cleanup_error;
        }

        if (count >= capacity) {
            capacity *= 2;
            char **new_sessions = (char **)AIRY_REALLOC(sessions, capacity * sizeof(char *));
            if (!new_sessions) {
                goto cleanup_error;
            }
            sessions = new_sessions;
        }

        sessions[count] = AIRY_STRDUP(record.id);
        if (!sessions[count]) {
            goto cleanup_error;
        }
        count++;
    }

    heapstore_registry_iter_destroy(iter);

    *out_sessions = sessions;
    *out_count = count;

    return AIRY_SUCCESS;

cleanup_error:
    for (size_t i = 0; i < count; i++) {
        AIRY_FREE(sessions[i]);
    }
    AIRY_FREE(sessions);
    heapstore_registry_iter_destroy(iter);
    return (err == heapstore_ERR_NOT_FOUND) ? AIRY_EIO : AIRY_ENOMEM;
}

airy_err_t heapstore_syscall_trace_save(const char *trace_id, const char *span_id,
                                        const char *parent_id, const char *name,
                                        int64_t start_time_us, int64_t end_time_us, int status,
                                        const char *events_json)
{

    if (!g_integration_initialized) {
        return AIRY_ENOTINIT;
    }
    if (!trace_id || !span_id || !name) {
        return AIRY_EINVAL;
    }

    heapstore_span_t record;
    __builtin_memset(&record, 0, sizeof(record));

    AIRY_STRNCPY_TERM(record.trace_id, trace_id, sizeof(record.trace_id));
    AIRY_STRNCPY_TERM(record.span_id, span_id, sizeof(record.span_id));
    if (parent_id) {
        AIRY_STRNCPY_TERM(record.parent_span_id, parent_id, sizeof(record.parent_span_id));
    }
    AIRY_STRNCPY_TERM(record.name, name, sizeof(record.name));
    record.start_time_ns = (uint64_t)start_time_us * 1000;
    record.end_time_ns = (uint64_t)end_time_us * 1000;
    snprintf(record.status, sizeof(record.status), "%d", status);
    if (events_json) {
        if (strlen(events_json) > 0) {
            record.attributes = AIRY_STRDUP(events_json);
            if (!record.attributes) {
                return AIRY_ENOMEM;
            }
            record.attribute_count = 1;
        }
    }

    heapstore_error_t err = heapstore_trace_write_span(&record);

    if (record.attributes) {
        AIRY_FREE(record.attributes);
    }

    return (err == heapstore_SUCCESS) ? AIRY_SUCCESS : AIRY_EIO;
}

airy_err_t heapstore_syscall_trace_export(char **out_traces)
{
    if (!g_integration_initialized) {
        return AIRY_ENOTINIT;
    }
    if (!out_traces) {
        return AIRY_EINVAL;
    }

    heapstore_error_t err = heapstore_trace_export_to_json(out_traces, true);
    return (err == heapstore_SUCCESS) ? AIRY_SUCCESS : AIRY_EIO;
}

/* ─── memoryrovol 原始数据持久化 ────────────────────────────────────────
 * 数据文件布局（<root> = heapstore 根）：
 *   <root>/memory/memoryrovol/<record_id>.bin   — 原始数据（真正落盘）
 *   <root>/memory/memoryrovol/<record_id>.meta  — 元数据（JSON 字符串，可为空）
 * pool/allocation 记录作为内存索引保留（含大小/时间/状态）。 */

static void memoryrovol_build_path(const char *record_id, const char *ext, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s/memory/memoryrovol/%s%s", heapstore_get_root(), record_id, ext);
}

airy_err_t heapstore_memory_raw_save(const void *data, size_t len, const char *metadata,
                                     char **out_record_id)
{

    if (!g_integration_initialized) {
        return AIRY_ENOTINIT;
    }
    if (!data || len == 0 || !out_record_id) {
        return AIRY_EINVAL;
    }

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/memory/memoryrovol", heapstore_get_root());
    heapstore_ensure_directory(dir);

    heapstore_memory_pool_t pool;
    __builtin_memset(&pool, 0, sizeof(pool));

    snprintf(pool.pool_id, sizeof(pool.pool_id), "mem_raw_%llu", (unsigned long long)time(NULL));
    AIRY_STRNCPY_TERM(pool.name, "memoryrovol_raw", sizeof(pool.name));
    pool.total_size = len;
    pool.used_size = len;
    pool.block_size = len;
    pool.block_count = 1;
    pool.free_block_count = 0;
    pool.created_at = (uint64_t)time(NULL);
    AIRY_STRNCPY_TERM(pool.status, "active", sizeof(pool.status));

    heapstore_error_t err = heapstore_memory_record_pool(&pool);
    if (err != heapstore_SUCCESS) {
        return AIRY_EIO;
    }

    heapstore_memory_allocation_t alloc;
    __builtin_memset(&alloc, 0, sizeof(alloc));
    AIRY_STRNCPY_TERM(alloc.allocation_id, pool.pool_id, sizeof(alloc.allocation_id));
    AIRY_STRNCPY_TERM(alloc.pool_id, pool.pool_id, sizeof(alloc.pool_id));
    alloc.size = len;
    alloc.address = 0; /* 不存进程指针：跨进程/重启后无效，真实数据落盘 */
    alloc.allocated_at = (uint64_t)time(NULL);
    alloc.freed_at = 0;
    AIRY_STRNCPY_TERM(alloc.status, "allocated", sizeof(alloc.status));

    heapstore_error_t alloc_err = heapstore_memory_record_allocation(&alloc);
    if (alloc_err != heapstore_SUCCESS) {
        return AIRY_EIO;
    }

    /* 真实数据写入 <record_id>.bin */
    char data_path[512];
    memoryrovol_build_path(pool.pool_id, ".bin", data_path, sizeof(data_path));
    FILE *fp = fopen(data_path, "wb");
    if (!fp) {
        return AIRY_EIO;
    }
    size_t written = fwrite(data, 1, len, fp);
    int ferr = ferror(fp);
    fclose(fp);
    if (written != len || ferr) {
        remove(data_path);
        return AIRY_EIO;
    }

    /* 元数据写入 <record_id>.meta（可为空，不写文件即无元数据） */
    if (metadata && metadata[0]) {
        char meta_path[512];
        memoryrovol_build_path(pool.pool_id, ".meta", meta_path, sizeof(meta_path));
        fp = fopen(meta_path, "wb");
        if (!fp) {
            remove(data_path);
            return AIRY_EIO;
        }
        size_t mw = fwrite(metadata, 1, strlen(metadata), fp);
        int merr = ferror(fp);
        fclose(fp);
        if (mw != strlen(metadata) || merr) {
            remove(data_path);
            remove(meta_path);
            return AIRY_EIO;
        }
    }

    *out_record_id = AIRY_STRDUP(pool.pool_id);
    if (!*out_record_id) {
        return AIRY_ENOMEM;
    }

    return AIRY_SUCCESS;
}

airy_err_t heapstore_memory_raw_load(const char *record_id, void **out_data, size_t *out_len,
                                     char **out_metadata)
{

    if (!g_integration_initialized) {
        return AIRY_ENOTINIT;
    }
    if (!record_id || !out_data || !out_len) {
        return AIRY_EINVAL;
    }

    /* 从磁盘真实读取数据 */
    char data_path[512];
    memoryrovol_build_path(record_id, ".bin", data_path, sizeof(data_path));
    FILE *fp = fopen(data_path, "rb");
    if (!fp) {
        *out_data = NULL;
        *out_len = 0;
        if (out_metadata)
            *out_metadata = NULL;
        return AIRY_ENOENT;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return AIRY_EIO;
    }
    long fsize = ftell(fp);
    if (fsize < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return AIRY_EIO;
    }
    size_t copy_len = (size_t)fsize;
    void *buf = AIRY_MALLOC(copy_len > 0 ? copy_len : 1);
    if (!buf) {
        fclose(fp);
        return AIRY_ENOMEM;
    }
    size_t got = copy_len > 0 ? fread(buf, 1, copy_len, fp) : 0;
    int rerr = ferror(fp);
    fclose(fp);
    if (got != copy_len || rerr) {
        AIRY_FREE(buf);
        return AIRY_EIO;
    }

    *out_data = buf;
    *out_len = copy_len;

    if (out_metadata) {
        *out_metadata = NULL;
        char meta_path[512];
        memoryrovol_build_path(record_id, ".meta", meta_path, sizeof(meta_path));
        fp = fopen(meta_path, "rb");
        if (fp) {
            if (fseek(fp, 0, SEEK_END) != 0) {
                fclose(fp);
            } else {
                long msize = ftell(fp);
                if (msize >= 0 && fseek(fp, 0, SEEK_SET) == 0) {
                    char *m = AIRY_MALLOC((size_t)msize + 1);
                    if (m) {
                        size_t mg = fread(m, 1, (size_t)msize, fp);
                        if (mg == (size_t)msize) {
                            m[msize] = '\0';
                            *out_metadata = m;
                        } else {
                            AIRY_FREE(m);
                        }
                    }
                }
                fclose(fp);
            }
        }
    }

    return AIRY_SUCCESS;
}

airy_err_t heapstore_memory_raw_delete(const char *record_id)
{
    if (!g_integration_initialized) {
        return AIRY_ENOTINIT;
    }
    if (!record_id) {
        return AIRY_EINVAL;
    }

    char data_path[512];
    memoryrovol_build_path(record_id, ".bin", data_path, sizeof(data_path));
    remove(data_path);
    char meta_path[512];
    memoryrovol_build_path(record_id, ".meta", meta_path, sizeof(meta_path));
    remove(meta_path);

    heapstore_error_t err = heapstore_memory_free_allocation(record_id);
    if (err == heapstore_ERR_NOT_FOUND) {
        heapstore_memory_pool_t pool;
        __builtin_memset(&pool, 0, sizeof(pool));
        AIRY_STRNCPY_TERM(pool.pool_id, record_id, sizeof(pool.pool_id));
        AIRY_STRNCPY_TERM(pool.status, "deleted", sizeof(pool.status));
        err = heapstore_memory_record_pool(&pool);
    }

    return (err == heapstore_SUCCESS) ? AIRY_SUCCESS : AIRY_EIO;
}

airy_err_t heapstore_ipc_channel_save(const char *channel_id, const char *state_json)
{

    if (!g_integration_initialized) {
        return AIRY_ENOTINIT;
    }
    if (!channel_id || !state_json) {
        return AIRY_EINVAL;
    }

    heapstore_ipc_channel_t record;
    __builtin_memset(&record, 0, sizeof(record));

    AIRY_STRNCPY_TERM(record.channel_id, channel_id, sizeof(record.channel_id));
    AIRY_STRNCPY_TERM(record.name, channel_id, sizeof(record.name));
    AIRY_STRNCPY_TERM(record.type, "binder", sizeof(record.type));
    record.created_at = (uint64_t)time(NULL);
    record.last_activity_at = (uint64_t)time(NULL);
    AIRY_STRNCPY_TERM(record.status, "active", sizeof(record.status));

    heapstore_error_t err = heapstore_ipc_record_channel(&record);
    return (err == heapstore_SUCCESS) ? AIRY_SUCCESS : AIRY_EIO;
}

airy_err_t heapstore_ipc_channel_load(const char *channel_id, char **out_state)
{

    if (!g_integration_initialized) {
        return AIRY_ENOTINIT;
    }
    if (!channel_id || !out_state) {
        return AIRY_EINVAL;
    }

    heapstore_ipc_channel_t record;
    __builtin_memset(&record, 0, sizeof(record));

    heapstore_error_t err = heapstore_ipc_get_channel(channel_id, &record);
    if (err != heapstore_SUCCESS) {
        return (err == heapstore_ERR_NOT_FOUND) ? AIRY_ENOENT : AIRY_EIO;
    }

    char buffer[512];
    snprintf(buffer, sizeof(buffer),
             "{\"channel_id\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"status\":%s,"
             "\"buffer_size\":%llu,\"current_usage\":%llu}",
             record.channel_id, record.name, record.type, record.status,
             (unsigned long long)record.buffer_size, (unsigned long long)record.current_usage);

    *out_state = AIRY_STRDUP(buffer);

    return *out_state ? AIRY_SUCCESS : AIRY_ENOMEM;
}

airy_err_t heapstore_logging_write(const char *module, int level, const char *trace_id,
                                   const char *message, uint64_t timestamp_ns)
{

    if (!g_integration_initialized) {
        return AIRY_ENOTINIT;
    }
    if (!module || !message) {
        return AIRY_EINVAL;
    }

    heapstore_log_level_t log_level;
    switch (level) {
    case 0:
        log_level = HEAPSTORE_LOG_DEBUG;
        break;
    case 1:
        log_level = HEAPSTORE_LOG_INFO;
        break;
    case 2:
        log_level = HEAPSTORE_LOG_WARN;
        break;
    case 3:
        log_level = HEAPSTORE_LOG_ERROR;
        break;
    default:
        log_level = HEAPSTORE_LOG_INFO;
        break;
    }

    heapstore_log_file_info_t info;
    __builtin_memset(&info, 0, sizeof(info));

    heapstore_log_write((int)log_level, module, trace_id, __FILE__, __LINE__, "%s", message);

    return AIRY_SUCCESS;
}

airy_err_t heapstore_integration_health_check(char **out_health_json)
{
    if (!out_health_json) {
        return AIRY_EINVAL;
    }

    bool registry_ok = false, trace_ok = false, log_ok = false;
    bool ipc_ok = false, memory_ok = false;

    if (g_integration_initialized) {
        heapstore_health_check(&registry_ok, &trace_ok, &log_ok, &ipc_ok, &memory_ok);
    }

    char buffer[1024];
    snprintf(buffer, sizeof(buffer),
             "{"
             "\"initialized\":%s,"
             "\"registry\":%s,"
             "\"trace\":%s,"
             "\"log\":%s,"
             "\"ipc\":%s,"
             "\"memory\":%s,"
             "\"overall\":%s"
             "}",
             g_integration_initialized ? "true" : "false", registry_ok ? "true" : "false",
             trace_ok ? "true" : "false", log_ok ? "true" : "false", ipc_ok ? "true" : "false",
             memory_ok ? "true" : "false",
             (g_integration_initialized && registry_ok && trace_ok && log_ok) ? "true" : "false");

    *out_health_json = AIRY_STRDUP(buffer);
    return *out_health_json ? AIRY_SUCCESS : AIRY_ENOMEM;
}

airy_err_t heapstore_integration_get_stats(char **out_stats_json)
{
    if (!out_stats_json) {
        return AIRY_EINVAL;
    }

    if (!g_integration_initialized) {
        *out_stats_json = AIRY_STRDUP("{\"error\":\"not initialized\"}");
        return *out_stats_json ? AIRY_SUCCESS : AIRY_ENOMEM;
    }

    heapstore_stats_t stats;
    heapstore_metrics_t metrics;

    heapstore_error_t err1 = heapstore_get_stats(&stats);
    heapstore_error_t err2 = heapstore_get_metrics(&metrics);

    if (err1 != heapstore_SUCCESS || err2 != heapstore_SUCCESS) {
        *out_stats_json = AIRY_STRDUP("{\"error\":\"failed to get stats\"}");
        return *out_stats_json ? AIRY_SUCCESS : AIRY_ENOMEM;
    }

    char buffer[2048];
    snprintf(buffer, sizeof(buffer),
             "{"
             "\"disk_usage\":{"
             "\"total_bytes\":%llu,"
             "\"log_bytes\":%llu,"
             "\"registry_bytes\":%llu,"
             "\"trace_bytes\":%llu,"
             "\"ipc_bytes\":%llu,"
             "\"memory_bytes\":%llu"
             "},"
             "\"file_counts\":{"
             "\"log_files\":%u,"
             "\"trace_files\":%u"
             "},"
             "\"performance\":{"
             "\"total_operations\":%llu,"
             "\"failed_operations\":%llu,"
             "\"fast_path_ops\":%llu,"
             "\"slow_path_ops\":%llu,"
             "\"circuit_breaker_trips\":%llu,"
             "\"avg_operation_time_ns\":%.2f,"
             "\"peak_concurrent_ops\":%llu"
             "}"
             "}",
             (unsigned long long)stats.total_disk_usage_bytes,
             (unsigned long long)stats.log_usage_bytes,
             (unsigned long long)stats.registry_usage_bytes,
             (unsigned long long)stats.trace_usage_bytes, (unsigned long long)stats.ipc_usage_bytes,
             (unsigned long long)stats.memory_usage_bytes, stats.log_file_count,
             stats.trace_file_count, (unsigned long long)metrics.total_operations,
             (unsigned long long)metrics.failed_operations,
             (unsigned long long)metrics.fast_path_operations,
             (unsigned long long)metrics.slow_path_operations,
             (unsigned long long)metrics.circuit_breaker_trips, metrics.avg_operation_time_ns,
             (unsigned long long)metrics.peak_concurrent_ops);

    *out_stats_json = AIRY_STRDUP(buffer);
    return *out_stats_json ? AIRY_SUCCESS : AIRY_ENOMEM;
}
