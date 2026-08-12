// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_registry_sqlite_iter.c
 * @brief Registry SQLite backend iterator domain: iteration and iterator destroy.
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

/**
  * @brief Iterate to the next record
 *
  * Returns the record type selected by the iterator's current_type
 */
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

    const char *text;

    switch (iter->current_type) {
    case 0: { /* agents */
        heapstore_agent_record_t *agent_rec = (heapstore_agent_record_t *)record;
        __builtin_memset(agent_rec, 0, sizeof(*agent_rec));

        text = (const char *)sqlite3_column_text(iter->stmt, 0);
        if (text)
            AIRY_STRNCPY_TERM(agent_rec->id, text, sizeof(agent_rec->id));
        text = (const char *)sqlite3_column_text(iter->stmt, 1);
        if (text)
            AIRY_STRNCPY_TERM(agent_rec->name, text, sizeof(agent_rec->name));
        text = (const char *)sqlite3_column_text(iter->stmt, 2);
        if (text)
            AIRY_STRNCPY_TERM(agent_rec->type, text, sizeof(agent_rec->type));
        text = (const char *)sqlite3_column_text(iter->stmt, 3);
        if (text)
            AIRY_STRNCPY_TERM(agent_rec->version, text, sizeof(agent_rec->version));
        text = (const char *)sqlite3_column_text(iter->stmt, 4);
        if (text)
            AIRY_STRNCPY_TERM(agent_rec->status, text, sizeof(agent_rec->status));
        text = (const char *)sqlite3_column_text(iter->stmt, 5);
        if (text)
            AIRY_STRNCPY_TERM(agent_rec->config_path, text, sizeof(agent_rec->config_path));
        agent_rec->created_at = sqlite3_column_int64(iter->stmt, 6);
        agent_rec->updated_at = sqlite3_column_int64(iter->stmt, 7);
        break;
    }
    case 1: { /* skills */
        heapstore_skill_record_t *skill_rec = (heapstore_skill_record_t *)record;
        __builtin_memset(skill_rec, 0, sizeof(*skill_rec));

        text = (const char *)sqlite3_column_text(iter->stmt, 0);
        if (text)
            AIRY_STRNCPY_TERM(skill_rec->id, text, sizeof(skill_rec->id));
        text = (const char *)sqlite3_column_text(iter->stmt, 1);
        if (text)
            AIRY_STRNCPY_TERM(skill_rec->name, text, sizeof(skill_rec->name));
        text = (const char *)sqlite3_column_text(iter->stmt, 2);
        if (text)
            AIRY_STRNCPY_TERM(skill_rec->version, text, sizeof(skill_rec->version));
        text = (const char *)sqlite3_column_text(iter->stmt, 3);
        if (text)
            AIRY_STRNCPY_TERM(skill_rec->library_path, text, sizeof(skill_rec->library_path));
        text = (const char *)sqlite3_column_text(iter->stmt, 4);
        if (text)
            AIRY_STRNCPY_TERM(skill_rec->manifest_path, text, sizeof(skill_rec->manifest_path));
        skill_rec->installed_at = sqlite3_column_int64(iter->stmt, 5);
        break;
    }
    case 2: { /* sessions */
        heapstore_session_record_t *session_rec = (heapstore_session_record_t *)record;
        __builtin_memset(session_rec, 0, sizeof(*session_rec));

        text = (const char *)sqlite3_column_text(iter->stmt, 0);
        if (text)
            AIRY_STRNCPY_TERM(session_rec->id, text, sizeof(session_rec->id));
        text = (const char *)sqlite3_column_text(iter->stmt, 1);
        if (text)
            AIRY_STRNCPY_TERM(session_rec->user_id, text, sizeof(session_rec->user_id));
        session_rec->created_at = sqlite3_column_int64(iter->stmt, 2);
        session_rec->last_active_at = sqlite3_column_int64(iter->stmt, 3);
        session_rec->ttl_seconds = sqlite3_column_int(iter->stmt, 4);
        text = (const char *)sqlite3_column_text(iter->stmt, 5);
        if (text)
            AIRY_STRNCPY_TERM(session_rec->status, text, sizeof(session_rec->status));
        text = (const char *)sqlite3_column_text(iter->stmt, 6);
        if (text)
            AIRY_STRNCPY_TERM(session_rec->metadata, text, sizeof(session_rec->metadata));
        break;
    }
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
