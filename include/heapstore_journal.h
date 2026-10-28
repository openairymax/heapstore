// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_journal.h
 * @brief Heapstore append-only journal: turns, checkpoints, llm/tool calls,
 *        events, cost aggregation, and critiques.
 *
 * 七个 journal 域共享同一套写入/计数/裁剪机制，仅记录布局与目标表不同，
 * 因此对外只暴露四个动作：单条追加、批量追加、计数、裁剪。cost_tracking
 * 是唯一例外：按 (agent_id, provider, model, period_start) 聚合累加，
 * 其单条/批量写入语义为 upsert。
 */

#ifndef AIRY_HEAPSTORE_JOURNAL_H
#define AIRY_HEAPSTORE_JOURNAL_H

#include "heapstore.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Journal 域标识（顺序与内部描述符表一致，不可重排）。 */
typedef enum {
    heapstore_JOUR_TURNS = 0,
    heapstore_JOUR_CKPTS,
    heapstore_JOUR_LLM,
    heapstore_JOUR_TOOLS,
    heapstore_JOUR_EVENTS,
    heapstore_JOUR_COSTS,
    heapstore_JOUR_CRITIQUES,
    heapstore_JOUR_MAX
} heapstore_jour_type_t;

typedef struct {
    char id[64];
    char session_id[64];
    int32_t turn_number;
    char role[32];
    char content[1024];
    char tool_calls[512];
    char tool_results[512];
    int32_t tokens_in;
    int32_t tokens_out;
    double cost_usd;
    int32_t duration_ms;
    int64_t created_at;
} heapstore_turn_rec_t;

typedef struct {
    char id[64];
    char session_id[64];
    int32_t turn_number;
    char state[1024];
    char checksum[80];
    int32_t size_bytes;
    int64_t created_at;
} heapstore_ckpt_rec_t;

typedef struct {
    char id[64];
    char turn_id[64];
    char provider[64];
    char model[128];
    char request_json[512];
    char response_json[512];
    int32_t tokens_in;
    int32_t tokens_out;
    double cost_usd;
    int32_t latency_ms;
    char status[32];
    char error_message[256];
    int64_t created_at;
} heapstore_llm_call_rec_t;

typedef struct {
    char id[64];
    char turn_id[64];
    char tool_name[128];
    char arguments[512];
    char result[512];
    int32_t approved;
    char safety_check[256];
    int32_t duration_ms;
    char status[32];
    char error_message[256];
    int64_t created_at;
} heapstore_tool_call_rec_t;

typedef struct {
    char event_type[64];
    char source[128];
    char severity[16];
    char payload[512];
    int64_t created_at;
} heapstore_event_rec_t;

/** 成本聚合记录：period_start/period_end 为 YYYY-MM-DD（UTC）。 */
typedef struct {
    char id[64];
    char agent_id[64];
    char provider[64];
    char model[128];
    int64_t tokens_in;
    int64_t tokens_out;
    double cost_usd;
    char period_start[16];
    char period_end[16];
    double budget_limit;
    int64_t created_at;
} heapstore_cost_rec_t;

/** 反思批判记录：一次评估的分数与理由（critique 域，追加写）。 */
typedef struct {
    char turn_id[64];
    char atom[64];
    double score;
    char reason[256];
    int64_t created_at;
} heapstore_critique_rec_t;

/**
 * @brief 追加一条 journal 记录。
 *
 * @param type 目标域
 * @param rec  记录指针（按域对应的结构体解释）
 * @return heapstore_SUCCESS 或对应错误码
 *
 * @threadsafe SQLite 后端 yes；无 SQLite 内存后端 no
 */
heapstore_error_t hs_jour_add(heapstore_jour_type_t type, const void *rec);

/**
 * @brief 在一个事务内追加多条记录；任一条失败则整批回滚。
 *
 * @param type 目标域
 * @param recs 记录数组首地址
 * @param n    记录条数（0 视为参数错误）
 * @return heapstore_SUCCESS 或对应错误码
 *
 * @threadsafe SQLite 后端 yes；无 SQLite 内存后端 no
 */
heapstore_error_t hs_jour_add_n(heapstore_jour_type_t type, const void *recs, size_t n);

/**
 * @brief 统计域内当前记录条数。
 *
 * @param type 目标域
 * @param out  输出条数（非空）
 * @return heapstore_SUCCESS 或对应错误码
 *
 * @threadsafe SQLite 后端 yes；无 SQLite 内存后端 no
 */
heapstore_error_t hs_jour_count(heapstore_jour_type_t type, uint64_t *out);

/**
 * @brief 裁剪域内旧记录，仅保留最新 keep 条（keep 为 0 时清空）。
 *
 * @param type 目标域
 * @param keep 保留条数
 * @return heapstore_SUCCESS 或对应错误码
 *
 * @threadsafe SQLite 后端 yes；无 SQLite 内存后端 no
 */
heapstore_error_t hs_jour_trim(heapstore_jour_type_t type, size_t keep);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_HEAPSTORE_JOURNAL_H */
