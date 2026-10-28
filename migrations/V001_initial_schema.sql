-- Migration: V001
-- Description: AgentRT Heapstore Initial Schema
-- Created: 2026-06-16T00:00:00Z
-- Forward-only: true
--
-- 本文件是 heapstore 唯一 schema 真相源（SSoT）：构建期由 CMake
-- 读取并注入生成 heapstore_schema.h，运行时 registry 初始化直接执行
-- 该文本，不再在 C 源码内嵌 DDL 副本。
--
-- 语法口径为 SQLite（实际运行后端）：
--   UUID        -> TEXT
--   JSONB       -> TEXT
--   TIMESTAMPTZ -> INTEGER（Unix 秒）
--   BIGSERIAL   -> INTEGER PRIMARY KEY AUTOINCREMENT
--   NUMERIC     -> REAL
-- 前三张表（agents / agent_capabilities / skills）与 sessions 表的列定义
-- 已由现网数据库承载，务必逐字保持，否则行提取列序会错位。

-- ===========================================================================
-- Agent 注册表
-- ===========================================================================
CREATE TABLE IF NOT EXISTS agents (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    type TEXT,
    version TEXT,
    status TEXT,
    config_path TEXT,
    created_at INTEGER,
    updated_at INTEGER,
    priority INTEGER DEFAULT 0,
    tags TEXT DEFAULT ''
);

CREATE TABLE IF NOT EXISTS agent_capabilities (
    agent_id TEXT,
    capability TEXT,
    FOREIGN KEY (agent_id) REFERENCES agents(id)
);

CREATE TABLE IF NOT EXISTS skills (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    version TEXT,
    library_path TEXT,
    manifest_path TEXT,
    installed_at INTEGER
);

CREATE TABLE IF NOT EXISTS sessions (
    id TEXT PRIMARY KEY,
    user_id TEXT,
    created_at INTEGER,
    last_active_at INTEGER,
    ttl_seconds INTEGER,
    status TEXT,
    metadata TEXT DEFAULT ''
);

CREATE INDEX IF NOT EXISTS idx_agent_type ON agents(type);
CREATE INDEX IF NOT EXISTS idx_skill_status ON skills(name);
CREATE INDEX IF NOT EXISTS idx_session_user ON sessions(user_id);

-- ===========================================================================
-- Agent 对话轮次表（journal，追加写）
-- ===========================================================================
CREATE TABLE IF NOT EXISTS turns (
    id TEXT PRIMARY KEY,
    session_id TEXT NOT NULL,
    turn_number INTEGER NOT NULL,
    role TEXT NOT NULL,
    content TEXT,
    tool_calls TEXT,
    tool_results TEXT,
    tokens_in INTEGER DEFAULT 0,
    tokens_out INTEGER DEFAULT 0,
    cost_usd REAL DEFAULT 0,
    duration_ms INTEGER DEFAULT 0,
    created_at INTEGER,
    UNIQUE(session_id, turn_number)
);

CREATE INDEX IF NOT EXISTS idx_turns_session ON turns(session_id);
CREATE INDEX IF NOT EXISTS idx_turns_created ON turns(created_at);

-- ===========================================================================
-- Checkpoint / 状态快照表（journal，追加写）
-- ===========================================================================
CREATE TABLE IF NOT EXISTS checkpoints (
    id TEXT PRIMARY KEY,
    session_id TEXT NOT NULL,
    turn_number INTEGER NOT NULL,
    state TEXT NOT NULL,
    checksum TEXT NOT NULL,
    size_bytes INTEGER DEFAULT 0,
    created_at INTEGER,
    UNIQUE(session_id, turn_number)
);

CREATE INDEX IF NOT EXISTS idx_checkpoints_session ON checkpoints(session_id);

-- ===========================================================================
-- LLM 调用日志表（journal，追加写）
-- ===========================================================================
CREATE TABLE IF NOT EXISTS llm_calls (
    id TEXT PRIMARY KEY,
    turn_id TEXT,
    provider TEXT NOT NULL,
    model TEXT NOT NULL,
    request_json TEXT,
    response_json TEXT,
    tokens_in INTEGER DEFAULT 0,
    tokens_out INTEGER DEFAULT 0,
    cost_usd REAL DEFAULT 0,
    latency_ms INTEGER DEFAULT 0,
    status TEXT NOT NULL DEFAULT 'ok',
    error_message TEXT,
    created_at INTEGER
);

CREATE INDEX IF NOT EXISTS idx_llm_calls_turn ON llm_calls(turn_id);
CREATE INDEX IF NOT EXISTS idx_llm_calls_created ON llm_calls(created_at);
CREATE INDEX IF NOT EXISTS idx_llm_calls_provider ON llm_calls(provider);
CREATE INDEX IF NOT EXISTS idx_llm_calls_model ON llm_calls(model);

-- ===========================================================================
-- 工具调用日志表（journal，追加写）
-- ===========================================================================
CREATE TABLE IF NOT EXISTS tool_calls (
    id TEXT PRIMARY KEY,
    turn_id TEXT,
    tool_name TEXT NOT NULL,
    arguments TEXT,
    result TEXT,
    approved INTEGER NOT NULL DEFAULT 0,
    safety_check TEXT,
    duration_ms INTEGER DEFAULT 0,
    status TEXT NOT NULL DEFAULT 'pending',
    error_message TEXT,
    created_at INTEGER
);

CREATE INDEX IF NOT EXISTS idx_tool_calls_turn ON tool_calls(turn_id);
CREATE INDEX IF NOT EXISTS idx_tool_calls_tool ON tool_calls(tool_name);
CREATE INDEX IF NOT EXISTS idx_tool_calls_created ON tool_calls(created_at);

-- ===========================================================================
-- 事件日志表（journal，追加写）
-- ===========================================================================
CREATE TABLE IF NOT EXISTS events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    event_type TEXT NOT NULL,
    source TEXT NOT NULL,
    severity TEXT NOT NULL DEFAULT 'INFO',
    payload TEXT,
    created_at INTEGER
);

CREATE INDEX IF NOT EXISTS idx_events_type ON events(event_type);
CREATE INDEX IF NOT EXISTS idx_events_created ON events(created_at);
CREATE INDEX IF NOT EXISTS idx_events_severity ON events(severity);

-- ===========================================================================
-- 成本追踪表（按 agent/provider/model/period 聚合并累加，非追加写）
-- ===========================================================================
CREATE TABLE IF NOT EXISTS cost_tracking (
    id TEXT PRIMARY KEY,
    agent_id TEXT NOT NULL DEFAULT '',
    provider TEXT NOT NULL,
    model TEXT NOT NULL,
    tokens_in INTEGER DEFAULT 0,
    tokens_out INTEGER DEFAULT 0,
    cost_usd REAL DEFAULT 0,
    period_start TEXT NOT NULL,
    period_end TEXT NOT NULL,
    budget_limit REAL,
    created_at INTEGER,
    UNIQUE(agent_id, provider, model, period_start)
);

CREATE INDEX IF NOT EXISTS idx_cost_agent ON cost_tracking(agent_id);
CREATE INDEX IF NOT EXISTS idx_cost_period ON cost_tracking(period_start, period_end);

-- ===========================================================================
-- 反思批判表（journal，追加写）：反思管线的评估分数与理由
-- ===========================================================================
CREATE TABLE IF NOT EXISTS critiques (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    turn_id TEXT NOT NULL,
    atom TEXT NOT NULL,
    score REAL NOT NULL DEFAULT 0,
    reason TEXT,
    created_at INTEGER
);

CREATE INDEX IF NOT EXISTS idx_critiques_turn ON critiques(turn_id);
CREATE INDEX IF NOT EXISTS idx_critiques_created ON critiques(created_at);
