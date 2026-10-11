// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_registry_sqlite_session.c
 * @brief Registry SQLite backend session domain: CRUD, query and batch insert.
 */

// @owner: team-B
#include "heapstore_registry_internal.h"

#ifdef heapstore_SQLITE_IMPLEMENTATION

static heapstore_error_t bind_session_row(sqlite3_stmt *stmt, void *data)
{
    const heapstore_session_record_t *record = (const heapstore_session_record_t *)data;

    sqlite3_bind_text(stmt, 1, record->id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, record->user_id, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, record->created_at);
    sqlite3_bind_int64(stmt, 4, record->last_active_at);
    sqlite3_bind_int(stmt, 5, record->ttl_seconds);
    sqlite3_bind_text(stmt, 6, record->status, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, record->metadata, -1, SQLITE_STATIC);
    return heapstore_SUCCESS;
}

static heapstore_error_t bind_session_update(sqlite3_stmt *stmt, void *data)
{
    const heapstore_session_record_t *record = (const heapstore_session_record_t *)data;

    sqlite3_bind_text(stmt, 1, record->user_id, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, record->last_active_at);
    sqlite3_bind_int(stmt, 3, record->ttl_seconds);
    sqlite3_bind_text(stmt, 4, record->status, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, record->metadata, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, record->id, -1, SQLITE_STATIC);
    return heapstore_SUCCESS;
}

void extract_session_row(sqlite3_stmt *stmt, void *rec)
{
    heapstore_session_record_t *record = (heapstore_session_record_t *)rec;

    __builtin_memset(record, 0, sizeof(*record));

    copy_text_col(stmt, 0, record->id, sizeof(record->id));
    copy_text_col(stmt, 1, record->user_id, sizeof(record->user_id));
    record->created_at = sqlite3_column_int64(stmt, 2);
    record->last_active_at = sqlite3_column_int64(stmt, 3);
    record->ttl_seconds = sqlite3_column_int(stmt, 4);
    copy_text_col(stmt, 5, record->status, sizeof(record->status));
    copy_text_col(stmt, 6, record->metadata, sizeof(record->metadata));
}

heapstore_error_t heapstore_registry_add_session(const heapstore_session_record_t *record)
{
    if (!record || !record->id[0]) {
        return heapstore_ERR_INVALID_PARAM;
    }

    return sql_exec_locked("INSERT INTO sessions "
                           "(id, user_id, created_at, last_active_at, ttl_seconds, status, "
                           " metadata) "
                           "VALUES (?, ?, ?, ?, ?, ?, ?);",
                           bind_session_row, (void *)record);
}

heapstore_error_t heapstore_registry_get_session(const char *id, heapstore_session_record_t *record)
{
    if (!id || !record) {
        return heapstore_ERR_INVALID_PARAM;
    }

    return registry_query_one("SELECT id, user_id, created_at, last_active_at, ttl_seconds, "
                              "status, metadata FROM sessions WHERE id = ?;",
                              registry_bind_id, (void *)id, extract_session_row, record);
}

heapstore_error_t heapstore_registry_update_session(const heapstore_session_record_t *record)
{
    if (!record || !record->id[0]) {
        return heapstore_ERR_INVALID_PARAM;
    }

    return sql_exec_locked("UPDATE sessions SET user_id = ?, last_active_at = ?, "
                           "ttl_seconds = ?, status = ?, metadata = ? WHERE id = ?;",
                           bind_session_update, (void *)record);
}

heapstore_error_t heapstore_registry_delete_session(const char *id)
{
    if (!id) {
        return heapstore_ERR_INVALID_PARAM;
    }

    return sql_exec_locked("DELETE FROM sessions WHERE id = ?;", registry_bind_id, (void *)id);
}

heapstore_error_t heapstore_registry_query_sessions(const char *filter_status,
                                                    heapstore_registry_iter_t **iter)
{
    heapstore_error_t pre = query_precheck(iter);
    if (pre != heapstore_SUCCESS) {
        return pre;
    }

    if (filter_status && filter_status[0]) {
        return registry_query_open("SELECT id, user_id, created_at, last_active_at, "
                                   "ttl_seconds, status, metadata FROM sessions "
                                   "WHERE status = ? ORDER BY last_active_at DESC;",
                                   heapstore_REGISTRY_REC_SESSION, registry_bind_id,
                                   (void *)filter_status, iter);
    }
    return registry_query_open("SELECT id, user_id, created_at, last_active_at, ttl_seconds, "
                               "status, metadata FROM sessions "
                               "ORDER BY last_active_at DESC;",
                               heapstore_REGISTRY_REC_SESSION, NULL, NULL, iter);
}

heapstore_error_t hs_batch_insert_sessions(
    const heapstore_session_record_t *records, size_t count)
{
    if (!records || count == 0) {
        return heapstore_ERR_INVALID_PARAM;
    }

    return batch_exec_locked("INSERT INTO sessions "
                                 "(id, user_id, created_at, last_active_at, ttl_seconds, "
                                 " status, metadata) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?);",
                                 bind_session_row, records, count,
                                 sizeof(heapstore_session_record_t));
}

#endif /* heapstore_SQLITE_IMPLEMENTATION */
