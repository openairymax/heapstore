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
static void heapstore_lock_init(void)
{
#ifdef _WIN32
    airy_mtx_init(&g_integration_mutex);
#endif
}

/**
  * @brief Clean up the integration-layer mutex
 */
static void heapstore_lock_free(void)
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

    heapstore_lock_init();

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

    heapstore_config_t config = heapstore_def_config(effective_root);

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
    heapstore_lock_free();
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
