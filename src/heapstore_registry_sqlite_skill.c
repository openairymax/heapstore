// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_registry_sqlite_skill.c
 * @brief Registry SQLite backend skill domain: CRUD, query and batch insert.
 */

// @owner: team-B
#include "heapstore_registry_internal.h"

#ifdef heapstore_SQLITE_IMPLEMENTATION

static heapstore_error_t bind_skill_row(sqlite3_stmt *stmt, void *data)
{
    const heapstore_skill_record_t *record = (const heapstore_skill_record_t *)data;

    sqlite3_bind_text(stmt, 1, record->id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, record->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, record->version, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, record->library_path, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, record->manifest_path, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 6, record->installed_at);
    return heapstore_SUCCESS;
}

void extract_skill_row(sqlite3_stmt *stmt, void *rec)
{
    heapstore_skill_record_t *record = (heapstore_skill_record_t *)rec;
    const char *text;

    __builtin_memset(record, 0, sizeof(*record));

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
}

heapstore_error_t heapstore_registry_add_skill(const heapstore_skill_record_t *record)
{
    if (!record || !record->id[0]) {
        return heapstore_ERR_INVALID_PARAM;
    }

    return sql_exec_locked("INSERT INTO skills "
                           "(id, name, version, library_path, manifest_path, installed_at) "
                           "VALUES (?, ?, ?, ?, ?, ?);",
                           bind_skill_row, (void *)record);
}

heapstore_error_t heapstore_registry_get_skill(const char *id, heapstore_skill_record_t *record)
{
    if (!id || !record) {
        return heapstore_ERR_INVALID_PARAM;
    }

    return registry_query_one("SELECT id, name, version, library_path, manifest_path, "
                              "installed_at FROM skills WHERE id = ?;",
                              registry_bind_id, (void *)id, extract_skill_row, record);
}

heapstore_error_t heapstore_registry_delete_skill(const char *id)
{
    if (!id) {
        return heapstore_ERR_INVALID_PARAM;
    }

    return sql_exec_locked("DELETE FROM skills WHERE id = ?;", registry_bind_id, (void *)id);
}

heapstore_error_t heapstore_registry_query_skills(heapstore_registry_iter_t **iter)
{
    if (!iter) {
        return heapstore_ERR_INVALID_PARAM;
    }
    if (!s_registry.initialized || !s_registry.db) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    return registry_query_open("SELECT id, name, version, library_path, manifest_path, "
                               "installed_at FROM skills ORDER BY installed_at DESC;",
                               heapstore_REGISTRY_REC_SKILL, NULL, NULL, iter);
}

heapstore_error_t heapstore_registry_batch_insert_skills(const heapstore_skill_record_t *records,
                                                         size_t count)
{
    if (!records || count == 0) {
        return heapstore_ERR_INVALID_PARAM;
    }

    return batch_exec_locked("INSERT INTO skills "
                                 "(id, name, version, library_path, manifest_path, "
                                 " installed_at) "
                                 "VALUES (?, ?, ?, ?, ?, ?);",
                                 bind_skill_row, records, count,
                                 sizeof(heapstore_skill_record_t));
}

#endif /* heapstore_SQLITE_IMPLEMENTATION */
