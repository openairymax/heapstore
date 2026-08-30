// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_migration.c
 * @brief AgentRT heapstore schema versioning and data migration: version
 *        detection, shared helpers and step execution engine.
 *
 * Functional domain after heapstore_migration.c split:
 * - version management of the heapstore data format (.schema_version)
 * - forward steps live in heapstore_migration_forward.c
 * - rollback steps live in heapstore_migration_rollback.c
 */

// @owner: team-C
#include "heapstore_migration_internal.h"

#include "utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "airy_memory.h"

void mig_get_version_file_path(char *buffer, size_t buffer_size)
{
    const char *root = heapstore_get_root();
    snprintf(buffer, buffer_size, "%s/%s", root, HEAPSTORE_MIGRATION_VERSION_FILE);
}

uint64_t mig_get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

heapstore_error_t mig_backup_data_file(const char *file_path)
{
    char backup_path[heapstore_MAX_PATH_LEN];
    snprintf(backup_path, sizeof(backup_path), "%s%s", file_path,
             HEAPSTORE_MIGRATION_BACKUP_SUFFIX);

    FILE *src = fopen(file_path, "rb");
    if (!src) {
        return heapstore_ERR_FILE_OPEN_FAILED;
    }

    FILE *dst = fopen(backup_path, "wb");
    if (!dst) {
        fclose(src);
        return heapstore_ERR_FILE_OPEN_FAILED;
    }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            fclose(src);
            fclose(dst);
            return heapstore_ERR_FILE_OPERATION_FAILED;
        }
    }

    fclose(src);
    fclose(dst);
    return heapstore_SUCCESS;
}

heapstore_error_t mig_restore_data_file(const char *file_path)
{
    char backup_path[heapstore_MAX_PATH_LEN];
    snprintf(backup_path, sizeof(backup_path), "%s%s", file_path,
             HEAPSTORE_MIGRATION_BACKUP_SUFFIX);

    if (rename(backup_path, file_path) != 0) {
        return heapstore_ERR_FILE_OPERATION_FAILED;
    }
    return heapstore_SUCCESS;
}

void mig_cleanup_backup_file(const char *file_path)
{
    char backup_path[heapstore_MAX_PATH_LEN];
    snprintf(backup_path, sizeof(backup_path), "%s%s", file_path,
             HEAPSTORE_MIGRATION_BACKUP_SUFFIX);
    remove(backup_path);
}

void mig_db_path(char *buffer, size_t buffer_size)
{
    const char *root = heapstore_get_root();
    snprintf(buffer, buffer_size, "%s%s", root, HEAPSTORE_MIGRATION_DB_REL_PATH);
}

#ifdef AIRY_HAS_SQLITE3

sqlite3 *mig_db_open(void)
{
    char db_path[heapstore_MAX_PATH_LEN];
    mig_db_path(db_path, sizeof(db_path));

    sqlite3 *db = NULL;
    if (sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) !=
        SQLITE_OK) {
        if (db) {
            sqlite3_close(db);
        }
        return NULL;
    }
    return db;
}

bool mig_table_has_column(sqlite3 *db, const char *table, const char *column)
{
    char sql[192];
    snprintf(sql, sizeof(sql), "PRAGMA table_info(%s);", table);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }

    bool found = false;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (name && strcmp(name, column) == 0) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

heapstore_error_t mig_add_column(sqlite3 *db, const char *table, const char *column,
                                 const char *column_def, uint64_t *records_affected)
{
    if (mig_table_has_column(db, table, column)) {
        *records_affected = 0;
        return heapstore_SUCCESS;
    }

    char sql[256];
    snprintf(sql, sizeof(sql), "ALTER TABLE %s ADD COLUMN %s;", table, column_def);

    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        sqlite3_free(err_msg);
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s;", table);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) {
        *records_affected = (uint64_t)sqlite3_column_int64(stmt, 0);
    } else {
        *records_affected = 0;
    }
    if (stmt) {
        sqlite3_finalize(stmt);
    }
    return heapstore_SUCCESS;
}

/**
 * Only runs if the column exists; restores the table and indexes afterwards.
 */
heapstore_error_t mig_drop_columns(sqlite3 *db, const char *table,
                                   const char *const *drop_columns, size_t drop_count,
                                   uint64_t *records_affected)
{
    *records_affected = 0;

    char keep_cols[1024] = {0};
    {
        char sql[192];
        snprintf(sql, sizeof(sql), "PRAGMA table_info(%s);", table);
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
            return heapstore_ERR_DB_QUERY_FAILED;
        }
        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            const char *name = (const char *)sqlite3_column_text(stmt, 1);
            if (!name) {
                continue;
            }
            bool drop = false;
            for (size_t i = 0; i < drop_count; i++) {
                if (strcmp(name, drop_columns[i]) == 0) {
                    drop = true;
                    break;
                }
            }
            if (drop) {
                continue;
            }
            if (keep_cols[0]) {
                strncat(keep_cols, ", ", sizeof(keep_cols) - strlen(keep_cols) - 1);
            }
            strncat(keep_cols, name, sizeof(keep_cols) - strlen(keep_cols) - 1);
        }
        sqlite3_finalize(stmt);
        if (keep_cols[0] == '\0') {
            return heapstore_ERR_DB_QUERY_FAILED;
        }
    }

    char *err_msg = NULL;
    int rc = sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    char sql[1200];
    snprintf(sql, sizeof(sql), "CREATE TABLE %s_tmp AS SELECT %s FROM %s;", table, keep_cols,
             table);
    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        sqlite3_free(err_msg);
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return heapstore_ERR_DB_QUERY_FAILED;
    }
    sqlite3_free(err_msg);
    err_msg = NULL;

    snprintf(sql, sizeof(sql), "DROP TABLE %s;", table);
    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        sqlite3_free(err_msg);
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return heapstore_ERR_DB_QUERY_FAILED;
    }
    sqlite3_free(err_msg);
    err_msg = NULL;

    snprintf(sql, sizeof(sql), "ALTER TABLE %s_tmp RENAME TO %s;", table, table);
    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        sqlite3_free(err_msg);
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return heapstore_ERR_DB_QUERY_FAILED;
    }
    sqlite3_free(err_msg);
    err_msg = NULL;

    if (strcmp(table, "agents") == 0) {
        sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_agent_type ON agents(type);", NULL, NULL,
                     NULL);
    } else if (strcmp(table, "sessions") == 0) {
        sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_session_user ON sessions(user_id);", NULL,
                     NULL, NULL);
    }

    rc = sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s;", table);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) {
        *records_affected = (uint64_t)sqlite3_column_int64(stmt, 0);
    }
    if (stmt) {
        sqlite3_finalize(stmt);
    }
    return heapstore_SUCCESS;
}

#endif /* AIRY_HAS_SQLITE3 */

heapstore_error_t heapstore_migration_get_version(uint32_t *version)
{
    if (!version) {
        return heapstore_ERR_INVALID_PARAM;
    }

    if (!heapstore_ready()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    char version_path[heapstore_MAX_PATH_LEN];
    mig_get_version_file_path(version_path, sizeof(version_path));

    FILE *f = fopen(version_path, "r");
    if (!f) {

        *version = 0;
        return heapstore_SUCCESS;
    }

    uint32_t ver = 0;

    char ver_buf[32];
    if (fgets(ver_buf, sizeof(ver_buf), f) != NULL) {
        ver = (uint32_t)strtoul(ver_buf, NULL, 10);
    } else {
        fclose(f);
        *version = 0;
        return heapstore_SUCCESS;
    }

    fclose(f);
    *version = ver;
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_migration_set_version(uint32_t version)
{
    if (!heapstore_ready()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    char version_path[heapstore_MAX_PATH_LEN];
    mig_get_version_file_path(version_path, sizeof(version_path));

    FILE *f = fopen(version_path, "w");
    if (!f) {
        return heapstore_ERR_FILE_OPEN_FAILED;
    }

    fprintf(f, "%u\n", version);
    fclose(f);
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_migration_check(bool *needs_migration, uint32_t *current_version)
{
    if (!needs_migration) {
        return heapstore_ERR_INVALID_PARAM;
    }

    uint32_t disk_version = 0;
    heapstore_error_t err = heapstore_migration_get_version(&disk_version);
    if (err != heapstore_SUCCESS) {
        return err;
    }

    if (current_version) {
        *current_version = disk_version;
    }

    if (disk_version == 0) {

        *needs_migration = false;
        return heapstore_migration_set_version(HEAPSTORE_SCHEMA_VERSION_CURRENT);
    }

    *needs_migration = (disk_version < HEAPSTORE_SCHEMA_VERSION_CURRENT);
    return heapstore_SUCCESS;
}

/**
  * @brief Fill the report header and allocate the per-step array.
 */
static void mig_report_prepare(heapstore_migration_report_t *report, uint32_t from_version,
                               uint32_t to_version, heapstore_migration_direction_t direction,
                               size_t step_count)
{
    if (!report) {
        return;
    }

    AIRY_MEMSET(report, 0, sizeof(*report));
    report->from_version = from_version;
    report->to_version = to_version;
    report->direction = direction;
    report->step_count = step_count;
    report->steps = (heapstore_migration_step_t *)AIRY_MALLOC(
        step_count * sizeof(heapstore_migration_step_t));
    if (report->steps) {
        AIRY_MEMSET(report->steps, 0, step_count * sizeof(heapstore_migration_step_t));
    }
}

heapstore_error_t mig_run_steps(const migration_step_def_t *steps, size_t step_count,
                                uint32_t current_version, uint32_t target_version,
                                heapstore_migration_report_t *report, bool forward)
{
    heapstore_migration_direction_t direction =
        forward ? HEAPSTORE_MIGRATE_FORWARD : HEAPSTORE_MIGRATE_BACKWARD;

    const migration_step_def_t *applicable_steps[HEAPSTORE_MIGRATION_MAX_STEPS];
    size_t applicable_count = 0;

    for (size_t i = 0; i < step_count && applicable_count < HEAPSTORE_MIGRATION_MAX_STEPS; i++) {
        bool applicable = forward ? (steps[i].from_version >= current_version &&
                                     steps[i].to_version <= target_version)
                                  : (steps[i].from_version <= current_version &&
                                     steps[i].to_version >= target_version);
        if (applicable) {
            applicable_steps[applicable_count++] = &steps[i];
        }
    }

    mig_report_prepare(report, current_version, target_version, direction, applicable_count);

    uint64_t total_start = mig_get_time_ms();
    bool all_success = true;

    for (size_t i = 0; i < applicable_count; i++) {
        uint64_t step_start = mig_get_time_ms();
        uint64_t records = 0;

        heapstore_error_t step_err = applicable_steps[i]->execute(&records);
        uint64_t step_duration = mig_get_time_ms() - step_start;

        if (report && report->steps) {
            AIRY_STRNCPY_TERM(report->steps[i].name, applicable_steps[i]->name,
                              sizeof(report->steps[i].name));
            report->steps[i].result = step_err;
            report->steps[i].records_affected = records;
            report->steps[i].duration_ms = step_duration;
        }

        if (step_err != heapstore_SUCCESS) {
            all_success = false;

            break;
        }
    }

    if (all_success) {

        heapstore_error_t err = heapstore_migration_set_version(target_version);
        if (err != heapstore_SUCCESS) {
            all_success = false;
        }
    }

    if (report) {
        report->success = all_success;
        report->total_duration_ms = mig_get_time_ms() - total_start;
    }

    return all_success ? heapstore_SUCCESS : heapstore_ERR_INTERNAL;
}

void heapstore_migration_report_free(heapstore_migration_report_t *report)
{
    if (!report || !report->steps) {
        return;
    }
    AIRY_FREE(report->steps);
    report->steps = NULL;
    report->step_count = 0;
}

heapstore_error_t heapstore_migration_list_fields(const char *record_type, char ***fields,
                                                  size_t *field_count)
{
    if (!record_type || !fields || !field_count) {
        return heapstore_ERR_INVALID_PARAM;
    }

    *fields = NULL;
    *field_count = 0;

    static const char *agent_fields[] = {"id",       "name",        "type",       "version",
                                         "status",   "config_path", "created_at", "updated_at",

                                         "priority", "tags",        NULL};

    static const char *session_fields[] = {"id",          "user_id", "created_at", "last_active_at",
                                           "ttl_seconds", "status",

                                           "metadata",    NULL};

    static const char *skill_fields[] = {
        "id", "name", "version", "library_path", "manifest_path", "installed_at", NULL};

    static const char *memory_pool_fields[] = {"pool_id",          "name",
                                               "total_size",       "used_size",
                                               "block_size",       "block_count",
                                               "free_block_count", "created_at",
                                               "status",           NULL};

    static const char *memory_alloc_fields[] = {"allocation_id", "pool_id",  "size",   "address",
                                                "allocated_at",  "freed_at", "status", NULL};

    static const char *ipc_channel_fields[] = {"channel_id",  "name",          "type",
                                               "status",      "created_at",    "last_activity_at",
                                               "buffer_size", "current_usage", NULL};

    static const char *ipc_buffer_fields[] = {"buffer_id",  "channel_id", "size", "used",
                                              "created_at", "status",     NULL};

    static const char *span_fields[] = {"trace_id",    "span_id",      "parent_span_id",
                                        "name",        "kind",         "start_time_ns",
                                        "end_time_ns", "service_name", "status",
                                        NULL};

    typedef struct {
        const char *type;
        const char **fields;
    } field_map_t;

    static const field_map_t field_maps[] = {
        {"agent", agent_fields},
        {"session", session_fields},
        {"skill", skill_fields},
        {"memory_pool", memory_pool_fields},
        {"memory_allocation", memory_alloc_fields},
        {"ipc_channel", ipc_channel_fields},
        {"ipc_buffer", ipc_buffer_fields},
        {"span", span_fields},
        {NULL, NULL},
    };

    const char **selected = NULL;
    for (const field_map_t *fm = field_maps; fm->type != NULL; fm++) {
        if (strcmp(fm->type, record_type) == 0) {
            selected = fm->fields;
            break;
        }
    }

    if (!selected) {
        return heapstore_ERR_NOT_FOUND;
    }

    size_t count = 0;
    while (selected[count] != NULL) {
        count++;
    }

    char **out = (char **)AIRY_MALLOC((count + 1) * sizeof(char *));
    if (!out) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < count; i++) {
        out[i] = (char *)AIRY_MALLOC(strlen(selected[i]) + 1);
        if (!out[i]) {

            for (size_t j = 0; j < i; j++) {
                AIRY_FREE(out[j]);
            }
            AIRY_FREE(out);
            return heapstore_ERR_OUT_OF_MEMORY;
        }
        AIRY_STRNCPY_TERM(out[i], selected[i], strlen(selected[i]) + 1);
    }
    out[count] = NULL;

    *fields = out;
    *field_count = count;
    return heapstore_SUCCESS;
}
