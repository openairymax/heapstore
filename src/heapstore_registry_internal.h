// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_registry_internal.h
 * @brief Registry internal shared definitions: SQLite internals and cross-file helpers.
 */

#ifndef AIRY_HEAPSTORE_REGISTRY_INTERNAL_H
#define AIRY_HEAPSTORE_REGISTRY_INTERNAL_H

#include "heapstore_registry.h"

#include "platform.h"

#ifdef AIRY_HAS_SQLITE3
#define heapstore_SQLITE_IMPLEMENTATION
#endif

#ifdef heapstore_SQLITE_IMPLEMENTATION
#include <sqlite3.h>

typedef struct {
    sqlite3 *db;
    char db_path[512];
    airy_mtx_t lock;
    int initialized;
} registry_db_t;

/**
  * @brief Registry iterator internal structure
 */
struct heapstore_registry_iter {
    sqlite3_stmt *stmt;
    int current_type;
    int has_more;
};

extern registry_db_t s_registry;

heapstore_error_t execute_sql_with_lock(
    const char *sql, heapstore_error_t (*bind_func)(sqlite3_stmt *, void *), void *bind_data);

heapstore_error_t bind_agent_record(sqlite3_stmt *stmt, void *data);

#endif /* heapstore_SQLITE_IMPLEMENTATION */

#endif /* AIRY_HEAPSTORE_REGISTRY_INTERNAL_H */
