/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file heapstore_migration.h
 * @brief AgentRT heapstore schema versioning and data migration interface.
 *
 * Provides version management for the heapstore data format:
 * - SCHEMA_VERSION persisted in the heapstore data directory
 * - automatic version-difference detection at startup
 * - forward-compatible (v1 -> v2) non-destructive migration
 * - backward-compatible (v2 -> v1) rollback (core data preserved)
 */

/* @owner: team-C */
#ifndef AIRY_RT_HEAPSTORE_MIGRATION_H
#define AIRY_RT_HEAPSTORE_MIGRATION_H

#include "heapstore.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief Current heapstore data format schema version
 *
  * Version scheme: MAJOR * 10000 + MINOR * 100 + PATCH
 * v1.0.0 = 10000, v1.1.0 = 10100, v2.0.0 = 20000
 */
#define HEAPSTORE_SCHEMA_VERSION_CURRENT 10000 /* v1.0.0 */

/**
  * @brief Migration direction
 */
typedef enum {
    HEAPSTORE_MIGRATE_FORWARD = 0,
    HEAPSTORE_MIGRATE_BACKWARD
} heapstore_migration_direction_t;

/**
  * @brief Migration step result
 */
typedef struct heapstore_migration_step {
    char name[128];
    heapstore_error_t result;
    uint64_t records_affected;
    uint64_t duration_ms;
} heapstore_migration_step_t;

/**
  * @brief Migration report
 */
typedef struct heapstore_migration_report {
    uint32_t from_version;
    uint32_t to_version;
    heapstore_migration_direction_t direction;
    size_t step_count;
    heapstore_migration_step_t *steps;
    bool success;
    uint64_t total_duration_ms;
    uint64_t total_records_affected;
} heapstore_migration_report_t;

/**
  * @brief Read the current schema version from the heapstore data directory
 *
  * The version is stored in .schema_version at the heapstore root.
  * A missing file is treated as version 0 (uninitialized).
 *
  * @param version [out] Output the current schema version
  * @return heapstore_error_t Error code
 *
 * @ownership version: BORROW (caller-owned buffer, function writes to it)
  * @threadsafe no (call early in heapstore_init)
 * @reentrant no
 *
  * @note Returns heapstore_ERR_NOT_INITIALIZED when uninitialized
 * @since v1.0.0
 */
heapstore_error_t heapstore_migration_get_version(uint32_t *version);

/**
  * @brief Write the current schema version to the heapstore data directory
 *
  * @param version [in] Version to write
  * @return heapstore_error_t Error code
 *
 * @threadsafe no
 * @reentrant no
 *
  * @note Usually called after a migration completes
 * @since v1.0.0
 */
heapstore_error_t heapstore_migration_set_version(uint32_t version);

/**
  * @brief Detect whether migration is needed
 *
  * Compares the on-disk version with the compile-time current version.
 *
  * @param needs_migration [out] Whether migration is needed
  * @param current_version [out] Current on-disk version (may be NULL)
  * @return heapstore_error_t Error code
 *
 * @ownership needs_migration: BORROW, current_version: BORROW (may be NULL)
 * @threadsafe no
 * @reentrant no
 *
 * @since v1.0.0
 */
heapstore_error_t heapstore_migration_check(bool *needs_migration, uint32_t *current_version);

/**
  * @brief Run a forward-compatible migration (v_from -> v_to)
 *
  * Non-destructive: adds fields and extends data structures.
  * Original data is preserved; the version is bumped only after success.
 *
  * @param target_version [in] Target version (0 upgrades to the latest)
  * @param report [out] Migration report (may be NULL)
  * @return heapstore_error_t Error code
 *
 * @ownership report: BORROW (caller-owned buffer, function writes to it, may be NULL)
 * @threadsafe no
 * @reentrant no
 *
  * @note The version is untouched on failure, keeping data consistent
 * @since v1.0.0
 */
heapstore_error_t heapstore_migration_forward(uint32_t target_version,
                                              heapstore_migration_report_t *report);

/**
  * @brief Run a backward-compatible rollback (v_to -> v_from)
 *
  * Drops new fields and keeps core data.
  * Requires explicit confirmation (no --force here; the caller decides).
 *
 * @param target_version [in] target version number
  * @param report [out] Rollback report (may be NULL)
  * @return heapstore_error_t Error code
 *
 * @ownership report: BORROW (caller-owned buffer, function writes to it, may be NULL)
 * @threadsafe no
 * @reentrant no
 *
  * @warning Rollback discards data added in newer versions
 * @since v1.0.0
 */
heapstore_error_t heapstore_migration_rollback(uint32_t target_version,
                                               heapstore_migration_report_t *report);

/**
  * @brief Free dynamically allocated memory in a migration report
 *
  * @param report [in] Migration report to free
 *
 * @ownership report: TRANSFER (function takes ownership of internal allocations)
 * @threadsafe yes
 * @reentrant yes
 *
 * @since v1.0.0
 */
void heapstore_migration_report_free(heapstore_migration_report_t *report);

/**
  * @brief List the fields of all record types in the current data format
 *
  * Lets migration scripts inspect the current schema.
 *
  * @param record_type [in] Record type name (e.g. "agent", "session", "skill")
  * @param fields [out] Output NULL-terminated field name array; caller frees
  * @param field_count [out] Field count
  * @return heapstore_error_t Error code
 *
 * @ownership fields: OWNER (caller must free each string and the array)
 * @threadsafe yes
 * @reentrant yes
 *
 * @since v1.0.0
 */
heapstore_error_t heapstore_migration_list_fields(const char *record_type, char ***fields,
                                                  size_t *field_count);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_RT_HEAPSTORE_MIGRATION_H */