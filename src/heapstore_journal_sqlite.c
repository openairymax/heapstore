// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_journal_sqlite.c
 * @brief Journal SQLite backend: descriptor-driven write, count and trim for
 *        the seven journal domains.
 */

// @owner: team-B
#include "heapstore_journal.h"
#include "heapstore_registry_internal.h"

#ifdef heapstore_SQLITE_IMPLEMENTATION

/** 单字段绑定类型：定长文本 / 32 位整型 / 64 位整型 / 浮点。 */
enum {
    JF_TXT = 0,
    JF_I32,
    JF_I64,
    JF_F64
};

typedef struct {
    uint16_t off;
    uint8_t kind;
} jfield_t;

/** 域描述符：写入 SQL、目标表名、字段表；六域共用一套绑定与事务机制。 */
typedef struct {
    const char *write_sql;
    const char *table;
    const jfield_t *fields;
    uint8_t nfields;
} jdom_desc_t;

static const jfield_t s_turn_f[] = {
    {offsetof(heapstore_turn_rec_t, id), JF_TXT},
    {offsetof(heapstore_turn_rec_t, session_id), JF_TXT},
    {offsetof(heapstore_turn_rec_t, turn_number), JF_I32},
    {offsetof(heapstore_turn_rec_t, role), JF_TXT},
    {offsetof(heapstore_turn_rec_t, content), JF_TXT},
    {offsetof(heapstore_turn_rec_t, tool_calls), JF_TXT},
    {offsetof(heapstore_turn_rec_t, tool_results), JF_TXT},
    {offsetof(heapstore_turn_rec_t, tokens_in), JF_I32},
    {offsetof(heapstore_turn_rec_t, tokens_out), JF_I32},
    {offsetof(heapstore_turn_rec_t, cost_usd), JF_F64},
    {offsetof(heapstore_turn_rec_t, duration_ms), JF_I32},
    {offsetof(heapstore_turn_rec_t, created_at), JF_I64},
};

static const jfield_t s_ckpt_f[] = {
    {offsetof(heapstore_ckpt_rec_t, id), JF_TXT},
    {offsetof(heapstore_ckpt_rec_t, session_id), JF_TXT},
    {offsetof(heapstore_ckpt_rec_t, turn_number), JF_I32},
    {offsetof(heapstore_ckpt_rec_t, state), JF_TXT},
    {offsetof(heapstore_ckpt_rec_t, checksum), JF_TXT},
    {offsetof(heapstore_ckpt_rec_t, size_bytes), JF_I32},
    {offsetof(heapstore_ckpt_rec_t, created_at), JF_I64},
};

static const jfield_t s_llm_f[] = {
    {offsetof(heapstore_llm_call_rec_t, id), JF_TXT},
    {offsetof(heapstore_llm_call_rec_t, turn_id), JF_TXT},
    {offsetof(heapstore_llm_call_rec_t, provider), JF_TXT},
    {offsetof(heapstore_llm_call_rec_t, model), JF_TXT},
    {offsetof(heapstore_llm_call_rec_t, request_json), JF_TXT},
    {offsetof(heapstore_llm_call_rec_t, response_json), JF_TXT},
    {offsetof(heapstore_llm_call_rec_t, tokens_in), JF_I32},
    {offsetof(heapstore_llm_call_rec_t, tokens_out), JF_I32},
    {offsetof(heapstore_llm_call_rec_t, cost_usd), JF_F64},
    {offsetof(heapstore_llm_call_rec_t, latency_ms), JF_I32},
    {offsetof(heapstore_llm_call_rec_t, status), JF_TXT},
    {offsetof(heapstore_llm_call_rec_t, error_message), JF_TXT},
    {offsetof(heapstore_llm_call_rec_t, created_at), JF_I64},
};

static const jfield_t s_tool_f[] = {
    {offsetof(heapstore_tool_call_rec_t, id), JF_TXT},
    {offsetof(heapstore_tool_call_rec_t, turn_id), JF_TXT},
    {offsetof(heapstore_tool_call_rec_t, tool_name), JF_TXT},
    {offsetof(heapstore_tool_call_rec_t, arguments), JF_TXT},
    {offsetof(heapstore_tool_call_rec_t, result), JF_TXT},
    {offsetof(heapstore_tool_call_rec_t, approved), JF_I32},
    {offsetof(heapstore_tool_call_rec_t, safety_check), JF_TXT},
    {offsetof(heapstore_tool_call_rec_t, duration_ms), JF_I32},
    {offsetof(heapstore_tool_call_rec_t, status), JF_TXT},
    {offsetof(heapstore_tool_call_rec_t, error_message), JF_TXT},
    {offsetof(heapstore_tool_call_rec_t, created_at), JF_I64},
};

static const jfield_t s_event_f[] = {
    {offsetof(heapstore_event_rec_t, event_type), JF_TXT},
    {offsetof(heapstore_event_rec_t, source), JF_TXT},
    {offsetof(heapstore_event_rec_t, severity), JF_TXT},
    {offsetof(heapstore_event_rec_t, payload), JF_TXT},
    {offsetof(heapstore_event_rec_t, created_at), JF_I64},
};

static const jfield_t s_cost_f[] = {
    {offsetof(heapstore_cost_rec_t, id), JF_TXT},
    {offsetof(heapstore_cost_rec_t, agent_id), JF_TXT},
    {offsetof(heapstore_cost_rec_t, provider), JF_TXT},
    {offsetof(heapstore_cost_rec_t, model), JF_TXT},
    {offsetof(heapstore_cost_rec_t, tokens_in), JF_I64},
    {offsetof(heapstore_cost_rec_t, tokens_out), JF_I64},
    {offsetof(heapstore_cost_rec_t, cost_usd), JF_F64},
    {offsetof(heapstore_cost_rec_t, period_start), JF_TXT},
    {offsetof(heapstore_cost_rec_t, period_end), JF_TXT},
    {offsetof(heapstore_cost_rec_t, budget_limit), JF_F64},
    {offsetof(heapstore_cost_rec_t, created_at), JF_I64},
};

static const jfield_t s_critique_f[] = {
    {offsetof(heapstore_critique_rec_t, turn_id), JF_TXT},
    {offsetof(heapstore_critique_rec_t, atom), JF_TXT},
    {offsetof(heapstore_critique_rec_t, score), JF_F64},
    {offsetof(heapstore_critique_rec_t, reason), JF_TXT},
    {offsetof(heapstore_critique_rec_t, created_at), JF_I64},
};

static const jdom_desc_t s_doms[heapstore_JOUR_MAX] = {
    {"INSERT INTO turns (id, session_id, turn_number, role, content, tool_calls,"
     " tool_results, tokens_in, tokens_out, cost_usd, duration_ms, created_at)"
     " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
     "turns", s_turn_f, (uint8_t)(sizeof(s_turn_f) / sizeof(s_turn_f[0]))},
    {"INSERT INTO checkpoints (id, session_id, turn_number, state, checksum,"
     " size_bytes, created_at) VALUES (?, ?, ?, ?, ?, ?, ?);",
     "checkpoints", s_ckpt_f, (uint8_t)(sizeof(s_ckpt_f) / sizeof(s_ckpt_f[0]))},
    {"INSERT INTO llm_calls (id, turn_id, provider, model, request_json, response_json,"
     " tokens_in, tokens_out, cost_usd, latency_ms, status, error_message, created_at)"
     " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
     "llm_calls", s_llm_f, (uint8_t)(sizeof(s_llm_f) / sizeof(s_llm_f[0]))},
    {"INSERT INTO tool_calls (id, turn_id, tool_name, arguments, result, approved,"
     " safety_check, duration_ms, status, error_message, created_at)"
     " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
     "tool_calls", s_tool_f, (uint8_t)(sizeof(s_tool_f) / sizeof(s_tool_f[0]))},
    {"INSERT INTO events (event_type, source, severity, payload, created_at)"
     " VALUES (?, ?, ?, ?, ?);",
     "events", s_event_f, (uint8_t)(sizeof(s_event_f) / sizeof(s_event_f[0]))},
    /* cost_tracking 为聚合表：同键累加 token 与费用，预算仅在给出正值时覆盖。 */
    {"INSERT INTO cost_tracking (id, agent_id, provider, model, tokens_in, tokens_out,"
     " cost_usd, period_start, period_end, budget_limit, created_at)"
     " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
     " ON CONFLICT(agent_id, provider, model, period_start) DO UPDATE SET"
     " tokens_in = cost_tracking.tokens_in + excluded.tokens_in,"
     " tokens_out = cost_tracking.tokens_out + excluded.tokens_out,"
     " cost_usd = cost_tracking.cost_usd + excluded.cost_usd,"
     " period_end = excluded.period_end,"
     " budget_limit = CASE WHEN excluded.budget_limit > 0"
     " THEN excluded.budget_limit ELSE cost_tracking.budget_limit END;",
     "cost_tracking", s_cost_f, (uint8_t)(sizeof(s_cost_f) / sizeof(s_cost_f[0]))},
    {"INSERT INTO critiques (turn_id, atom, score, reason, created_at)"
     " VALUES (?, ?, ?, ?, ?);",
     "critiques", s_critique_f, (uint8_t)(sizeof(s_critique_f) / sizeof(s_critique_f[0]))},
};

static const jdom_desc_t *j_dom(heapstore_jour_type_t type)
{
    if ((unsigned int)type >= (unsigned int)heapstore_JOUR_MAX) {
        return NULL;
    }
    return &s_doms[type];
}

static void j_bind(sqlite3_stmt *stmt, const jdom_desc_t *dom, const void *rec)
{
    const char *base = (const char *)rec;

    for (uint8_t i = 0; i < dom->nfields; i++) {
        const jfield_t *field = &dom->fields[i];
        const void *slot = base + field->off;

        switch (field->kind) {
        case JF_TXT:
            sqlite3_bind_text(stmt, (int)i + 1, (const char *)slot, -1, SQLITE_STATIC);
            break;
        case JF_I32:
            sqlite3_bind_int(stmt, (int)i + 1, *(const int32_t *)slot);
            break;
        case JF_I64:
            sqlite3_bind_int64(stmt, (int)i + 1, (sqlite3_int64)*(const int64_t *)slot);
            break;
        default:
            sqlite3_bind_double(stmt, (int)i + 1, *(const double *)slot);
            break;
        }
    }
}

heapstore_error_t hs_jour_add(heapstore_jour_type_t type, const void *rec)
{
    const jdom_desc_t *dom = j_dom(type);
    if (!dom || !rec) {
        return heapstore_ERR_INVALID_PARAM;
    }

    heapstore_error_t err = heapstore_SUCCESS;
    sqlite3_stmt *stmt = hs_stmt_begin(dom->write_sql, &err);
    if (!stmt) {
        return err;
    }

    j_bind(stmt, dom, rec);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    airy_mtx_unlock(&s_registry.lock);

    return (rc == SQLITE_DONE) ? heapstore_SUCCESS : heapstore_ERR_DB_QUERY_FAILED;
}

heapstore_error_t hs_jour_add_n(heapstore_jour_type_t type, const void *recs, size_t n)
{
    const jdom_desc_t *dom = j_dom(type);
    if (!dom || !recs || n == 0) {
        return heapstore_ERR_INVALID_PARAM;
    }

    heapstore_error_t err = heapstore_SUCCESS;
    sqlite3_stmt *stmt = hs_stmt_begin(dom->write_sql, &err);
    if (!stmt) {
        return err;
    }

    if (sqlite3_exec(s_registry.db, "BEGIN TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        airy_mtx_unlock(&s_registry.lock);
        return heapstore_ERR_DB_QUERY_FAILED;
    }

    /* 记录尺寸由域记录类型确定：描述符只描述字段，故按域取步长。 */
    size_t step = 0;
    switch (type) {
    case heapstore_JOUR_TURNS:
        step = sizeof(heapstore_turn_rec_t);
        break;
    case heapstore_JOUR_CKPTS:
        step = sizeof(heapstore_ckpt_rec_t);
        break;
    case heapstore_JOUR_LLM:
        step = sizeof(heapstore_llm_call_rec_t);
        break;
    case heapstore_JOUR_TOOLS:
        step = sizeof(heapstore_tool_call_rec_t);
        break;
    case heapstore_JOUR_EVENTS:
        step = sizeof(heapstore_event_rec_t);
        break;
    case heapstore_JOUR_CRITIQUES:
        step = sizeof(heapstore_critique_rec_t);
        break;
    default:
        step = sizeof(heapstore_cost_rec_t);
        break;
    }

    heapstore_error_t result = heapstore_SUCCESS;
    const char *base = (const char *)recs;
    for (size_t i = 0; i < n; i++) {
        j_bind(stmt, dom, base + i * step);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            result = heapstore_ERR_DB_QUERY_FAILED;
            sqlite3_exec(s_registry.db, "ROLLBACK;", NULL, NULL, NULL);
            break;
        }
        sqlite3_reset(stmt);
    }

    if (result == heapstore_SUCCESS &&
        sqlite3_exec(s_registry.db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
        result = heapstore_ERR_DB_QUERY_FAILED;
    }

    sqlite3_finalize(stmt);
    airy_mtx_unlock(&s_registry.lock);
    return result;
}

static void extract_u64(sqlite3_stmt *stmt, void *out)
{
    *(uint64_t *)out = (uint64_t)sqlite3_column_int64(stmt, 0);
}

heapstore_error_t hs_jour_count(heapstore_jour_type_t type, uint64_t *out)
{
    const jdom_desc_t *dom = j_dom(type);
    if (!dom || !out) {
        return heapstore_ERR_INVALID_PARAM;
    }

    char sql[96];
    if (snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s;", dom->table) < 0) {
        return heapstore_ERR_INVALID_PARAM;
    }

    return registry_query_one(sql, NULL, NULL, extract_u64, out);
}

static heapstore_error_t bind_keep(sqlite3_stmt *stmt, void *data)
{
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)*(const size_t *)data);
    return heapstore_SUCCESS;
}

heapstore_error_t hs_jour_trim(heapstore_jour_type_t type, size_t keep)
{
    const jdom_desc_t *dom = j_dom(type);
    if (!dom) {
        return heapstore_ERR_INVALID_PARAM;
    }

    char sql[160];
    if (snprintf(sql, sizeof(sql),
                 "DELETE FROM %s WHERE rowid NOT IN"
                 " (SELECT rowid FROM %s ORDER BY rowid DESC LIMIT ?);",
                 dom->table, dom->table) < 0) {
        return heapstore_ERR_INVALID_PARAM;
    }

    return sql_exec_locked(sql, bind_keep, &keep);
}

#endif /* heapstore_SQLITE_IMPLEMENTATION */
