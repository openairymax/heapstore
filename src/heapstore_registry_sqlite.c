// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_registry_sqlite.c
 * @brief Registry SQLite backend core: DB init, connection management and generic SQL.
 */

// @owner: team-B
#include "heapstore_registry.h"

#include "../include/utils.h"
#include "platform.h"
#include "private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "airy_memory.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "heapstore_registry_internal.h"

#ifdef heapstore_SQLITE_IMPLEMENTATION

registry_db_t s_registry = {0};

static heapstore_error_t init_database(sqlite3 *db)
{
    const char *sql = "CREATE TABLE IF NOT EXISTS agents ("
                      "    id TEXT PRIMARY KEY,"
                      "    name TEXT NOT NULL,"
                      "    type TEXT,"
                      "    version TEXT,"
                      "    status TEXT,"
                      "    config_path TEXT,"
                      "    created_at INTEGER,"
                      "    updated_at INTEGER,"
                      "    priority INTEGER DEFAULT 0,"
                      "    tags TEXT DEFAULT ''"
                      ");"
                      "CREATE TABLE IF NOT EXISTS agent_capabilities ("
                      "    agent_id TEXT,"
                      "    capability TEXT,"
                      "    FOREIGN KEY (agent_id) REFERENCES agents(id)"
                      ");"
                      "CREATE TABLE IF NOT EXISTS skills ("
                      "    id TEXT PRIMARY KEY,"
                      "    name TEXT NOT NULL,"
                      "    version TEXT,"
                      "    library_path TEXT,"
                      "    manifest_path TEXT,"
                      "    installed_at INTEGER"
                      ");"
                      "CREATE TABLE IF NOT EXISTS sessions ("
                      "    id TEXT PRIMARY KEY,"
                      "    user_id TEXT,"
                      "    created_at INTEGER,"
                      "    last_active_at INTEGER,"
                      "    ttl_seconds INTEGER,"
                      "    status TEXT,"
                      "    metadata TEXT DEFAULT ''"
                      ");"
                      "CREATE INDEX IF NOT EXISTS idx_agent_type ON agents(type);"
                      "CREATE INDEX IF NOT EXISTS idx_skill_status ON skills(name);"
                      "CREATE INDEX IF NOT EXISTS idx_session_user ON sessions(user_id);";

    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) {
            sqlite3_free(err_msg);
        }
        return heapstore_ERR_DB_INIT_FAILED;
    }
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_init(void)
{
    if (s_registry.initialized) {
        return heapstore_SUCCESS;
    }

    const char *configured_root = heapstore_get_root();
    char root_path[256];
    if (configured_root && configured_root[0] != '\0') {
        AIRY_STRNCPY_TERM(root_path, configured_root, sizeof(root_path));
    } else {
        const char *tmpdir = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
        snprintf(root_path, sizeof(root_path), "%s/agentrt/heapstore", tmpdir);
    }

    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/registry", root_path);

    heapstore_ensure_directory(full_path);

    snprintf(s_registry.db_path, sizeof(s_registry.db_path), "%s/registry.db", full_path);

    int rc = sqlite3_open(s_registry.db_path, &s_registry.db);
    if (rc != SQLITE_OK) {
        return heapstore_ERR_DB_INIT_FAILED;
    }

    sqlite3_exec(s_registry.db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(s_registry.db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    sqlite3_exec(s_registry.db, "PRAGMA cache_size=10000;", NULL, NULL, NULL);
    sqlite3_exec(s_registry.db, "PRAGMA temp_store=MEMORY;", NULL, NULL, NULL);

    heapstore_error_t err = init_database(s_registry.db);
    if (err != heapstore_SUCCESS) {
        sqlite3_close(s_registry.db);
        __builtin_memset(&s_registry, 0, sizeof(s_registry));
        return err;
    }

    airy_mtx_init(&s_registry.lock);
    s_registry.initialized = true;

    return heapstore_SUCCESS;
}

void heapstore_registry_shutdown(void)
{
    if (!s_registry.initialized) {
        return;
    }

    airy_mtx_lock(&s_registry.lock);

    if (s_registry.db) {
        sqlite3_close(s_registry.db);
        s_registry.db = NULL;
    }

    s_registry.initialized = false;
    airy_mtx_unlock(&s_registry.lock);
    airy_mtx_destroy(&s_registry.lock);
}

heapstore_error_t execute_sql_with_lock(
    const char *sql, heapstore_error_t (*bind_func)(sqlite3_stmt *, void *), void *bind_data)
{
    if (!s_registry.initialized || !s_registry.db) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    airy_mtx_lock(&s_registry.lock);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_registry.db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        airy_mtx_unlock(&s_registry.lock);
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    if (bind_func) {
        heapstore_error_t err = bind_func(stmt, bind_data);
        if (err != heapstore_SUCCESS) {
            sqlite3_finalize(stmt);
            airy_mtx_unlock(&s_registry.lock);
            return err;
        }
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    airy_mtx_unlock(&s_registry.lock);

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    return heapstore_SUCCESS;
}

heapstore_error_t bind_agent_record(sqlite3_stmt *stmt, void *data)
{
    const heapstore_agent_record_t *record = (const heapstore_agent_record_t *)data;

    sqlite3_bind_text(stmt, 1, record->id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, record->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, record->type, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, record->version, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, record->status, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, record->config_path, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 7, record->created_at);
    sqlite3_bind_int64(stmt, 8, record->updated_at);

    return heapstore_SUCCESS;
}

static heapstore_error_t __attribute__((unused)) bind_agent_id(sqlite3_stmt *stmt, void *data)
{
    const char *agent_id = (const char *)data;
    sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_STATIC);
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_vacuum(void)
{
    if (!s_registry.initialized || !s_registry.db) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    airy_mtx_lock(&s_registry.lock);
    sqlite3_exec(s_registry.db, "VACUUM;", NULL, NULL, NULL);
    airy_mtx_unlock(&s_registry.lock);

    return heapstore_SUCCESS;
}

bool heapstore_registry_is_healthy(void)
{
    return s_registry.initialized && s_registry.db != NULL;
}

#endif /* heapstore_SQLITE_IMPLEMENTATION */
