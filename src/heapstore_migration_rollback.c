// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_migration_rollback.c
 * @brief Backward-compatible (safe rollback) schema migration steps
 *        (functional domain after heapstore_migration.c split).
 */

// @owner: team-C
#include "heapstore_migration_internal.h"

#include "airy_memory.h"

#include <sys/stat.h>

/**
  * @brief v2.0.0 -> v1.1.0: remove the session metadata field
 *
  * Implementation: rebuild the sessions table without metadata, keeping core fields.
 */
static heapstore_error_t rollback_v2_0_to_v1_1_session_metadata(uint64_t *records_affected)
{
    *records_affected = 0;

#ifdef AIRY_HAS_SQLITE3
    char db_path[heapstore_MAX_PATH_LEN];
    mig_db_path(db_path, sizeof(db_path));
    struct stat st;
    if (stat(db_path, &st) != 0) {
        return heapstore_SUCCESS;
    }

    heapstore_error_t err = mig_backup_data_file(db_path);
    if (err != heapstore_SUCCESS) {
        return err;
    }

    sqlite3 *db = mig_db_open();
    if (!db) {
        mig_restore_data_file(db_path);
        return heapstore_ERR_DB_INIT_FAILED;
    }

    static const char *drop_cols[] = {"metadata"};
    uint64_t affected = 0;
    err = mig_drop_columns(db, "sessions", drop_cols, 1, &affected);
    if (err != heapstore_SUCCESS) {
        sqlite3_close(db);
        mig_restore_data_file(db_path);
        return err;
    }

    sqlite3_close(db);
    mig_cleanup_backup_file(db_path);
    *records_affected = affected;
    return heapstore_SUCCESS;
#else
    return heapstore_SUCCESS;
#endif
}

/**
  * @brief v1.1.0 -> v1.0.0: remove agent priority and tags fields
 *
  * Implementation: rebuild the agents table without priority/tags, keeping core fields.
 */
static heapstore_error_t rollback_v1_1_to_v1_0_agent_fields(uint64_t *records_affected)
{
    *records_affected = 0;

#ifdef AIRY_HAS_SQLITE3
    char db_path[heapstore_MAX_PATH_LEN];
    mig_db_path(db_path, sizeof(db_path));
    struct stat st;
    if (stat(db_path, &st) != 0) {
        return heapstore_SUCCESS;
    }

    heapstore_error_t err = mig_backup_data_file(db_path);
    if (err != heapstore_SUCCESS) {
        return err;
    }

    sqlite3 *db = mig_db_open();
    if (!db) {
        mig_restore_data_file(db_path);
        return heapstore_ERR_DB_INIT_FAILED;
    }

    static const char *drop_cols[] = {"priority", "tags"};
    uint64_t affected = 0;
    err = mig_drop_columns(db, "agents", drop_cols, 2, &affected);
    if (err != heapstore_SUCCESS) {
        sqlite3_close(db);
        mig_restore_data_file(db_path);
        return err;
    }

    sqlite3_close(db);
    mig_cleanup_backup_file(db_path);
    *records_affected = affected;
    return heapstore_SUCCESS;
#else
    return heapstore_SUCCESS;
#endif
}

static const migration_step_def_t g_rollback_steps[] = {
    {
        .name = "v2.0→v1.1: Remove session metadata",
        .execute = rollback_v2_0_to_v1_1_session_metadata,
        .from_version = 20000,
        .to_version = 10100,
    },
    {
        .name = "v1.1→v1.0: Remove agent priority/tags",
        .execute = rollback_v1_1_to_v1_0_agent_fields,
        .from_version = 10100,
        .to_version = 10000,
    },
};

static const size_t g_rollback_step_count = sizeof(g_rollback_steps) / sizeof(g_rollback_steps[0]);

heapstore_error_t heapstore_migration_rollback(uint32_t target_version,
                                               heapstore_migration_report_t *report)
{
    if (!heapstore_ready()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    uint32_t current_ver = 0;
    heapstore_error_t err = heapstore_migration_get_version(&current_ver);
    if (err != heapstore_SUCCESS) {
        return err;
    }

    if (current_ver <= target_version) {
        if (report) {
            AIRY_MEMSET(report, 0, sizeof(*report));
            report->from_version = current_ver;
            report->to_version = current_ver;
            report->direction = HEAPSTORE_MIGRATE_BACKWARD;
            report->success = true;
        }
        return heapstore_SUCCESS;
    }

    return mig_run_steps(g_rollback_steps, g_rollback_step_count, current_ver, target_version,
                         report, false);
}
