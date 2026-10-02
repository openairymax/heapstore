// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_migration_rollback.c
 * @brief Backward-compatible (safe rollback) schema migration steps
 *        (functional domain after heapstore_migration.c split).
 */

// @owner: team-C
#include "heapstore_migration_internal.h"

/**
  * @brief v2.0.0 -> v1.1.0: remove the session metadata field
 *
 * The sessions table is rebuilt without metadata, keeping core fields; the
 * shared backup/open/close skeleton lives in mig_apply_cols().
 */
static heapstore_error_t rollback_v2_0_to_v1_1_session_metadata(uint64_t *records_affected)
{
    static const char *const drop_cols[] = {"metadata"};
    static const mig_col_op_t ops[] = {
        {.table = "sessions", .drop_columns = drop_cols, .drop_count = 1},
    };
    return mig_apply_cols(ops, 1, records_affected);
}

/**
  * @brief v1.1.0 -> v1.0.0: remove agent priority and tags fields
 *
 * The agents table is rebuilt without priority/tags, keeping core fields.
 */
static heapstore_error_t rollback_v1_1_to_v1_0_agent_fields(uint64_t *records_affected)
{
    static const char *const drop_cols[] = {"priority", "tags"};
    static const mig_col_op_t ops[] = {
        {.table = "agents", .drop_columns = drop_cols, .drop_count = 2},
    };
    return mig_apply_cols(ops, 1, records_affected);
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
    uint32_t current_ver = 0;
    heapstore_error_t err = mig_entry_begin(&current_ver);
    if (err != heapstore_SUCCESS) {
        return err;
    }

    if (current_ver <= target_version) {
        mig_report_noop(report, current_ver, HEAPSTORE_MIGRATE_BACKWARD);
        return heapstore_SUCCESS;
    }

    return mig_run_steps(g_rollback_steps, g_rollback_step_count, current_ver, target_version,
                         report, false);
}
