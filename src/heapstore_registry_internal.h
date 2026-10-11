// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_registry_internal.h
 * @brief Registry internal shared definitions: SQLite internals and cross-file helpers.
 */

#ifndef AIRY_HEAPSTORE_REGISTRY_INTERNAL_H
#define AIRY_HEAPSTORE_REGISTRY_INTERNAL_H

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

#ifdef AIRY_HAS_SQLITE3
#define heapstore_SQLITE_IMPLEMENTATION
#endif

#ifdef heapstore_SQLITE_IMPLEMENTATION
#include <sqlite3.h>

typedef struct {
    sqlite3 *db;
    char db_path[512];
    airy_mtx_t lock;
    int initialized;
} registry_db_t;

/**
  * @brief Registry iterator internal structure
 */
struct heapstore_registry_iter {
    sqlite3_stmt *stmt;
    int current_type;
    int has_more;
};

extern registry_db_t s_registry;

/** 迭代器记录类型（与各实体 SELECT 列序一一对应）。 */
enum {
    heapstore_REGISTRY_REC_AGENT,
    heapstore_REGISTRY_REC_SKILL,
    heapstore_REGISTRY_REC_SESSION
};

heapstore_error_t sql_exec_locked(
    const char *sql, heapstore_error_t (*bind_func)(sqlite3_stmt *, void *), void *bind_data);

heapstore_error_t bind_agent_record(sqlite3_stmt *stmt, void *data);

/** 锁内单行查询：prepare→bind→step→ROW 时 extract 填充 out，无行返回 NOT_FOUND。 */
heapstore_error_t registry_query_one(const char *sql,
                                     heapstore_error_t (*bind_fn)(sqlite3_stmt *, void *),
                                     void *bind_data, void (*extract_fn)(sqlite3_stmt *, void *),
                                     void *out);

/** 锁内打开迭代器：prepare→bind→装配 iter（记录类型 rec_type），失败回收 stmt。 */
heapstore_error_t registry_query_open(const char *sql, int rec_type,
                                      heapstore_error_t (*bind_fn)(sqlite3_stmt *, void *),
                                      void *bind_data, heapstore_registry_iter_t **iter);

/** 锁内事务批量插入：prepare 一次，逐条 bind/step，失败 ROLLBACK，成功 COMMIT。 */
heapstore_error_t batch_exec_locked(const char *sql,
                                        heapstore_error_t (*bind_fn)(sqlite3_stmt *, void *),
                                        const void *records, size_t count, size_t elem_size);

/** 单 text 参数绑定（按 id 查删的通用 binder）。 */
heapstore_error_t registry_bind_id(sqlite3_stmt *stmt, void *id);

/** 各实体行提取：清零记录后按 SELECT 列序填充（get 与 iter_next 共用）。 */
void extract_agent_row(sqlite3_stmt *stmt, void *rec);
void extract_session_row(sqlite3_stmt *stmt, void *rec);
void extract_skill_row(sqlite3_stmt *stmt, void *rec);

/** 提取 text 列到定长缓冲：列为 NULL 时保留记录清零后的默认值。 */
void copy_text_col(sqlite3_stmt *stmt, int col, char *dst, size_t size);

/** 迭代查询前置校验：iter 非空且注册表就绪，否则返回对应错误码。 */
heapstore_error_t query_precheck(heapstore_registry_iter_t **iter);

#endif /* heapstore_SQLITE_IMPLEMENTATION */

#endif /* AIRY_HEAPSTORE_REGISTRY_INTERNAL_H */
