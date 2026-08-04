/**
 * @file heapstore_migration.c
 * @brief AgentRT heapstore Schema 版本化与数据迁移实现
 *
 * 实现 heapstore 数据格式的版本管理，包括：
 * - SCHEMA_VERSION 持久化到 .schema_version 文件
 * - 启动时版本检测与自动迁移触发
 * - 前向兼容 (v1 → v2) 非破坏性迁移
 * - 后向兼容 (v2 → v1) 安全回滚
 *
 * Copyright (C) 2025-2026 SPHARX Ltd. All Rights Reserved.
 * SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
 * SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
 */

// @owner: team-C
#include "heapstore_migration.h"
#include "private.h"
#include "utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "airy_memory.h"

#ifdef AIRY_HAS_SQLITE3
#include <sqlite3.h>
#endif

/* ========== 内部常量 ========== */

#define HEAPSTORE_MIGRATION_VERSION_FILE ".schema_version"
#define HEAPSTORE_MIGRATION_BACKUP_SUFFIX ".pre_migration_bak"
#define HEAPSTORE_MIGRATION_MAX_STEPS 64
#define HEAPSTORE_MIGRATION_DB_REL_PATH "/registry/registry.db"

/* ========== 工具函数 ========== */

/**
 * @brief 获取迁移版本文件的完整路径
 */
static void get_version_file_path(char *buffer, size_t buffer_size)
{
    const char *root = heapstore_get_root();
    snprintf(buffer, buffer_size, "%s/%s", root, HEAPSTORE_MIGRATION_VERSION_FILE);
}

/**
 * @brief 获取当前时间戳（毫秒）
 */
static uint64_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/**
 * @brief 备份数据文件
 */
static heapstore_error_t backup_data_file(const char *file_path)
{
    char backup_path[heapstore_MAX_PATH_LEN];
    snprintf(backup_path, sizeof(backup_path), "%s%s", file_path,
             HEAPSTORE_MIGRATION_BACKUP_SUFFIX);

    /* 复制文件 */
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

/**
 * @brief 从备份恢复数据文件
 */
static heapstore_error_t restore_data_file(const char *file_path)
{
    char backup_path[heapstore_MAX_PATH_LEN];
    snprintf(backup_path, sizeof(backup_path), "%s%s", file_path,
             HEAPSTORE_MIGRATION_BACKUP_SUFFIX);

    if (rename(backup_path, file_path) != 0) {
        return heapstore_ERR_FILE_OPERATION_FAILED;
    }
    return heapstore_SUCCESS;
}

/**
 * @brief 清理备份文件
 */
static void cleanup_backup_file(const char *file_path)
{
    char backup_path[heapstore_MAX_PATH_LEN];
    snprintf(backup_path, sizeof(backup_path), "%s%s", file_path,
             HEAPSTORE_MIGRATION_BACKUP_SUFFIX);
    remove(backup_path);
}

/* ========== P3.20.1: Schema 版本化 ========== */

heapstore_error_t heapstore_migration_get_version(uint32_t *version)
{
    if (!version) {
        return heapstore_ERR_INVALID_PARAM;
    }

    if (!heapstore_is_initialized()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    char version_path[heapstore_MAX_PATH_LEN];
    get_version_file_path(version_path, sizeof(version_path));

    FILE *f = fopen(version_path, "r");
    if (!f) {
        /* 文件不存在，版本为 0 */
        *version = 0;
        return heapstore_SUCCESS;
    }

    uint32_t ver = 0;
    /* BAN-151: 使用 fgets+strtoul 替代被禁止的 fscanf */
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
    if (!heapstore_is_initialized()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    char version_path[heapstore_MAX_PATH_LEN];
    get_version_file_path(version_path, sizeof(version_path));

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
        /* 首次初始化，写入当前版本 */
        *needs_migration = false;
        return heapstore_migration_set_version(HEAPSTORE_SCHEMA_VERSION_CURRENT);
    }

    *needs_migration = (disk_version < HEAPSTORE_SCHEMA_VERSION_CURRENT);
    return heapstore_SUCCESS;
}

/* ========== P3.20.2: 前向兼容迁移 ========== */

/**
 * @brief 迁移步骤函数类型
 */
typedef heapstore_error_t (*migration_step_fn)(uint64_t *records_affected);

/**
 * @brief 迁移步骤定义
 */
typedef struct {
    const char *name;
    migration_step_fn execute;
    uint32_t from_version;
    uint32_t to_version;
} migration_step_def_t;

/* ---- 具体迁移步骤实现 ---- */

/**
 * @brief 解析 registry 数据库路径：heapstore 根 + registry/registry.db
 */
static void mig_db_path(char *buffer, size_t buffer_size)
{
    const char *root = heapstore_get_root();
    snprintf(buffer, buffer_size, "%s%s", root, HEAPSTORE_MIGRATION_DB_REL_PATH);
}

#ifdef AIRY_HAS_SQLITE3

/**
 * @brief 打开 registry 数据库（只读迁移视图，失败返回 NULL）
 */
static sqlite3 *mig_db_open(void)
{
    char db_path[heapstore_MAX_PATH_LEN];
    mig_db_path(db_path, sizeof(db_path));

    sqlite3 *db = NULL;
    if (sqlite3_open_v2(db_path, &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        if (db) {
            sqlite3_close(db);
        }
        return NULL;
    }
    return db;
}

/**
 * @brief 检查表是否包含指定列
 */
static bool mig_table_has_column(sqlite3 *db, const char *table, const char *column)
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

/**
 * @brief 幂等添加列（已存在则跳过），返回变更行数
 */
static heapstore_error_t mig_add_column(sqlite3 *db, const char *table,
                                        const char *column, const char *column_def,
                                        uint64_t *records_affected)
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

    /* 统计现有记录数作为受影响记录数（默认值已由 ADD COLUMN DEFAULT 生效） */
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
 * @brief 通过重建表方式移除列（兼容 SQLite < 3.35 无 DROP COLUMN）
 *
 * 仅在目标列存在时执行；重建后恢复同名表与索引。
 */
static heapstore_error_t mig_drop_columns(sqlite3 *db, const char *table,
                                          const char *const *drop_columns,
                                          size_t drop_count, uint64_t *records_affected)
{
    *records_affected = 0;

    /* 收集保留列 */
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
    snprintf(sql, sizeof(sql), "CREATE TABLE %s_tmp AS SELECT %s FROM %s;", table, keep_cols, table);
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

    /* 恢复索引 */
    if (strcmp(table, "agents") == 0) {
        sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_agent_type ON agents(type);",
                     NULL, NULL, NULL);
    } else if (strcmp(table, "sessions") == 0) {
        sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_session_user ON sessions(user_id);",
                     NULL, NULL, NULL);
    }

    rc = sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    /* 统计受影响行数（迁移后剩余记录） */
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

/**
 * @brief v1.0.0 → v1.1.0: 为 agent_record 新增 priority 和 tags 字段
 *
 * 真实实现：在 registry 数据库的 agents 表中追加 priority/tags 列
 * （幂等：列已存在则跳过），并为存量记录应用默认值。
 */
static heapstore_error_t migrate_v1_0_to_v1_1_agent_fields(uint64_t *records_affected)
{
    *records_affected = 0;

#ifdef AIRY_HAS_SQLITE3
    /* 无数据库文件时视为空库，无需迁移 */
    char db_path[heapstore_MAX_PATH_LEN];
    mig_db_path(db_path, sizeof(db_path));
    struct stat st;
    if (stat(db_path, &st) != 0) {
        return heapstore_SUCCESS;
    }

    /* 备份原始数据库 */
    heapstore_error_t err = backup_data_file(db_path);
    if (err != heapstore_SUCCESS) {
        return err;
    }

    sqlite3 *db = mig_db_open();
    if (!db) {
        restore_data_file(db_path);
        return heapstore_ERR_DB_INIT_FAILED;
    }

    uint64_t affected = 0;
    uint64_t step_affected = 0;
    err = mig_add_column(db, "agents", "priority", "priority INTEGER DEFAULT 0",
                         &step_affected);
    if (err != heapstore_SUCCESS) {
        sqlite3_close(db);
        restore_data_file(db_path);
        return err;
    }
    affected += step_affected;
    err = mig_add_column(db, "agents", "tags", "tags TEXT DEFAULT ''", &step_affected);
    if (err != heapstore_SUCCESS) {
        sqlite3_close(db);
        restore_data_file(db_path);
        return err;
    }
    affected += step_affected;

    sqlite3_close(db);
    cleanup_backup_file(db_path);
    *records_affected = affected;
    return heapstore_SUCCESS;
#else
    /* 非 SQLite 构建：内存链表实现无持久化 schema，迁移为空操作 */
    return heapstore_SUCCESS;
#endif
}

/**
 * @brief v1.1.0 → v2.0.0: 为 session_record 新增 metadata 字段
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

    heapstore_error_t err = backup_data_file(db_path);
    if (err != heapstore_SUCCESS) {
        return err;
    }

    sqlite3 *db = mig_db_open();
    if (!db) {
        restore_data_file(db_path);
        return heapstore_ERR_DB_INIT_FAILED;
    }

    uint64_t affected = 0;
    err = mig_add_column(db, "sessions", "metadata", "metadata TEXT DEFAULT ''", &affected);
    if (err != heapstore_SUCCESS) {
        sqlite3_close(db);
        restore_data_file(db_path);
        return err;
    }

    sqlite3_close(db);
    cleanup_backup_file(db_path);
    *records_affected = affected;
    return heapstore_SUCCESS;
#else
    return heapstore_SUCCESS;
#endif
}

/* ---- 迁移步骤注册表 ---- */

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

static const size_t g_forward_step_count =
    sizeof(g_forward_steps) / sizeof(g_forward_steps[0]);

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
        /* 无需迁移 */
        if (report) {
            __builtin_memset(report, 0, sizeof(*report));
            report->from_version = current_ver;
            report->to_version = current_ver;
            report->direction = HEAPSTORE_MIGRATE_FORWARD;
            report->success = true;
        }
        return heapstore_SUCCESS;
    }

    /* 收集需要执行的迁移步骤 */
    migration_step_def_t *applicable_steps[HEAPSTORE_MIGRATION_MAX_STEPS];
    size_t step_count = 0;

    for (size_t i = 0; i < g_forward_step_count && step_count < HEAPSTORE_MIGRATION_MAX_STEPS;
         i++) {
        if (g_forward_steps[i].from_version >= current_ver &&
            g_forward_steps[i].to_version <= target_version) {
            applicable_steps[step_count++] = (migration_step_def_t *)&g_forward_steps[i];
        }
    }

    /* 初始化报告 */
    if (report) {
        __builtin_memset(report, 0, sizeof(*report));
        report->from_version = current_ver;
        report->to_version = target_version;
        report->direction = HEAPSTORE_MIGRATE_FORWARD;
        report->step_count = step_count;
        report->steps = (heapstore_migration_step_t *)AIRY_MALLOC(
            step_count * sizeof(heapstore_migration_step_t));
        if (report->steps) {
            __builtin_memset(report->steps, 0, step_count * sizeof(heapstore_migration_step_t));
        }
    }

    uint64_t total_start = get_time_ms();
    bool all_success = true;

    /* 执行迁移步骤 */
    for (size_t i = 0; i < step_count; i++) {
        uint64_t step_start = get_time_ms();
        uint64_t records = 0;

        heapstore_error_t step_err = applicable_steps[i]->execute(&records);
        uint64_t step_duration = get_time_ms() - step_start;

        if (report && report->steps) {
            AIRY_STRNCPY_TERM(report->steps[i].name, applicable_steps[i]->name,
                                 sizeof(report->steps[i].name));
            report->steps[i].result = step_err;
            report->steps[i].records_affected = records;
            report->steps[i].duration_ms = step_duration;
        }

        if (step_err != heapstore_SUCCESS) {
            all_success = false;
            /* 迁移失败：停止执行后续步骤，但不回滚已执行的步骤 */
            break;
        }
    }

    if (all_success) {
        /* 所有步骤成功，更新版本号 */
        err = heapstore_migration_set_version(target_version);
        if (err != heapstore_SUCCESS) {
            all_success = false;
        }
    }

    if (report) {
        report->success = all_success;
        report->total_duration_ms = get_time_ms() - total_start;
    }

    return all_success ? heapstore_SUCCESS : heapstore_ERR_INTERNAL;
}

/* ========== P3.20.3: 后向兼容回滚 ========== */

/**
 * @brief v2.0.0 → v1.1.0: 移除 session metadata 字段
 *
 * 真实实现：通过重建 sessions 表移除 metadata 列，保留核心字段。
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

    heapstore_error_t err = backup_data_file(db_path);
    if (err != heapstore_SUCCESS) {
        return err;
    }

    sqlite3 *db = mig_db_open();
    if (!db) {
        restore_data_file(db_path);
        return heapstore_ERR_DB_INIT_FAILED;
    }

    static const char *drop_cols[] = {"metadata"};
    uint64_t affected = 0;
    err = mig_drop_columns(db, "sessions", drop_cols, 1, &affected);
    if (err != heapstore_SUCCESS) {
        sqlite3_close(db);
        restore_data_file(db_path);
        return err;
    }

    sqlite3_close(db);
    cleanup_backup_file(db_path);
    *records_affected = affected;
    return heapstore_SUCCESS;
#else
    return heapstore_SUCCESS;
#endif
}

/**
 * @brief v1.1.0 → v1.0.0: 移除 agent priority 和 tags 字段
 *
 * 真实实现：通过重建 agents 表移除 priority/tags 列，保留核心字段。
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

    heapstore_error_t err = backup_data_file(db_path);
    if (err != heapstore_SUCCESS) {
        return err;
    }

    sqlite3 *db = mig_db_open();
    if (!db) {
        restore_data_file(db_path);
        return heapstore_ERR_DB_INIT_FAILED;
    }

    static const char *drop_cols[] = {"priority", "tags"};
    uint64_t affected = 0;
    err = mig_drop_columns(db, "agents", drop_cols, 2, &affected);
    if (err != heapstore_SUCCESS) {
        sqlite3_close(db);
        restore_data_file(db_path);
        return err;
    }

    sqlite3_close(db);
    cleanup_backup_file(db_path);
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

static const size_t g_rollback_step_count =
    sizeof(g_rollback_steps) / sizeof(g_rollback_steps[0]);

heapstore_error_t heapstore_migration_rollback(uint32_t target_version,
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

    if (current_ver <= target_version) {
        if (report) {
            __builtin_memset(report, 0, sizeof(*report));
            report->from_version = current_ver;
            report->to_version = current_ver;
            report->direction = HEAPSTORE_MIGRATE_BACKWARD;
            report->success = true;
        }
        return heapstore_SUCCESS;
    }

    /* 收集需要执行的回滚步骤 */
    migration_step_def_t *applicable_steps[HEAPSTORE_MIGRATION_MAX_STEPS];
    size_t step_count = 0;

    for (size_t i = 0; i < g_rollback_step_count && step_count < HEAPSTORE_MIGRATION_MAX_STEPS;
         i++) {
        if (g_rollback_steps[i].from_version <= current_ver &&
            g_rollback_steps[i].to_version >= target_version) {
            applicable_steps[step_count++] = (migration_step_def_t *)&g_rollback_steps[i];
        }
    }

    if (report) {
        __builtin_memset(report, 0, sizeof(*report));
        report->from_version = current_ver;
        report->to_version = target_version;
        report->direction = HEAPSTORE_MIGRATE_BACKWARD;
        report->step_count = step_count;
        report->steps = (heapstore_migration_step_t *)AIRY_MALLOC(
            step_count * sizeof(heapstore_migration_step_t));
        if (report->steps) {
            __builtin_memset(report->steps, 0, step_count * sizeof(heapstore_migration_step_t));
        }
    }

    uint64_t total_start = get_time_ms();
    bool all_success = true;

    for (size_t i = 0; i < step_count; i++) {
        uint64_t step_start = get_time_ms();
        uint64_t records = 0;

        heapstore_error_t step_err = applicable_steps[i]->execute(&records);
        uint64_t step_duration = get_time_ms() - step_start;

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
        err = heapstore_migration_set_version(target_version);
        if (err != heapstore_SUCCESS) {
            all_success = false;
        }
    }

    if (report) {
        report->success = all_success;
        report->total_duration_ms = get_time_ms() - total_start;
    }

    return all_success ? heapstore_SUCCESS : heapstore_ERR_INTERNAL;
}

/* ========== 工具函数 ========== */

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

    /* 根据 record_type 返回对应的字段列表 */
    /* 这些定义应与 heapstore_types.h 中的结构体定义保持同步 */
    static const char *agent_fields[] = {
        "id", "name", "type", "version", "status", "config_path",
        "created_at", "updated_at",
        /* v1.1.0 新增 */
        "priority", "tags",
        NULL
    };

    static const char *session_fields[] = {
        "id", "user_id", "created_at", "last_active_at", "ttl_seconds", "status",
        /* v2.0.0 新增 */
        "metadata",
        NULL
    };

    static const char *skill_fields[] = {
        "id", "name", "version", "library_path", "manifest_path", "installed_at",
        NULL
    };

    static const char *memory_pool_fields[] = {
        "pool_id", "name", "total_size", "used_size", "block_size",
        "block_count", "free_block_count", "created_at", "status",
        NULL
    };

    static const char *memory_alloc_fields[] = {
        "allocation_id", "pool_id", "size", "address",
        "allocated_at", "freed_at", "status",
        NULL
    };

    static const char *ipc_channel_fields[] = {
        "channel_id", "name", "type", "status",
        "created_at", "last_activity_at", "buffer_size", "current_usage",
        NULL
    };

    static const char *ipc_buffer_fields[] = {
        "buffer_id", "channel_id", "size", "used", "created_at", "status",
        NULL
    };

    static const char *span_fields[] = {
        "trace_id", "span_id", "parent_span_id", "name", "kind",
        "start_time_ns", "end_time_ns", "service_name", "status",
        NULL
    };

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

    /* 计算字段数量 */
    size_t count = 0;
    while (selected[count] != NULL) {
        count++;
    }

    /* 分配并复制字段名 */
    char **out = (char **)AIRY_MALLOC((count + 1) * sizeof(char *));
    if (!out) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < count; i++) {
        out[i] = (char *)AIRY_MALLOC(strlen(selected[i]) + 1);
        if (!out[i]) {
            /* 释放已分配的内存 */
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