// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_registry_sqlite_agent.c
 * @brief Registry SQLite backend agent domain: CRUD, query and batch insert.
 */

// @owner: team-B
#include "heapstore_registry_internal.h"

#ifdef heapstore_SQLITE_IMPLEMENTATION

static heapstore_error_t bind_agent_update(sqlite3_stmt *stmt, void *data)
{
    const heapstore_agent_record_t *record = (const heapstore_agent_record_t *)data;

    sqlite3_bind_text(stmt, 1, record->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, record->type, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, record->version, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, record->status, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, record->config_path, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 6, record->updated_at);
    sqlite3_bind_int(stmt, 7, record->priority);
    sqlite3_bind_text(stmt, 8, record->tags, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, record->id, -1, SQLITE_STATIC);
    return heapstore_SUCCESS;
}

typedef struct {
    const char *type;
    const char *status;
} agent_filters_t;

static heapstore_error_t bind_agent_filters(sqlite3_stmt *stmt, void *data)
{
    const agent_filters_t *f = (const agent_filters_t *)data;

    int idx = 1;
    if (f->type) {
        sqlite3_bind_text(stmt, idx++, f->type, -1, SQLITE_STATIC);
    }
    if (f->status) {
        sqlite3_bind_text(stmt, idx++, f->status, -1, SQLITE_STATIC);
    }
    return heapstore_SUCCESS;
}

void extract_agent_row(sqlite3_stmt *stmt, void *rec)
{
    heapstore_agent_record_t *record = (heapstore_agent_record_t *)rec;
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
        AIRY_STRNCPY_TERM(record->type, text, sizeof(record->type));
    }
    text = (const char *)sqlite3_column_text(stmt, 3);
    if (text) {
        AIRY_STRNCPY_TERM(record->version, text, sizeof(record->version));
    }
    text = (const char *)sqlite3_column_text(stmt, 4);
    if (text) {
        AIRY_STRNCPY_TERM(record->status, text, sizeof(record->status));
    }
    text = (const char *)sqlite3_column_text(stmt, 5);
    if (text) {
        AIRY_STRNCPY_TERM(record->config_path, text, sizeof(record->config_path));
    }
    record->created_at = sqlite3_column_int64(stmt, 6);
    record->updated_at = sqlite3_column_int64(stmt, 7);
    record->priority = sqlite3_column_int(stmt, 8);
    text = (const char *)sqlite3_column_text(stmt, 9);
    if (text) {
        AIRY_STRNCPY_TERM(record->tags, text, sizeof(record->tags));
    }
}

heapstore_error_t heapstore_registry_add_agent(const heapstore_agent_record_t *record)
{
    if (!record || !record->id[0]) {
        return heapstore_ERR_INVALID_PARAM;
    }

    return sql_exec_locked("INSERT INTO agents "
                           "(id, name, type, version, status, config_path, created_at, "
                           " updated_at, priority, tags) "
                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
                           bind_agent_record, (void *)record);
}

heapstore_error_t heapstore_registry_get_agent(const char *id, heapstore_agent_record_t *record)
{
    if (!id || !record) {
        return heapstore_ERR_INVALID_PARAM;
    }

    return registry_query_one("SELECT id, name, type, version, status, config_path, "
                              "created_at, updated_at, priority, tags "
                              "FROM agents WHERE id = ?;",
                              registry_bind_id, (void *)id, extract_agent_row, record);
}

heapstore_error_t heapstore_registry_update_agent(const heapstore_agent_record_t *record)
{
    if (!record || !record->id[0]) {
        return heapstore_ERR_INVALID_PARAM;
    }

    return sql_exec_locked("UPDATE agents SET "
                           "name = ?, type = ?, version = ?, status = ?, config_path = ?, "
                           "updated_at = ?, priority = ?, tags = ? "
                           "WHERE id = ?;",
                           bind_agent_update, (void *)record);
}

heapstore_error_t heapstore_registry_delete_agent(const char *id)
{
    if (!id) {
        return heapstore_ERR_INVALID_PARAM;
    }

    return sql_exec_locked("DELETE FROM agents WHERE id = ?;", registry_bind_id, (void *)id);
}

heapstore_error_t heapstore_registry_query_agents(const char *filter_type,
                                                  const char *filter_status,
                                                  heapstore_registry_iter_t **iter)
{
    if (!iter) {
        return heapstore_ERR_INVALID_PARAM;
    }
    if (!s_registry.initialized || !s_registry.db) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT id, name, type, version, status, config_path, created_at, "
             "updated_at, priority, tags FROM agents WHERE 1=1");
    if (filter_type) {
        size_t pos = strlen(sql);
        snprintf(sql + pos, sizeof(sql) - pos, " AND type = ?");
    }
    if (filter_status) {
        size_t pos = strlen(sql);
        snprintf(sql + pos, sizeof(sql) - pos, " AND status = ?");
    }

    agent_filters_t filters = {filter_type, filter_status};
    return registry_query_open(sql, heapstore_REGISTRY_REC_AGENT, bind_agent_filters, &filters,
                               iter);
}

heapstore_error_t heapstore_registry_batch_insert_agents(const heapstore_agent_record_t *records,
                                                         size_t count)
{
    if (!records || count == 0) {
        return heapstore_ERR_INVALID_PARAM;
    }

    return batch_exec_locked("INSERT INTO agents "
                                 "(id, name, type, version, status, config_path, created_at, "
                                 " updated_at, priority, tags) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
                                 bind_agent_record, records, count,
                                 sizeof(heapstore_agent_record_t));
}

#endif /* heapstore_SQLITE_IMPLEMENTATION */
