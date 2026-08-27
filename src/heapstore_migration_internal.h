/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file heapstore_migration_internal.h
 * @brief Internal header shared by the migration functional domains
 *        (management+version detection / forward steps / rollback steps)
 *        after heapstore_migration.c was split.
 */

/* @owner: team-C */
#ifndef AIRY_HEAPSTORE_MIGRATION_INTERNAL_H
#define AIRY_HEAPSTORE_MIGRATION_INTERNAL_H

#include "heapstore.h"
#include "heapstore_migration.h"
#include "private.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef AIRY_HAS_SQLITE3
#include <sqlite3.h>
#endif

#define HEAPSTORE_MIGRATION_VERSION_FILE ".schema_version"
#define HEAPSTORE_MIGRATION_BACKUP_SUFFIX ".pre_migration_bak"
#define HEAPSTORE_MIGRATION_MAX_STEPS 64
#define HEAPSTORE_MIGRATION_DB_REL_PATH "/registry/registry.db"

/**
  * @brief Migration step function type
 */
typedef heapstore_error_t (*migration_step_fn)(uint64_t *records_affected);

/**
  * @brief Migration step definition
 */
typedef struct {
    const char *name;
    migration_step_fn execute;
    uint32_t from_version;
    uint32_t to_version;
} migration_step_def_t;

/* ---- Shared helpers (heapstore_migration.c) ---- */

/**
  * @brief Get the full path of the migration version file
 */
void mig_get_version_file_path(char *buffer, size_t buffer_size);

/**
  * @brief Get the current timestamp (ms)
 */
uint64_t mig_get_time_ms(void);

/**
  * @brief Back up data files
 */
heapstore_error_t mig_backup_data_file(const char *file_path);

/**
  * @brief Restore data files from backup
 */
heapstore_error_t mig_restore_data_file(const char *file_path);

/**
  * @brief Clean up backup files
 */
void mig_cleanup_backup_file(const char *file_path);

/**
  * @brief Resolve the registry DB path: heapstore root + registry/registry.db
 */
void mig_db_path(char *buffer, size_t buffer_size);

/**
  * @brief Select applicable steps, execute them in order and fill the report.
 *
 * @param steps Step table
 * @param step_count Step table size
 * @param current_version Current on-disk version
 * @param target_version Target version
 * @param report Report buffer (may be NULL)
 * @param forward true for forward selection (from>=current && to<=target),
 *                false for rollback selection (from<=current && to>=target)
 */
heapstore_error_t mig_run_steps(const migration_step_def_t *steps, size_t step_count,
                                uint32_t current_version, uint32_t target_version,
                                heapstore_migration_report_t *report, bool forward);

#ifdef AIRY_HAS_SQLITE3

/* ---- Shared SQLite helpers (heapstore_migration.c) ---- */

/**
  * @brief Open the registry DB (read-only migration view; NULL on failure)
 */
sqlite3 *mig_db_open(void);

/**
  * @brief Check whether a table has a given column
 */
bool mig_table_has_column(sqlite3 *db, const char *table, const char *column);

/**
  * @brief Idempotently add a column (skip if present); returns changed row count
 */
heapstore_error_t mig_add_column(sqlite3 *db, const char *table, const char *column,
                                 const char *column_def, uint64_t *records_affected);

/**
  * @brief Drop columns by rebuilding the table (SQLite < 3.35 lacks DROP COLUMN)
 */
heapstore_error_t mig_drop_columns(sqlite3 *db, const char *table,
                                   const char *const *drop_columns, size_t drop_count,
                                   uint64_t *records_affected);

#endif /* AIRY_HAS_SQLITE3 */

#endif /* AIRY_HEAPSTORE_MIGRATION_INTERNAL_H */
