// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_registry_sqlite_agent.c
 * @brief Registry SQLite backend agent domain: CRUD, query and batch insert.
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

heapstore_error_t heapstore_registry_add_agent(const heapstore_agent_record_t *record)
{
    if (!record || !record->id[0]) {
        return heapstore_ERR_INVALID_PARAM;
    }

    const char *sql = "INSERT INTO agents "
                      "(id, name, type, version, status, config_path, created_at, updated_at, "
                      " priority, tags) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    return execute_sql_with_lock(sql, bind_agent_record, (void *)record);
}

heapstore_error_t heapstore_registry_get_agent(const char *id, heapstore_agent_record_t *record)
{
    if (!id || !record) {
        return heapstore_ERR_INVALID_PARAM;
    }

    if (!s_registry.initialized || !s_registry.db) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    airy_mtx_lock(&s_registry.lock);

    const char *sql = "SELECT id, name, type, version, status, config_path, created_at, "
                      "updated_at, priority, tags "
                      "FROM agents WHERE id = ?;";
    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare_v2(s_registry.db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        airy_mtx_unlock(&s_registry.lock);
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const char *text;
        text = (const char *)sqlite3_column_text(stmt, 0);
        if (text) {
            AIRY_STRNCPY_TERM(record->id, text, sizeof(record->id));
        } else
            record->id[0] = '\0';

        text = (const char *)sqlite3_column_text(stmt, 1);
        if (text) {
            AIRY_STRNCPY_TERM(record->name, text, sizeof(record->name));
        } else
            record->name[0] = '\0';

        text = (const char *)sqlite3_column_text(stmt, 2);
        if (text) {
            AIRY_STRNCPY_TERM(record->type, text, sizeof(record->type));
        } else
            record->type[0] = '\0';

        text = (const char *)sqlite3_column_text(stmt, 3);
        if (text) {
            AIRY_STRNCPY_TERM(record->version, text, sizeof(record->version));
        } else
            record->version[0] = '\0';

        text = (const char *)sqlite3_column_text(stmt, 4);
        if (text) {
            AIRY_STRNCPY_TERM(record->status, text, sizeof(record->status));
        } else
            record->status[0] = '\0';

        text = (const char *)sqlite3_column_text(stmt, 5);
        if (text) {
            AIRY_STRNCPY_TERM(record->config_path, text, sizeof(record->config_path));
        } else
            record->config_path[0] = '\0';

        record->created_at = sqlite3_column_int64(stmt, 6);
        record->updated_at = sqlite3_column_int64(stmt, 7);
        record->priority = sqlite3_column_int(stmt, 8);
        text = (const char *)sqlite3_column_text(stmt, 9);
        if (text) {
            AIRY_STRNCPY_TERM(record->tags, text, sizeof(record->tags));
        } else
            record->tags[0] = '\0';

        sqlite3_finalize(stmt);
        airy_mtx_unlock(&s_registry.lock);
        return heapstore_SUCCESS;
    }

    sqlite3_finalize(stmt);
    airy_mtx_unlock(&s_registry.lock);
    return heapstore_ERR_NOT_FOUND;
}

heapstore_error_t heapstore_registry_update_agent(const heapstore_agent_record_t *record)
{
    if (!record || !record->id[0]) {
        return heapstore_ERR_INVALID_PARAM;
    }

    if (!s_registry.initialized || !s_registry.db) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    airy_mtx_lock(&s_registry.lock);

    const char *sql =
        "UPDATE agents SET "
        "name = ?, type = ?, version = ?, status = ?, config_path = ?, updated_at = ?, "
        "priority = ?, tags = ? "
        "WHERE id = ?;";
    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare_v2(s_registry.db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        airy_mtx_unlock(&s_registry.lock);
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    sqlite3_bind_text(stmt, 1, record->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, record->type, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, record->version, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, record->status, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, record->config_path, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 6, record->updated_at);
    sqlite3_bind_int(stmt, 7, record->priority);
    sqlite3_bind_text(stmt, 8, record->tags, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, record->id, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    airy_mtx_unlock(&s_registry.lock);

    if (rc != SQLITE_DONE) {
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_delete_agent(const char *id)
{
    if (!id) {
        return heapstore_ERR_INVALID_PARAM;
    }

    if (!s_registry.initialized || !s_registry.db) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    airy_mtx_lock(&s_registry.lock);

    const char *sql = "DELETE FROM agents WHERE id = ?;";
    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare_v2(s_registry.db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        airy_mtx_unlock(&s_registry.lock);
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    airy_mtx_unlock(&s_registry.lock);

    if (rc != SQLITE_DONE) {
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_query_agents(const char *filter_type,
                                                  const char *filter_status,
                                                  heapstore_registry_iter_t **iter)
{
    if (!iter)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized || !s_registry.db)
        return heapstore_ERR_NOT_INITIALIZED;

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT id, name, type, version, status, config_path, created_at, updated_at FROM "
             "agents WHERE 1=1");
    if (filter_type) {
        size_t pos = strlen(sql);
        snprintf(sql + pos, sizeof(sql) - pos, " AND type = ?");
    }
    if (filter_status) {
        size_t pos = strlen(sql);
        snprintf(sql + pos, sizeof(sql) - pos, " AND status = ?");
    }

    airy_mtx_lock(&s_registry.lock);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(s_registry.db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        airy_mtx_unlock(&s_registry.lock);
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    int param_idx = 1;
    if (filter_type) {
        sqlite3_bind_text(stmt, param_idx++, filter_type, -1, SQLITE_STATIC);
    }
    if (filter_status) {
        sqlite3_bind_text(stmt, param_idx++, filter_status, -1, SQLITE_STATIC);
    }

    heapstore_registry_iter_t *new_iter =
        (heapstore_registry_iter_t *)AIRY_CALLOC(1, sizeof(heapstore_registry_iter_t));
    if (!new_iter) {
        sqlite3_finalize(stmt);
        airy_mtx_unlock(&s_registry.lock);
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    new_iter->stmt = stmt;
    new_iter->current_type = 0;
    new_iter->has_more = 1;

    *iter = new_iter;
    airy_mtx_unlock(&s_registry.lock);
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_batch_insert_agents(const heapstore_agent_record_t *records,
                                                         size_t count)
{
    if (!records || count == 0) {
        return heapstore_ERR_INVALID_PARAM;
    }

    if (!s_registry.initialized || !s_registry.db) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    const char *sql = "INSERT INTO agents "
                      "(id, name, type, version, status, config_path, created_at, updated_at, "
                      " priority, tags) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    airy_mtx_lock(&s_registry.lock);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_registry.db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        airy_mtx_unlock(&s_registry.lock);
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    rc = sqlite3_exec(s_registry.db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        airy_mtx_unlock(&s_registry.lock);
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    heapstore_error_t result = heapstore_SUCCESS;
    for (size_t i = 0; i < count; i++) {
        const heapstore_agent_record_t *record = &records[i];

        sqlite3_bind_text(stmt, 1, record->id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, record->name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, record->type, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, record->version, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, record->status, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, record->config_path, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 7, record->created_at);
        sqlite3_bind_int64(stmt, 8, record->updated_at);
        sqlite3_bind_int(stmt, 9, record->priority);
        sqlite3_bind_text(stmt, 10, record->tags, -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            result = heapstore_ERR_DB_QUERY_FAILED;
            sqlite3_exec(s_registry.db, "ROLLBACK;", NULL, NULL, NULL);
            break;
        }

        sqlite3_reset(stmt);
    }

    if (result == heapstore_SUCCESS) {
        rc = sqlite3_exec(s_registry.db, "COMMIT;", NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            result = heapstore_ERR_DB_QUERY_FAILED;
        }
    }

    sqlite3_finalize(stmt);
    airy_mtx_unlock(&s_registry.lock);

    return result;
}

#endif /* heapstore_SQLITE_IMPLEMENTATION */
