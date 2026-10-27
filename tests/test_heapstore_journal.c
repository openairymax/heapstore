// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file test_heapstore_journal.c
 * @brief Journal 六域单元测试：单条/批量写入、聚合语义、事务回滚、计数与裁剪。
 */

// @owner: team-B

#include "heapstore.h"
#include "heapstore_journal.h"
#include "airy_memory.h"

#include <assert.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JTEST_ROOT "hs_journal_test"
#define JTEST_DB JTEST_ROOT "/registry/registry.db"

/* 测试隔离：JTEST_ROOT 是 cwd 相对目录，上一次运行残留的 journal 行会让
 * UNIQUE(session_id, turn_number) 在首条插入即冲突，产生与产品无关的假失败。
 * 只清除 registry 库文件及其 WAL 影子文件，其余目录由 heapstore_init 重建。 */
static void journal_wipe(void)
{
    static const char *const files[] = {JTEST_DB, JTEST_DB "-wal", JTEST_DB "-shm"};
    size_t i;

    for (i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        (void)remove(files[i]);
    }
}

static void journal_boot(void)
{
    heapstore_config_t cfg = heapstore_def_config(JTEST_ROOT);

    journal_wipe();
    assert(heapstore_init(&cfg) == heapstore_SUCCESS);
    assert(heapstore_registry_init() == heapstore_SUCCESS);
}

static void journal_down(void)
{
    heapstore_registry_shutdown();
    heapstore_shutdown();
}

static sqlite3 *journal_open_db(void)
{
    sqlite3 *db = NULL;
    assert(sqlite3_open(JTEST_DB, &db) == SQLITE_OK);
    return db;
}

static long long journal_scalar(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *stmt = NULL;
    assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    long long value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

static void journal_turn(heapstore_turn_rec_t *rec, const char *id, const char *session,
                         int32_t number)
{
    AIRY_MEMSET(rec, 0, sizeof(*rec));
    snprintf(rec->id, sizeof(rec->id), "%s", id);
    snprintf(rec->session_id, sizeof(rec->session_id), "%s", session);
    rec->turn_number = number;
    snprintf(rec->role, sizeof(rec->role), "assistant");
    snprintf(rec->content, sizeof(rec->content), "content of %s", id);
    rec->tokens_in = 12;
    rec->tokens_out = 34;
    rec->created_at = 1790000000;
}

static void journal_cost(heapstore_cost_rec_t *rec, int64_t tokens_in, double cost)
{
    AIRY_MEMSET(rec, 0, sizeof(*rec));
    snprintf(rec->id, sizeof(rec->id), "cost_%lld", (long long)tokens_in);
    snprintf(rec->agent_id, sizeof(rec->agent_id), "agent_main");
    snprintf(rec->provider, sizeof(rec->provider), "local");
    snprintf(rec->model, sizeof(rec->model), "llama3");
    snprintf(rec->period_start, sizeof(rec->period_start), "2026-10-27");
    snprintf(rec->period_end, sizeof(rec->period_end), "2026-10-27");
    rec->tokens_in = tokens_in;
    rec->tokens_out = 5;
    rec->cost_usd = cost;
    rec->budget_limit = 0.0;
    rec->created_at = 1790000000;
}

static void test_journal_single_domains(void)
{
    printf("Test: journal_single_domains...");

    heapstore_turn_rec_t turn;
    heapstore_ckpt_rec_t ckpt;
    heapstore_llm_call_rec_t llm;
    heapstore_tool_call_rec_t tool;
    heapstore_event_rec_t event;
    uint64_t count = 0;

    journal_turn(&turn, "turn_a", "sess_main", 1);
    assert(hs_jour_add(heapstore_JOUR_TURNS, &turn) == heapstore_SUCCESS);

    AIRY_MEMSET(&ckpt, 0, sizeof(ckpt));
    snprintf(ckpt.id, sizeof(ckpt.id), "ckpt_a");
    snprintf(ckpt.session_id, sizeof(ckpt.session_id), "sess_main");
    ckpt.turn_number = 1;
    snprintf(ckpt.state, sizeof(ckpt.state), "{\"step\":1}");
    snprintf(ckpt.checksum, sizeof(ckpt.checksum), "deadbeef");
    ckpt.size_bytes = 11;
    assert(hs_jour_add(heapstore_JOUR_CKPTS, &ckpt) == heapstore_SUCCESS);

    AIRY_MEMSET(&llm, 0, sizeof(llm));
    snprintf(llm.id, sizeof(llm.id), "llm_a");
    snprintf(llm.turn_id, sizeof(llm.turn_id), "turn_a");
    snprintf(llm.provider, sizeof(llm.provider), "local");
    snprintf(llm.model, sizeof(llm.model), "llama3");
    snprintf(llm.request_json, sizeof(llm.request_json), "{\"q\":\"hi\"}");
    snprintf(llm.response_json, sizeof(llm.response_json), "{\"a\":\"hello\"}");
    llm.tokens_in = 7;
    llm.tokens_out = 9;
    llm.cost_usd = 0.0012;
    llm.latency_ms = 42;
    snprintf(llm.status, sizeof(llm.status), "ok");
    assert(hs_jour_add(heapstore_JOUR_LLM, &llm) == heapstore_SUCCESS);

    AIRY_MEMSET(&tool, 0, sizeof(tool));
    snprintf(tool.id, sizeof(tool.id), "tool_a");
    snprintf(tool.turn_id, sizeof(tool.turn_id), "turn_a");
    snprintf(tool.tool_name, sizeof(tool.tool_name), "fs_read");
    snprintf(tool.arguments, sizeof(tool.arguments), "{\"path\":\"/etc/hostname\"}");
    snprintf(tool.result, sizeof(tool.result), "{\"len\":12}");
    tool.approved = 1;
    snprintf(tool.safety_check, sizeof(tool.safety_check), "{\"verdict\":\"allow\"}");
    tool.duration_ms = 3;
    snprintf(tool.status, sizeof(tool.status), "done");
    assert(hs_jour_add(heapstore_JOUR_TOOLS, &tool) == heapstore_SUCCESS);

    AIRY_MEMSET(&event, 0, sizeof(event));
    snprintf(event.event_type, sizeof(event.event_type), "evolution.tick");
    snprintf(event.source, sizeof(event.source), "orchestrator");
    snprintf(event.severity, sizeof(event.severity), "INFO");
    snprintf(event.payload, sizeof(event.payload), "{\"gen\":3}");
    assert(hs_jour_add(heapstore_JOUR_EVENTS, &event) == heapstore_SUCCESS);

    assert(hs_jour_count(heapstore_JOUR_TURNS, &count) == heapstore_SUCCESS);
    assert(count == 1);
    assert(hs_jour_count(heapstore_JOUR_CKPTS, &count) == heapstore_SUCCESS);
    assert(count == 1);
    assert(hs_jour_count(heapstore_JOUR_LLM, &count) == heapstore_SUCCESS);
    assert(count == 1);
    assert(hs_jour_count(heapstore_JOUR_TOOLS, &count) == heapstore_SUCCESS);
    assert(count == 1);
    assert(hs_jour_count(heapstore_JOUR_EVENTS, &count) == heapstore_SUCCESS);
    assert(count == 1);

    printf("PASS\n");
}

static void test_journal_batch(void)
{
    printf("Test: journal_batch...");

    heapstore_turn_rec_t turns[3];
    uint64_t count = 0;

    journal_turn(&turns[0], "turn_b1", "sess_batch", 1);
    journal_turn(&turns[1], "turn_b2", "sess_batch", 2);
    journal_turn(&turns[2], "turn_b3", "sess_batch", 3);

    assert(hs_jour_add_n(heapstore_JOUR_TURNS, turns, 3) == heapstore_SUCCESS);
    assert(hs_jour_count(heapstore_JOUR_TURNS, &count) == heapstore_SUCCESS);
    assert(count == 4);

    printf("PASS\n");
}

static void test_journal_batch_rollback(void)
{
    printf("Test: journal_batch_rollback...");

    heapstore_turn_rec_t turns[3];
    uint64_t count = 0;

    journal_turn(&turns[0], "turn_c1", "sess_rb", 1);
    journal_turn(&turns[1], "turn_c2", "sess_rb", 2);
    /* 第三条与已存在记录 (sess_batch, 3) 冲突，整批必须回滚。 */
    journal_turn(&turns[2], "turn_b3_dup", "sess_batch", 3);

    assert(hs_jour_add_n(heapstore_JOUR_TURNS, turns, 3) != heapstore_SUCCESS);
    assert(hs_jour_count(heapstore_JOUR_TURNS, &count) == heapstore_SUCCESS);
    assert(count == 4);

    printf("PASS\n");
}

static void test_journal_cost_upsert(void)
{
    printf("Test: journal_cost_upsert...");

    heapstore_cost_rec_t first;
    heapstore_cost_rec_t second;
    uint64_t count = 0;
    sqlite3 *db = journal_open_db();

    journal_cost(&first, 100, 0.5);
    assert(hs_jour_add(heapstore_JOUR_COSTS, &first) == heapstore_SUCCESS);
    journal_cost(&second, 40, 0.25);
    assert(hs_jour_add(heapstore_JOUR_COSTS, &second) == heapstore_SUCCESS);

    assert(hs_jour_count(heapstore_JOUR_COSTS, &count) == heapstore_SUCCESS);
    assert(count == 1);
    assert(journal_scalar(db, "SELECT tokens_in FROM cost_tracking;") == 140);
    assert(journal_scalar(db, "SELECT tokens_out FROM cost_tracking;") == 10);
    assert(journal_scalar(db, "SELECT COUNT(*) FROM cost_tracking WHERE cost_usd > 0.74 AND"
                              " cost_usd < 0.76;") == 1);

    sqlite3_close(db);
    printf("PASS\n");
}

static void test_journal_trim(void)
{
    printf("Test: journal_trim...");

    uint64_t count = 0;
    sqlite3 *db = journal_open_db();

    assert(hs_jour_trim(heapstore_JOUR_TURNS, 2) == heapstore_SUCCESS);
    assert(hs_jour_count(heapstore_JOUR_TURNS, &count) == heapstore_SUCCESS);
    assert(count == 2);
    /* 保留最新行：批次写入的 turn_number 3 与 2 仍在，最旧的 1 被裁掉。 */
    assert(journal_scalar(db, "SELECT COUNT(*) FROM turns WHERE turn_number >= 2;") == 2);

    assert(hs_jour_trim(heapstore_JOUR_EVENTS, 0) == heapstore_SUCCESS);
    assert(hs_jour_count(heapstore_JOUR_EVENTS, &count) == heapstore_SUCCESS);
    assert(count == 0);

    sqlite3_close(db);
    printf("PASS\n");
}

static void test_journal_invalid_params(void)
{
    printf("Test: journal_invalid_params...");

    heapstore_turn_rec_t turn;
    uint64_t count = 0;

    journal_turn(&turn, "turn_bad", "sess_bad", 1);

    assert(hs_jour_add(heapstore_JOUR_MAX, &turn) == heapstore_ERR_INVALID_PARAM);
    assert(hs_jour_add(heapstore_JOUR_TURNS, NULL) == heapstore_ERR_INVALID_PARAM);
    assert(hs_jour_add_n(heapstore_JOUR_TURNS, &turn, 0) == heapstore_ERR_INVALID_PARAM);
    assert(hs_jour_add_n(heapstore_JOUR_TURNS, NULL, 1) == heapstore_ERR_INVALID_PARAM);
    assert(hs_jour_count(heapstore_JOUR_MAX, &count) == heapstore_ERR_INVALID_PARAM);
    assert(hs_jour_count(heapstore_JOUR_TURNS, NULL) == heapstore_ERR_INVALID_PARAM);
    assert(hs_jour_trim(heapstore_JOUR_MAX, 1) == heapstore_ERR_INVALID_PARAM);

    printf("PASS\n");
}

int main(void)
{
    printf("=== AgentRT heapstore Journal Unit Tests ===\n\n");

    journal_boot();

    test_journal_single_domains();
    test_journal_batch();
    test_journal_batch_rollback();
    test_journal_cost_upsert();
    test_journal_trim();
    test_journal_invalid_params();

    journal_down();

    printf("\n=== All Journal Tests Passed ===\n");
    return 0;
}
