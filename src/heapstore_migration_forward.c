// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_migration_forward.c
 * @brief Forward-compatible (non-destructive) schema migration steps
 *        (functional domain after heapstore_migration.c split).
 */

// @owner: team-C
#include "heapstore_migration_internal.h"

#include "airy_memory.h"

#include <sys/stat.h>

/**
  * @brief v1.0.0 -> v1.1.0: add priority and tags fields to agent_record
 *
  * Implementation: append priority/tags columns to the agents table
  * (idempotent: skipped if present) and apply defaults to existing rows.
 */
static heapstore_error_t migrate_v1_0_to_v1_1_agent_fields(uint64_t *records_affected)
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

    uint64_t affected = 0;
    uint64_t step_affected = 0;
    err = mig_add_column(db, "agents", "priority", "priority INTEGER DEFAULT 0", &step_affected);
    if (err != heapstore_SUCCESS) {
        sqlite3_close(db);
        mig_restore_data_file(db_path);
        return err;
    }
    affected += step_affected;
    err = mig_add_column(db, "agents", "tags", "tags TEXT DEFAULT ''", &step_affected);
    if (err != heapstore_SUCCESS) {
        sqlite3_close(db);
        mig_restore_data_file(db_path);
        return err;
    }
    affected += step_affected;

    sqlite3_close(db);
    mig_cleanup_backup_file(db_path);
    *records_affected = affected;
    return heapstore_SUCCESS;
#else

    return heapstore_SUCCESS;
#endif
}

/**
  * @brief v1.1.0 -> v2.0.0: add a metadata field to session_record
 */
static heapstore_error_t migrate_v1_1_to_v2_0_session_metadata(uint64_t *records_affected)
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

    uint64_t affected = 0;
    err = mig_add_column(db, "sessions", "metadata", "metadata TEXT DEFAULT ''", &affected);
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

static const migration_step_def_t g_forward_steps[] = {
    {
        .name = "v1.0→v1.1: Agent priority/tags fields",
        .execute = migrate_v1_0_to_v1_1_agent_fields,
        .from_version = 10000,
        .to_version = 10100,
    },
    {
        .name = "v1.1→v2.0: Session metadata field",
        .execute = migrate_v1_1_to_v2_0_session_metadata,
        .from_version = 10100,
        .to_version = 20000,
    },
};

static const size_t g_forward_step_count = sizeof(g_forward_steps) / sizeof(g_forward_steps[0]);

heapstore_error_t heapstore_migration_forward(uint32_t target_version,
                                              heapstore_migration_report_t *report)
{
    if (!heapstore_is_initialized()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    uint32_t current_ver = 0;
    heapstore_error_t err = heapstore_migration_get_version(&current_ver);
    if (err != heapstore_SUCCESS) {
        return err;
    }

    if (target_version == 0) {
        target_version = HEAPSTORE_SCHEMA_VERSION_CURRENT;
    }

    if (current_ver >= target_version) {

        if (report) {
            AIRY_MEMSET(report, 0, sizeof(*report));
            report->from_version = current_ver;
            report->to_version = current_ver;
            report->direction = HEAPSTORE_MIGRATE_FORWARD;
            report->success = true;
        }
        return heapstore_SUCCESS;
    }

    return mig_run_steps(g_forward_steps, g_forward_step_count, current_ver, target_version,
                         report, true);
}
