# heapstore — Heap-Backed Runtime Data Storage

> Persists everything the runtime produces: logs, registries, traces, memory records, token counts, IPC buffers, and batched writes.
> Leaf repository under the [agentrt](../) management repo.

**Language:** English | [简体中文](README_zh.md)

[![Version](https://img.shields.io/badge/version-0.1.9-5a6b7e)](https://atomgit.com/openairymax/heapstore)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)
[![C11](https://img.shields.io/badge/C-11-00599C?logo=c&logoColor=white)](https://en.cppreference.com/w/c/11)

- **Repository:** `git@atomgit.com:openairymax/heapstore.git`
- **Branch:** `develop/hubs-01`
- **Version:** 0.1.9 (aligned with agentrt management repo)

---

## Overview

**heapstore** is the **runtime data persistence layer** of the Airymax agent runtime. It persists everything the runtime produces while running — system logs, agent/skill/session registries, distributed traces, memory-pool allocation records, token counts, IPC buffers, and batched writes — through a **hybrid storage engine** that combines SQLite (when available) with an in-memory backend.

heapstore is designed around a **fast / slow dual-path** write model: the fast path is lock-free and asynchronous for high-frequency writes, while the slow path is synchronous with full parameter validation, timeout and trace-id propagation. A built-in **circuit breaker** trips after consecutive failures to prevent cascading faults, and **transactional batch writes** amortize I/O under load. It exposes seven storage engines (`core`, `log`, `registry`, `trace`, `memory`, `token`, `batch`) plus an IPC data store, all behind a unified init/shutdown/stats/circuit/batch API.

`heapstore` is one of the 7 leaf repositories aggregated by the [agentrt](../) management repo, forming the **Storage Layer** in the cyclic architecture (above the Security Layer `cupolas` and Kernel Layer `atoms`, below the Gateway and Service layers).

## Module Classification

**Class A — Foundational / Atomic.**

heapstore is a foundational persistence substrate: daemons and the gateway build their durable state on top of it. It depends on `commons` (platform, utils, sync, compat, plus the `airy_common` static lib) and logically on `atoms` (whose Syscall session/telemetry paths trigger `BUILD_HEAPSTORE` code-paths, and whose CoreKern IPC buffer primitives the IPC data store mirrors). As a Class-A module, heapstore guarantees a stable persistence API across the runtime and degrades gracefully (in-memory fallback) when SQLite is unavailable.

## Directory Structure

```
heapstore/
├── CMakeLists.txt                       # CMake build configuration (single static lib airy_heapstore)
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
│   ├── heapstore_migration.h            # Schema migration API
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
├── tests/                               # Test suite (unit / integration / fuzz / benchmark)
├── examples/                            # Examples (quick_start.c, batch_write.c)
└── scripts/                             # Ops scripts (perf regression, version tags)
```

## Core Components

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

Plus an **IPC data store** (`heapstore_ipc.c`) that mirrors the CoreKern IPC buffer primitives for durable IPC state.

### Conditional Compilation

| Dependency | Conditional Macro | Behavior When Missing |
|------------|-------------------|-----------------------|
| SQLite3 | `AIRY_HAS_SQLITE3` | Registry falls back to in-memory backend (full features but no persistence after process exit) |

## Architecture

```
┌──────────────────────────────────────────────┐
│             Applications (OpenLab)            │
├──────────────────────────────────────────────┤
│             Ecosystem (Toolkit / SDK)         │
├──────────────────────────────────────────────┤
│              Daemon Services (daemons)        │
├──────────────────────────────────────────────┤
│   Gateway Layer (gateway)                     │
├──────────────────────────────────────────────┤
│          ★ heapstore (Storage Layer) ★       │
├──────────────────────────────────────────────┤
│   Security (cupolas) / Kernel (atoms)         │
├──────────────────────────────────────────────┤
│            commons / OS                       │
└──────────────────────────────────────────────┘

  heapstore (airy_heapstore static lib)
  ┌────────────────────────────────────────────┐
  │  core  (init/paths/stats/circuit breaker)  │
  │  log   (SQLite)   registry (SQLite)        │
  │  trace (SQLite)   memory   (LMDB)          │
  │  token (SQLite)   batch    (LMDB)          │
  │  ipc   (durable IPC state)                 │
  └────────────────────────────────────────────┘
        ▲              ▲
        │              │
     daemons        gateway
   (15 daemons,   (access logs,
    registries,     request traces)
    token budgets)

  Write paths:
    fast path  → lock-free async   (high-frequency logs)
    slow path  → sync + validation (timeout, trace_id)
    batch      → linked-list buffer + tx commit
```

**Core mechanisms:** fast/slow dual-path writes; circuit breaker (CLOSED → OPEN → HALF_OPEN); transactional batch writes; SQLite + in-memory hybrid with graceful fallback.

## Upstream Dependencies

> `commons` is the foundation for all agentrt modules; heapstore consumes it directly and links the `airy_common` static lib. heapstore also logically depends on `atoms`.

| Dependency | Required | Purpose |
|------------|----------|---------|
| **commons** | Yes | Public utilities — `platform/include`, `utils/include`, `utils/sync/include`, `utils/compat/include`; also links `airy_common` static lib for sync, error, types, memory macros |
| **atoms** | Yes (logical) | Provides the CoreKern IPC buffer primitives that heapstore's IPC data store mirrors, and the Syscall surface that triggers `BUILD_HEAPSTORE` code-paths in `syscall/session.c` and `syscall/telemetry.c` |
| SQLite3 | No | Persistence backend for registry/log/trace/token; falls back to in-memory when absent (`AIRY_HAS_SQLITE3`) |
| Threads::Threads | Yes | Multi-threaded write paths |
| airy_compile_defs | Yes | Umbrella compile definitions (linked PUBLIC) |

## Downstream Consumers

| Consumer | What they use |
|----------|---------------|
| **daemons** | All 15 daemons persist their state through heapstore — `market_d`/`tool_d`/`llm_d` have dedicated data directories under `services/`; the registry tracks Agent/Skill/Session; the token engine budgets LLM usage (`heapstore_token_check_budget`) |
| **gateway** | Gateway writes access logs and request traces through `heapstore_log` and `heapstore_trace`; the circuit breaker protects gateway writes under load |
| SDK layer | SDK consumers read runtime state (registries, traces, token budgets) via the heapstore query APIs for observability and budgeting |

## Build

```bash
# Standard build (out-of-source, enforced by BAN-33)
cmake -S . -B /tmp/heapstore-build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build /tmp/heapstore-build --target airy_heapstore --parallel $(nproc)

# Run heapstore tests
ctest --test-dir /tmp/heapstore-build -R heapstore --output-on-failure

# Run performance benchmarks
/tmp/heapstore-build/benchmark_heapstore

# Run examples
/tmp/heapstore-build/quick_start
/tmp/heapstore-build/batch_write

# Install
cmake --install /tmp/heapstore-build --prefix /opt/airymax
```

**CMake options:**

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTS` | `ON` | Build the test suite (unit / integration / fuzz / benchmark) |
| `AIRY_HAS_SQLITE3` | auto | Auto-detected by umbrella CMake; gates SQLite backend (falls back to in-memory) |

**Build artifacts:**

- `airy_heapstore` — static library containing all storage engines
- Public headers installed under `include/agentrt/heapstore`

**Configuration example:**

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

## API

Public API surface is exported through `include/heapstore.h` (unified entry) and per-engine headers. Core mechanisms:

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

Convenience macros: `HEAPSTORE_LOG_ERROR` / `HEAPSTORE_LOG_WARN` / `HEAPSTORE_LOG_INFO` / `HEAPSTORE_LOG_DEBUG`.

## License

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

This module is dual-licensed under the terms of either:

- **GNU Affero General Public License v3.0 or later**
  ([AGPL-3.0-or-later](https://www.gnu.org/licenses/agpl-3.0.txt)), or
- **Apache License, Version 2.0**
  ([Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0.txt))

SPDX-License-Identifier: `AGPL-3.0-or-later OR Apache-2.0`

The full license texts are in the [LICENSE](LICENSE) file; the copyright notice is in [NOTICE](NOTICE). You may select either license to comply with. The AGPL-3.0-or-later terms apply by default; the Apache-2.0 alternative is provided for downstream integration scenarios (e.g., closed-source or proprietary distribution) that the AGPL does not accommodate.
