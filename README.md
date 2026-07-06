**Language:** English | [简体中文](README_zh.md)

# Airymax Heapstore — Runtime Data Storage

`agentrt/heapstore/`

**Version:** 0.1.1
**License:** AGPL-3.0-or-later OR Apache-2.0 (dual-licensed)
**Branch:** `feature/official-hubs-01`

---

## 1. Module Positioning

Heapstore is the **runtime data persistence layer** (A-tier) of the Airymax
agent runtime. It persists everything the runtime produces while running —
system logs, agent/skill/session registries, distributed traces, memory-pool
allocation records, token counts, IPC buffers, and batched writes — through a
**hybrid storage engine** that combines SQLite (when available) with an
in-memory backend.

Heapstore is designed around a **fast / slow dual-path** write model: the fast
path is lock-free and asynchronous for high-frequency writes, while the slow
path is synchronous with full parameter validation, timeout and trace-id
propagation. A built-in **circuit breaker** trips after consecutive failures
to prevent cascading faults, and **transactional batch writes** amortize I/O
under load.

Design goals:

- **High-throughput writes** — fast/slow dual path tuned for high-frequency
  logging, minimizing write latency.
- **Hybrid storage** — SQLite + in-memory backends; automatic fallback when
  SQLite is unavailable.
- **Observable** — full statistics, performance metrics and health checks for
  every sub-engine.
- **Low overhead** — fast-path writes are lock-free, minimizing impact on the
  main business logic.
- **Circuit protection** — built-in circuit breaker trips to prevent cascading
  failures on repeated write errors.
- **Batch optimization** — transactional batch writes reduce I/O overhead in
  high-frequency scenarios.

---

## 2. Directory Structure

```
heapstore/
├── CMakeLists.txt                       # CMake build configuration
├── README.md                            # This file (English)
├── README_zh.md                         # Chinese version
├── LICENSE                              # Dual license texts (AGPL-3.0 + Apache-2.0)
├── NOTICE                               # Copyright notice
├── include/                             # Public headers
│   ├── heapstore.h                      # Core API (init/shutdown/stats/circuit/batch)
│   ├── heapstore_types.h                # Shared type definitions (breaks circular deps)
│   ├── heapstore_log.h                  # Log management API
│   ├── heapstore_registry.h             # Registry API (Agent / Skill / Session)
│   ├── heapstore_trace.h                # Trace span storage API
│   ├── heapstore_memory.h               # Memory mgmt data storage API
│   ├── heapstore_token.h                # Token count and budget API
│   ├── heapstore_batch.h                # Batch write API
│   ├── heapstore_ipc.h                  # IPC data storage API
│   ├── heapstore_integration.h          # Integration test API
│   └── utils.h                          # Internal utilities
├── src/                                 # Source implementation
│   ├── private.h                        # Internal private header
│   ├── heapstore_core.c                 # Core (init / paths / stats / circuit breaker)
│   ├── heapstore_log.c                  # Log system
│   ├── heapstore_registry.c             # Registry (SQLite + in-memory fallback)
│   ├── heapstore_trace.c                # Trace storage
│   ├── heapstore_memory.c               # Memory data storage
│   ├── heapstore_token.c                # Token counting
│   ├── heapstore_batch.c                # Batch writes (linked-list buffer + tx commit)
│   ├── heapstore_ipc.c                  # IPC data storage
│   ├── heapstore_integration.c          # Integration layer
│   ├── heapstore_migration.c            # Schema migrations
│   └── utils.c                          # Utility functions
├── kernel/                              # Kernel-level services
│   ├── services/
│   │   ├── log_store_service.c          # Log store kernel service
│   │   └── trace_store_service.c        # Trace store kernel service
│   ├── ipc/                             # IPC kernel data (channels/, binder/)
│   └── memory/                          # Memory kernel data (patterns/, index/, meta/, raw/)
├── services/                            # Per-daemon data directories (market_d, tool_d, llm_d)
├── migrations/                          # Schema migration scripts
├── tests/                               # Test suite
│   ├── test_heapstore_core.c            # Core tests
│   ├── test_heapstore_log.c             # Log tests
│   ├── test_heapstore_registry.c        # Registry tests
│   ├── test_heapstore_trace.c           # Trace tests
│   ├── test_heapstore_memory.c          # Memory storage tests
│   ├── test_heapstore_ipc.c             # IPC storage tests
│   ├── test_heapstore_batch.c           # Batch write tests
│   ├── test_heapstore_integration.c     # Integration tests
│   ├── test_batch_performance.c         # Batch performance tests
│   ├── test_security_path_traversal.c   # Path-traversal security tests
│   ├── test_fuzzing_concurrency.c       # Concurrency fuzz tests
│   ├── test_edge_cases.c                # Edge-case tests
│   └── benchmark_heapstore.c            # Performance benchmarks
├── examples/                            # Examples (quick_start.c, batch_write.c)
└── scripts/                             # Ops scripts (perf regression, version tags)
```

### Seven Storage Engines

| Engine | Source | Backend | Purpose |
|--------|--------|---------|---------|
| **heapstore_core** | `heapstore_core.c` | LMDB/SQLite/Redis | Init, path mgmt, stats, circuit breaker |
| **heapstore_log** | `heapstore_log.c` | SQLite | System log persistence, rotation, cleanup |
| **heapstore_registry** | `heapstore_registry.c` | SQLite | Agent/Skill/Session registry, iterative query |
| **heapstore_trace** | `heapstore_trace.c` | SQLite | Distributed trace span storage, export |
| **heapstore_memory** | `heapstore_memory.c` | LMDB | Memory-pool / allocation records (read/write perf) |
| **heapstore_token** | `heapstore_token.c` | SQLite | Token usage stats and budget mgmt |
| **heapstore_batch** | `heapstore_batch.c` | LMDB | Batch writes — linked-list buffer + tx commit |

### Conditional Compilation

| Dependency | Conditional Macro | Behavior When Missing |
|------------|-------------------|-----------------------|
| SQLite3 | `AGENTRT_HAS_SQLITE3` | Registry falls back to in-memory backend (full features but no persistence after process exit) |

---

## 3. Upstream / Downstream Dependencies

### Upstream (Heapstore depends on)

| Dependency | Required | Purpose |
|------------|----------|---------|
| **commons** | Yes | Public utilities — `platform/include`, `utils/include`, `utils/sync/include`, `utils/compat/include`; also links `agentrt_common` static lib |
| **atoms** | Yes (logical) | Provides the CoreKern IPC buffer primitives that heapstore's IPC data store mirrors, and the Syscall surface that triggers `BUILD_HEAPSTORE` code-paths in `syscall/session.c` and `syscall/telemetry.c` |
| SQLite3 | No | Persistence backend for registry/log/trace/token; falls back to in-memory when absent |
| Threads::Threads | Yes | Multi-threaded write paths |
| agentrt_compile_defs | Yes | Umbrella compile definitions |

### Downstream (consumers of Heapstore)

| Consumer | What it uses |
|----------|--------------|
| **daemons** | All 12 daemons persist their state through heapstore — `market_d`/`tool_d`/`llm_d` have dedicated data directories; the registry tracks Agent/Skill/Session; the token engine budgets LLM usage |
| **gateway** | Gateway writes access logs and request traces through `heapstore_log` and `heapstore_trace`; the circuit breaker protects gateway writes under load |

---

## 4. Core Mechanisms

### Fast / Slow Dual-Path Writes

| Path | Function | Characteristics |
|------|----------|-----------------|
| **Fast path** | `heapstore_log_write_fast()` | Lock-free async write; for high-frequency logs; returns error when circuit breaker is open |
| **Slow path** | `heapstore_log_write_slow()` | Synchronous write; full parameter validation and error handling; supports timeout and trace_id |

### Circuit Breaker

```c
typedef enum {
    HEAPSTORE_CIRCUIT_CLOSED    = 0,  // Normal
    HEAPSTORE_CIRCUIT_OPEN,           // Tripped (rejects writes)
    HEAPSTORE_CIRCUIT_HALF_OPEN       // Probing (limited trial writes)
} heapstore_circuit_state_t;
```

- Trips when consecutive failures exceed `circuit_breaker_threshold`.
- After timeout, enters half-open state to allow trial writes.
- Manual reset via `heapstore_reset_circuit()`.

### Public API Summary

| Function | Description |
|----------|-------------|
| `heapstore_init(config)` | Initialize the data partition (must be called first) |
| `heapstore_shutdown()` | Shut down and release resources |
| `heapstore_get_stats(stats)` | Disk usage statistics |
| `heapstore_get_metrics(metrics)` | Performance metrics |
| `heapstore_health_check(...)` | Health check across 5 sub-systems |
| `heapstore_get_circuit_state(info)` | Get circuit breaker state |
| `heapstore_reset_circuit()` | Manually reset the circuit breaker |
| `heapstore_batch_begin(size)` / `heapstore_batch_commit(ctx)` / `heapstore_batch_rollback(ctx)` | Transactional batch writes |
| `heapstore_log_write_fast()` / `heapstore_log_write_slow()` | Fast/slow log writes |
| `heapstore_registry_add_agent()` / `heapstore_registry_get_agent()` / `heapstore_registry_query_agents()` | Agent registry CRUD |
| `heapstore_token_record()` / `heapstore_token_check_budget()` | Token tracking and budget enforcement |

Convenience macros: `HEAPSTORE_LOG_ERROR` / `HEAPSTORE_LOG_WARN` /
`HEAPSTORE_LOG_INFO` / `HEAPSTORE_LOG_DEBUG`.

---

## 5. Build Instructions

```bash
# Standard build (from the umbrella root, or standalone)
cmake -B build -DBUILD_TESTS=ON
cmake --build build --target agentrt_heapstore

# Run heapstore tests
ctest --test-dir build -R heapstore

# Run performance benchmarks
./build/benchmark_heapstore

# Run examples
./build/quick_start
./build/batch_write
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTS` | `ON` | Build the test suite (unit / integration / fuzz / benchmark) |
| `AGENTRT_HAS_SQLITE3` | auto | Auto-detected by umbrella CMake; gates SQLite backend |

### Build Artifacts

- `agentrt_heapstore` — static library containing all storage engines
- Public headers installed under `include/agentrt/heapstore`

### Installation

```bash
cmake --install build --prefix /opt/airymax
```

### Configuration Example

```json
{
  "heapstore": {
    "data_dir": "/var/lib/agentrt/heapstore",
    "max_log_size_mb": 100,
    "log_retention_days": 30,
    "trace_retention_days": 14,
    "enable_auto_cleanup": true,
    "enable_log_rotation": true,
    "enable_trace_export": true,
    "db_vacuum_interval_days": 7,
    "circuit_breaker_threshold": 10,
    "circuit_breaker_timeout_sec": 30
  }
}
```

---

## 6. License

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

This module is dual-licensed under the terms of either:

- **GNU Affero General Public License v3.0 or later**
  ([AGPL-3.0-or-later](https://www.gnu.org/licenses/agpl-3.0.txt)), or
- **Apache License, Version 2.0**
  ([Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0.txt))

SPDX-License-Identifier: `AGPL-3.0-or-later OR Apache-2.0`

The full license texts are in the [LICENSE](LICENSE) file; the copyright
notice is in [NOTICE](NOTICE). You may select either license to comply with.
The AGPL-3.0-or-later terms apply by default; the Apache-2.0 alternative is
provided for downstream integration scenarios (e.g., closed-source or
proprietary distribution) that the AGPL does not accommodate.
