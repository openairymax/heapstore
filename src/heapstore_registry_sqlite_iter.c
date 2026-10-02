// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_registry_sqlite_iter.c
 * @brief Registry SQLite backend iterator domain: iteration and iterator destroy.
 */

// @owner: team-B
#include "heapstore_registry_internal.h"

#ifdef heapstore_SQLITE_IMPLEMENTATION

heapstore_error_t heapstore_registry_iter_next(heapstore_registry_iter_t *iter, void *record)
{
    if (!iter || !record) {
        return heapstore_ERR_INVALID_PARAM;
    }
    if (!iter->stmt || !iter->has_more) {
        return heapstore_ERR_NOT_FOUND;
    }

    int rc = sqlite3_step(iter->stmt);
    if (rc != SQLITE_ROW) {
        iter->has_more = 0;
        return heapstore_ERR_NOT_FOUND;
    }

    switch (iter->current_type) {
    case heapstore_REGISTRY_REC_AGENT:
        extract_agent_row(iter->stmt, record);
        break;
    case heapstore_REGISTRY_REC_SKILL:
        extract_skill_row(iter->stmt, record);
        break;
    case heapstore_REGISTRY_REC_SESSION:
        extract_session_row(iter->stmt, record);
        break;
    default:
        return heapstore_ERR_INVALID_PARAM;
    }

    return heapstore_SUCCESS;
}

void heapstore_registry_iter_destroy(heapstore_registry_iter_t *iter)
{
    if (!iter) {
        return;
    }

    if (iter->stmt) {
        sqlite3_finalize(iter->stmt);
        iter->stmt = NULL;
    }

    AIRY_FREE(iter);
}

#endif /* heapstore_SQLITE_IMPLEMENTATION */
