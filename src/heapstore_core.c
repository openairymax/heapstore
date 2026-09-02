// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_core.c
 * @brief AgentRT data partition core: lifecycle, configuration and
 *        directory layout (functional domain after heapstore_core.c split;
 *        read/write paths live in heapstore_core_write.c, the circuit
 *        breaker in heapstore_core_circuit.c, operation metrics in
 *        heapstore_core_metrics.c, error strings in heapstore_core_error.c).
 */

// @owner: team-B
#include "heapstore_core_internal.h"

#include "heapstore.h"
#include "heapstore_ipc.h"
#include "heapstore_log.h"
#include "heapstore_memory.h"
#include "heapstore_migration.h"
#include "heapstore_registry.h"
#include "heapstore_trace.h"
#include "logging.h"
#include "logging_compat.h"
#include "platform.h"
#include "private.h"
#include "utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "airy_memory.h"

#include "atomic_compat.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#define mkdir(path, mode) _mkdir(path)
#define stat _stat
#define S_ISDIR(m) (((m) & _S_IFDIR) == _S_IFDIR)
#else
#include "airy_dirent.h"

#include <sys/resource.h>
#include <unistd.h>
#endif

#define heapstore_MAX_SERVICE_LOGS 32

static bool s_initialized = false;
static char s_root_path[heapstore_MAX_PATH_LEN];
static heapstore_config_t s_config;
static airy_mtx_t s_config_lock;

const heapstore_path_type_t heapstore_core_path_order[heapstore_CORE_PATH_COUNT] = {
    heapstore_PATH_KERNEL,       heapstore_PATH_LOGS,   heapstore_PATH_REGISTRY,
    heapstore_PATH_SERVICES,     heapstore_PATH_TRACES, heapstore_PATH_KERNEL_IPC,
    heapstore_PATH_KERNEL_MEMORY};

const char *heapstore_core_path_names[heapstore_CORE_PATH_COUNT] = {
    "kernel", "logs", "registry", "services", "traces", "kernel/ipc", "kernel/memory"};

const char *heapstore_core_subpath_map[heapstore_CORE_PATH_COUNT][heapstore_MAX_SUBPATHS] = {
    {NULL},
    {"apps", "kernel", "services", NULL},
    {NULL},
    {"llm_d", "market_d", "tool_d", NULL},
    {"spans", NULL},
    {"channels", "buffers", NULL},
    {"pools", "allocations", "stats", "index", "meta", "patterns", "raw", NULL}};

static const char *s_default_root = NULL;

static const char *_get_default_root(void)
{
    if (s_default_root)
        return s_default_root;
    const char *env = getenv("AIRY_HEAPSTORE_ROOT");
    if (env && env[0]) {
        s_default_root = env;
        return s_default_root;
    }
    static char fallback[512];
    const char *data_dir = airy_data_dir();
    if (data_dir && data_dir[0]) {
        snprintf(fallback, sizeof(fallback), "%s/agentrt/heapstore", data_dir);
    } else {
        snprintf(fallback, sizeof(fallback), "/tmp/agentrt/heapstore");
    }
    s_default_root = fallback;
    return s_default_root;
}

static void set_default_config(void)
{
    AIRY_MEMSET(&s_config, 0, sizeof(s_config));
    s_config.root_path = _get_default_root();
    s_config.max_log_size_mb = 100;
    s_config.log_retention_days = 7;
    s_config.trace_retention_days = 3;
    s_config.enable_auto_cleanup = true;
    s_config.enable_log_rotation = true;
    s_config.enable_trace_export = true;
    s_config.db_vacuum_interval_days = 7;
    s_config.circuit_breaker_threshold = heapstore_DEFAULT_CIRCUIT_THRESHOLD;
    s_config.circuit_breaker_timeout_sec = heapstore_DEFAULT_CIRCUIT_TIMEOUT_SEC;
}

/**
  * @brief Apply user configuration parameters
 */
static void apply_user_config(const heapstore_config_t *manager)
{
    if (!manager || !manager->root_path) {
        return;
    }

    s_config.root_path = manager->root_path;

    if (manager->max_log_size_mb > 0) {
        s_config.max_log_size_mb = manager->max_log_size_mb;
    }
    if (manager->log_retention_days > 0) {
        s_config.log_retention_days = manager->log_retention_days;
    }
    if (manager->trace_retention_days > 0) {
        s_config.trace_retention_days = manager->trace_retention_days;
    }
    if (manager->db_vacuum_interval_days > 0) {
        s_config.db_vacuum_interval_days = manager->db_vacuum_interval_days;
    }
    if (manager->circuit_breaker_threshold > 0) {
        s_config.circuit_breaker_threshold = manager->circuit_breaker_threshold;
    }
    if (manager->circuit_breaker_timeout_sec > 0) {
        s_config.circuit_breaker_timeout_sec = manager->circuit_breaker_timeout_sec;
    }

    s_config.enable_auto_cleanup = manager->enable_auto_cleanup;
    s_config.enable_log_rotation = manager->enable_log_rotation;
    s_config.enable_trace_export = manager->enable_trace_export;

    heapstore_core_circuit_apply_config(s_config.circuit_breaker_threshold,
                                        s_config.circuit_breaker_timeout_sec);
}

/**
  * @brief Create the directory structure
 */
static heapstore_error_t create_directory_structure(void)
{
    if (!heapstore_dir_ensure(s_root_path)) {
        return heapstore_ERR_DIR_CREATE_FAILED;
    }

    for (size_t i = 0; i < heapstore_CORE_PATH_COUNT; i++) {
        char full_path[heapstore_MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", s_root_path,
                 heapstore_core_path_names[i]);

        if (!heapstore_dir_ensure(full_path)) {
            return heapstore_ERR_DIR_CREATE_FAILED;
        }

        size_t subpath_idx = (size_t)heapstore_core_path_order[i];
        if (subpath_idx < heapstore_CORE_PATH_COUNT) {
            for (size_t j = 0; heapstore_core_subpath_map[subpath_idx][j] != NULL; j++) {
                char sub_path[heapstore_MAX_PATH_LEN];
                snprintf(sub_path, sizeof(sub_path), "%s/%s", full_path,
                         heapstore_core_subpath_map[subpath_idx][j]);
                if (!heapstore_dir_ensure(sub_path)) {
                    return heapstore_ERR_DIR_CREATE_FAILED;
                }
            }
        }
    }

    return heapstore_SUCCESS;
}

/**
  * @brief Initialize atomic variables
 */
static void initialize_atomic_vars(void)
{
    heapstore_core_circuit_init();
    heapstore_core_metrics_init();
}

/**
  * @brief Initialize subsystems, rolling back on failure
 */
typedef heapstore_error_t (*subsystem_init_func)(void);
typedef void (*subsystem_shutdown_func)(void);

static heapstore_error_t __attribute__((unused)) init_subsys_rollback(
    subsystem_init_func init, subsystem_shutdown_func shutdown, const char *name)
{

    heapstore_error_t err = init();
    if (err != heapstore_SUCCESS) {
        AIRY_LOG_ERROR("heapstore: failed to initialize %s: %s", name, heapstore_strerror(err));
        return err;
    }
    return heapstore_SUCCESS;
}

#define INIT_SUBSYSTEM(init_func, shutdown_func, name)                                    \
    do {                                                                                  \
        heapstore_error_t err =                                                           \
            init_subsys_rollback((subsystem_init_func)(init_func),                \
                                         (subsystem_shutdown_func)(shutdown_func), name); \
        if (err != heapstore_SUCCESS) {                                                   \
            return err;                                                                   \
        }                                                                                 \
    } while (0)

#define ROLLBACK_AND_RETURN(init_func, shutdown_func, name)                               \
    do {                                                                                  \
        heapstore_error_t err =                                                           \
            init_subsys_rollback((subsystem_init_func)(init_func),                \
                                         (subsystem_shutdown_func)(shutdown_func), name); \
        if (err != heapstore_SUCCESS) {                                                   \
            shutdown_func();                                                              \
            s_initialized = false;                                                        \
            return err;                                                                   \
        }                                                                                 \
    } while (0)

heapstore_error_t heapstore_init(const heapstore_config_t *manager)
{
    if (s_initialized) {
        AIRY_LOG_WARN("heapstore_init: already initialized");
        return heapstore_ERR_ALREADY_INITIALIZED;
    }

    AIRY_LOG_INFO("heapstore_init: initializing (root=%s)",
                  manager && manager->root_path ? manager->root_path : "default");

    airy_mtx_init(&s_config_lock);
    set_default_config();
    apply_user_config(manager);

    AIRY_STRNCPY_TERM(s_root_path, s_config.root_path, sizeof(s_root_path));

    heapstore_error_t err = create_directory_structure();
    if (err != heapstore_SUCCESS) {
        AIRY_LOG_ERROR("heapstore_init: directory structure creation failed, err=%d", err);
        return err;
    }
    AIRY_LOG_INFO("heapstore_init: [OK] directory structure created");

    initialize_atomic_vars();

    s_initialized = true;

    bool needs_migration = false;
    uint32_t disk_version = 0;
    heapstore_error_t mig_err = heapstore_migration_check(&needs_migration, &disk_version);
    if (mig_err == heapstore_SUCCESS && needs_migration) {
        AIRY_LOG_INFO(
            "heapstore_init: schema migration: disk=v%u, code=v%u, running forward migration",
            disk_version, HEAPSTORE_SCHEMA_VERSION_CURRENT);

        heapstore_migration_report_t report;
        mig_err = heapstore_migration_forward(0, &report);
        if (mig_err != heapstore_SUCCESS) {
            AIRY_LOG_ERROR("heapstore_init: migration FAILED: %s, data preserved at v%u",
                           heapstore_strerror(mig_err), disk_version);
            heapstore_migration_report_free(&report);
            s_initialized = false;
            return heapstore_ERR_INTERNAL;
        }
        AIRY_LOG_INFO("heapstore_init: migration complete: v%u->v%u (%lu steps, %lums)",
                      report.from_version, report.to_version, (unsigned long)report.step_count,
                      (unsigned long)report.total_duration_ms);
        heapstore_migration_report_free(&report);
    }

    err = heapstore_registry_init();
    if (err != heapstore_SUCCESS) {
        AIRY_LOG_ERROR("heapstore_init: registry init failed, err=%d", err);
        s_initialized = false;
        return err;
    }
    AIRY_LOG_INFO("heapstore_init: [OK] registry initialized");

    err = heapstore_trace_init();
    if (err != heapstore_SUCCESS) {
        AIRY_LOG_ERROR("heapstore_init: trace init failed, err=%d", err);
        heapstore_registry_shutdown();
        s_initialized = false;
        return err;
    }
    AIRY_LOG_INFO("heapstore_init: [OK] trace initialized");

    err = heapstore_ipc_init();
    if (err != heapstore_SUCCESS) {
        AIRY_LOG_ERROR("heapstore_init: ipc init failed, err=%d", err);
        heapstore_trace_shutdown();
        heapstore_registry_shutdown();
        s_initialized = false;
        return err;
    }
    AIRY_LOG_INFO("heapstore_init: [OK] ipc initialized");

    err = heapstore_memory_init();
    if (err != heapstore_SUCCESS) {
        AIRY_LOG_ERROR("heapstore_init: memory init failed, err=%d", err);
        heapstore_ipc_shutdown();
        heapstore_trace_shutdown();
        heapstore_registry_shutdown();
        s_initialized = false;
        return err;
    }
    AIRY_LOG_INFO("heapstore_init: [OK] memory initialized");

    err = heapstore_log_init();
    if (err != heapstore_SUCCESS) {
        AIRY_LOG_ERROR("heapstore_init: log init failed, err=%d", err);
        heapstore_memory_shutdown();
        heapstore_ipc_shutdown();
        heapstore_trace_shutdown();
        heapstore_registry_shutdown();
        s_initialized = false;
        return err;
    }
    AIRY_LOG_INFO("heapstore_init: [OK] log initialized");

    AIRY_LOG_INFO("heapstore_init: heapstore initialized successfully (root=%s)", s_root_path);
    return heapstore_SUCCESS;
}

void heapstore_shutdown(void)
{
    if (s_initialized) {
        AIRY_LOG_INFO("heapstore_shutdown: shutting down heapstore...");
        heapstore_log_shutdown();
        AIRY_LOG_INFO("heapstore_shutdown: [OK] log shutdown");
        heapstore_trace_shutdown();
        AIRY_LOG_INFO("heapstore_shutdown: [OK] trace shutdown");
        heapstore_ipc_shutdown();
        AIRY_LOG_INFO("heapstore_shutdown: [OK] ipc shutdown");
        heapstore_memory_shutdown();
        AIRY_LOG_INFO("heapstore_shutdown: [OK] memory shutdown");
        heapstore_registry_shutdown();
        AIRY_LOG_INFO("heapstore_shutdown: [OK] registry shutdown");
        s_initialized = false;
        AIRY_LOG_INFO("heapstore_shutdown: heapstore shutdown complete");
    }
}

bool heapstore_ready(void)
{
    return s_initialized;
}

const char *heapstore_get_root(void)
{
    if (s_root_path[0] == '\0') {
        /* 未显式 heapstore_init 时（如 trace/ipc 子模块独立初始化），
         * 惰性解析默认 root（AIRY_HEAPSTORE_ROOT 或 $AIRY_HOME/data），
         * 避免落到空串导致的 "/" 根目录写入。 */
        const char *root = _get_default_root();
        if (root && root[0])
            AIRY_STRNCPY_TERM(s_root_path, root, sizeof(s_root_path));
    }
    return s_root_path;
}

/**
  * @brief Snapshot the retention/cleanup settings under the config lock.
 */
void heapstore_core_get_cleanup_config(bool *enable_auto_cleanup, uint32_t *log_retention_days,
                                       uint32_t *trace_retention_days)
{
    airy_mtx_lock(&s_config_lock);
    if (enable_auto_cleanup) {
        *enable_auto_cleanup = s_config.enable_auto_cleanup;
    }
    if (log_retention_days) {
        *log_retention_days = s_config.log_retention_days;
    }
    if (trace_retention_days) {
        *trace_retention_days = s_config.trace_retention_days;
    }
    airy_mtx_unlock(&s_config_lock);
}

/**
  * @brief Update configuration parameters
 */
static void apply_config_update(const heapstore_config_t *manager)
{
    airy_mtx_lock(&s_config_lock);
    if (manager->max_log_size_mb > 0)
        s_config.max_log_size_mb = manager->max_log_size_mb;
    if (manager->log_retention_days > 0)
        s_config.log_retention_days = manager->log_retention_days;
    if (manager->trace_retention_days > 0)
        s_config.trace_retention_days = manager->trace_retention_days;
    if (manager->db_vacuum_interval_days > 0)
        s_config.db_vacuum_interval_days = manager->db_vacuum_interval_days;

    s_config.enable_auto_cleanup = manager->enable_auto_cleanup;
    s_config.enable_log_rotation = manager->enable_log_rotation;
    s_config.enable_trace_export = manager->enable_trace_export;

    if (manager->circuit_breaker_threshold > 0) {
        s_config.circuit_breaker_threshold = manager->circuit_breaker_threshold;
    }
    if (manager->circuit_breaker_timeout_sec > 0) {
        s_config.circuit_breaker_timeout_sec = manager->circuit_breaker_timeout_sec;
    }
    heapstore_core_circuit_apply_config(s_config.circuit_breaker_threshold,
                                        s_config.circuit_breaker_timeout_sec);
    airy_mtx_unlock(&s_config_lock);
}

heapstore_error_t heapstore_reload_config(const heapstore_config_t *manager)
{
    if (!s_initialized) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    if (!manager) {
        return heapstore_ERR_INVALID_PARAM;
    }

    apply_config_update(manager);

    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_flush(void)
{
    if (!s_initialized) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    heapstore_error_t err = heapstore_trace_flush();
    if (err != heapstore_SUCCESS) {
        return err;
    }

    return heapstore_SUCCESS;
}

/**
  * @brief Check a single subsystem's health
 */
static bool check_subsystem_health(const char *name, bool (*check_func)(void))
{
    bool healthy = check_func();
    if (!healthy) {
        heapstore_log_write_fast("health", HEAPSTORE_LOG_WARN, name);
    }
    return healthy;
}

/**
  * @brief Update output params and return the health state
 */
static void update_health_status(bool *output, bool healthy, bool *all_healthy)
{
    if (output) {
        *output = healthy;
    }
    if (!healthy && all_healthy) {
        *all_healthy = false;
    }
}

heapstore_error_t heapstore_health_check(bool *registry_ok, bool *trace_ok, bool *log_ok,
                                         bool *ipc_ok, bool *memory_ok)
{
    if (!s_initialized) {
        update_health_status(registry_ok, false, NULL);
        update_health_status(trace_ok, false, NULL);
        update_health_status(log_ok, false, NULL);
        update_health_status(ipc_ok, false, NULL);
        update_health_status(memory_ok, false, NULL);
        return heapstore_ERR_NOT_INITIALIZED;
    }

    bool all_healthy = true;

    update_health_status(registry_ok,
                         check_subsystem_health("registry", heapstore_registry_is_healthy),
                         &all_healthy);

    update_health_status(trace_ok, check_subsystem_health("trace", heapstore_trace_is_healthy),
                         &all_healthy);

    update_health_status(log_ok, check_subsystem_health("log", heapstore_log_is_healthy),
                         &all_healthy);

    update_health_status(ipc_ok, check_subsystem_health("ipc", heapstore_ipc_is_healthy),
                         &all_healthy);

    update_health_status(memory_ok, check_subsystem_health("memory", heapstore_memory_is_healthy),
                         &all_healthy);

    if (heapstore_circuit_open()) {
        all_healthy = false;
    }

    return all_healthy ? heapstore_SUCCESS : heapstore_ERR_INTERNAL;
}
