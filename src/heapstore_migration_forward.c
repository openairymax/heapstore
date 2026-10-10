// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_migration_forward.c
 * @brief Forward-compatible (non-destructive) schema migration steps
 *        (functional domain after heapstore_migration.c split).
 */

// @owner: team-C
#include "heapstore_migration_internal.h"

/**
  * @brief v1.0.0 -> v1.1.0: add priority and tags fields to agent_record
 *
 * Columns are appended idempotently with row-count defaults; the shared
 * backup/open/close skeleton lives in mig_apply_cols().
 */
static heapstore_error_t migrate_v1_0_to_v1_1_agent_fields(uint64_t *records_affected)
{
    static const mig_col_op_t ops[] = {
        {.table = "agents", .column = "priority", .column_def = "priority INTEGER DEFAULT 0"},
        {.table = "agents", .column = "tags", .column_def = "tags TEXT DEFAULT ''"},
    };
    return mig_apply_cols(ops, 2, records_affected);
}

/**
  * @brief v1.1.0 -> v2.0.0: add a metadata field to session_record
 */
static heapstore_error_t migrate_v1_1_to_v2_0_session_metadata(uint64_t *records_affected)
{
    static const mig_col_op_t ops[] = {
        {.table = "sessions", .column = "metadata", .column_def = "metadata TEXT DEFAULT ''"},
    };
    return mig_apply_cols(ops, 1, records_affected);
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
    uint32_t current_ver = 0;
    heapstore_error_t err = mig_entry_begin(&current_ver);
    if (err != heapstore_SUCCESS) {
        return err;
    }

    if (target_version == 0) {
        target_version = HEAPSTORE_SCHEMA_VERSION_CURRENT;
    }

    if (current_ver >= target_version) {
        mig_report_noop(report, current_ver);
        return heapstore_SUCCESS;
    }

    return mig_run_steps(g_forward_steps, g_forward_step_count, current_ver, target_version,
                         report);
}
