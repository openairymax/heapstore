// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_registry_sqlite_skill.c
 * @brief Registry SQLite backend skill domain: CRUD, query and batch insert.
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

heapstore_error_t heapstore_registry_add_skill(const heapstore_skill_record_t *record)
{
    if (!record || !record->id[0]) {
        return heapstore_ERR_INVALID_PARAM;
    }

    if (!s_registry.initialized || !s_registry.db) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    airy_mtx_lock(&s_registry.lock);

    const char *sql = "INSERT INTO skills "
                      "(id, name, version, library_path, manifest_path, installed_at) "
                      "VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare_v2(s_registry.db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        airy_mtx_unlock(&s_registry.lock);
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    sqlite3_bind_text(stmt, 1, record->id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, record->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, record->version, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, record->library_path, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, record->manifest_path, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 6, record->installed_at);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    airy_mtx_unlock(&s_registry.lock);

    if (rc != SQLITE_DONE) {
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_get_skill(const char *id, heapstore_skill_record_t *record)
{
    if (!id || !record) {
        return heapstore_ERR_INVALID_PARAM;
    }

    if (!s_registry.initialized || !s_registry.db) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    airy_mtx_lock(&s_registry.lock);

    const char *sql = "SELECT id, name, version, library_path, manifest_path, installed_at FROM "
                      "skills WHERE id = ?;";
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
        }
        text = (const char *)sqlite3_column_text(stmt, 1);
        if (text) {
            AIRY_STRNCPY_TERM(record->name, text, sizeof(record->name));
        }
        text = (const char *)sqlite3_column_text(stmt, 2);
        if (text) {
            AIRY_STRNCPY_TERM(record->version, text, sizeof(record->version));
        }
        text = (const char *)sqlite3_column_text(stmt, 3);
        if (text) {
            AIRY_STRNCPY_TERM(record->library_path, text, sizeof(record->library_path));
        }
        text = (const char *)sqlite3_column_text(stmt, 4);
        if (text) {
            AIRY_STRNCPY_TERM(record->manifest_path, text, sizeof(record->manifest_path));
        }
        record->installed_at = sqlite3_column_int64(stmt, 5);
        sqlite3_finalize(stmt);
        airy_mtx_unlock(&s_registry.lock);
        return heapstore_SUCCESS;
    }

    sqlite3_finalize(stmt);
    airy_mtx_unlock(&s_registry.lock);
    return heapstore_ERR_NOT_FOUND;
}

heapstore_error_t heapstore_registry_delete_skill(const char *id)
{
    if (!id) {
        return heapstore_ERR_INVALID_PARAM;
    }

    if (!s_registry.initialized || !s_registry.db) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    airy_mtx_lock(&s_registry.lock);

    const char *sql = "DELETE FROM skills WHERE id = ?;";
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

heapstore_error_t heapstore_registry_query_skills(heapstore_registry_iter_t **iter)
{
    if (!iter) {
        return heapstore_ERR_INVALID_PARAM;
    }

    if (!s_registry.initialized || !s_registry.db) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    airy_mtx_lock(&s_registry.lock);

    const char *sql = "SELECT id, name, version, library_path, manifest_path, installed_at FROM "
                      "skills ORDER BY installed_at DESC;";
    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare_v2(s_registry.db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        airy_mtx_unlock(&s_registry.lock);
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    heapstore_registry_iter_t *new_iter =
        (heapstore_registry_iter_t *)AIRY_MALLOC(sizeof(heapstore_registry_iter_t));
    if (!new_iter) {
        sqlite3_finalize(stmt);
        airy_mtx_unlock(&s_registry.lock);
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    new_iter->stmt = stmt;
    new_iter->current_type = 1; /* skills */
    new_iter->has_more = 1;

    *iter = new_iter;
    airy_mtx_unlock(&s_registry.lock);

    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_batch_insert_skills(const heapstore_skill_record_t *records,
                                                         size_t count)
{
    if (!records || count == 0) {
        return heapstore_ERR_INVALID_PARAM;
    }

    if (!s_registry.initialized || !s_registry.db) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    const char *sql = "INSERT INTO skills "
                      "(id, name, version, library_path, manifest_path, installed_at) "
                      "VALUES (?, ?, ?, ?, ?, ?);";

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
        const heapstore_skill_record_t *record = &records[i];

        sqlite3_bind_text(stmt, 1, record->id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, record->name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, record->version, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, record->library_path, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, record->manifest_path, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 6, record->installed_at);

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
