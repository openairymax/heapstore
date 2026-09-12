# heapstore — Heap-Backed Runtime Data Storage

> Persists everything the runtime produces: logs, registries, traces, memory records, token counts, IPC state, and batched writes.

**Language:** English | [简体中文](README_zh.md)

[![Version](https://img.shields.io/badge/version-0.1.15-5a6b7e)](https://atomgit.com/openairymax/heapstore)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)
[![C11](https://img.shields.io/badge/C-11-00599C?logo=c&logoColor=white)](https://en.cppreference.com/w/c/11)

- **Repository:** <https://atomgit.com/openairymax/heapstore>

---

## What It Is

**heapstore** is the runtime data persistence library of the Airymax agent runtime (AgentRT). It persists what a running system produces — system logs, agent/skill/session registries, trace spans, memory-pool accounting, token usage and budgets, IPC channel/buffer state, and transactional batch writes — behind one unified C API.

The storage model is deliberately pragmatic:

- **Registry** uses SQLite when it is available at build time and falls back to a fully functional in-memory backend otherwise (no persistence across process exit).
- **Logs** are plain append files partitioned by date and by service, with rotation and retention cleanup.
- **Traces** are buffered in memory and flushed as span files under the data root, with JSON export.
- **Tokens, memory records, and batch contexts** are in-process structures protected by locks or atomics.
- **IPC state** combines POSIX shared memory with file persistence under the data root.

A **fast / slow dual-path** write model keeps high-frequency logging off the critical path (lock-free asynchronous writes) while important writes go through a synchronous path with full validation, timeout, and trace-id propagation. A **circuit breaker** (CLOSED → OPEN → HALF_OPEN) rejects writes after consecutive failures instead of letting faults cascade.

heapstore is consumed as the static library `airy_heapstore`, linked by the gateway and the runtime daemons.

## Capabilities

| Capability | Entry points |
|------------|--------------|
| Partitioned data root with managed directory layout | `heapstore_init` / `heapstore_get_path` / `heapstore_get_full_path` |
| Fast (async, lock-free) and slow (sync, validated) log writes | `heapstore_log_write_fast` / `heapstore_log_write_slow`, `heapstore_log_write` |
| Agent / Skill / Session registry with iterative queries | `heapstore_registry_*` |
| Trace span storage, batch flush, JSON export | `heapstore_trace_*` |
| Memory pool / allocation accounting | `heapstore_memory_*` |
| Token usage counters and per-task budgets | `heapstore_token_record` / `heapstore_token_set_budget` / `heapstore_token_check_budget` |
| Transactional batch writes for logs, spans, records | `heapstore_batch_begin` / `heapstore_batch_add_*` / `heapstore_batch_commit` |
| IPC channel/buffer records over shared memory + files | `heapstore_ipc_*` |
| Circuit breaker with state inspection and manual reset | `heapstore_get_circuit_state` / `heapstore_reset_circuit` |
| Usage statistics and performance metrics | `heapstore_get_stats` / `heapstore_get_metrics` |
| Health check across five subsystems | `heapstore_health_check` |
| Schema migration (forward / rollback) for the SQLite registry | `heapstore_migration_*`, `migrations/*.sql` |

## Composition

### Storage engines

| Engine | Source | Backend | Purpose |
|--------|--------|---------|---------|
| **core** | `heapstore_core*.c` | in-process state | Init, path layout, stats, metrics, errors, circuit breaker, async writes |
| **log** | `heapstore_log.c`, `kernel/services/log_store_service.c` | date-partitioned files | Log persistence, per-service files, rotation, retention cleanup |
| **registry** | `heapstore_registry*.c` | SQLite, in-memory fallback | Agent/Skill/Session CRUD and iterative queries |
| **trace** | `heapstore_trace.c`, `kernel/services/trace_store_service.c` | in-memory buffer + span files | Span persistence, time-range/trace queries, JSON export |
| **memory** | `heapstore_memory.c` | in-memory tables | Memory-pool and allocation records |
| **token** | `heapstore_token.c` | in-memory atomics | Token usage stats, per-task budgets (up to 1024 tasks) |
| **batch** | `heapstore_core_batch.c` | in-memory buffer | Batched add/commit/rollback across engines |
| **ipc** | `heapstore_ipc*.c` | shared memory + files | Durable IPC channel/buffer state |
| **migration** | `heapstore_migration*.c` | SQLite + version file | Schema upgrades and rollback (V001, V002) |
| **integration** | `heapstore_integration.c` | data + metadata files | End-to-end write/read flows |

### Repository layout

```
heapstore/
├── CMakeLists.txt            # Defines the airy_heapstore static library
├── include/                  # Public headers (heapstore.h and per-engine APIs)
├── src/                      # Engine implementations (+ *_internal.h private headers)
├── kernel/
│   ├── services/             # Kernel-level log/trace store services (built into the lib)
│   └── README.md
├── services/README.md        # Per-service data directory layout (created at runtime)
├── migrations/               # SQL schema scripts (V001 initial, V002 tags/retry, rollback)
├── tests/                    # 8 ctest suites + benchmark
├── examples/                 # quick_start.c, batch_write.c
└── scripts/                  # Performance regression detector, @since tag tool
```

### Runtime data layout

`heapstore_init()` creates the following structure under the data root (see [Configuration](#configuration)):

```
<root>/
├── logs/{apps,kernel,services}/   # date- and service-partitioned log files
├── registry/                      # SQLite database (when compiled with SQLite)
├── traces/spans/                  # flushed trace span files
├── services/{llm_d,market_d,tool_d}/  # per-daemon data directories
└── kernel/
    ├── ipc/{channels,buffers}/    # IPC state persistence
    └── memory/{pools,allocations,stats,index,meta,patterns,raw}/
```

## Usage

```c
#include "heapstore.h"

heapstore_config_t config = {
    .root_path                = "./heapstore_data",
    .max_log_size_mb          = 100,
    .log_retention_days       = 7,
    .trace_retention_days     = 3,
    .circuit_breaker_threshold = 5,
    .circuit_breaker_timeout_sec = 30,
};
heapstore_init(&config);                        /* pass NULL for defaults */

heapstore_log_write_fast("my_service", 1, "hot-path message");
heapstore_log_write_slow("my_service", 1, "important message",
                         "trace-001", 1000);    /* sync, validated */

heapstore_batch_context_t *ctx = heapstore_batch_begin(1024);
heapstore_batch_add_log(ctx, "my_service", 1, "batched entry");
heapstore_batch_commit(ctx);
heapstore_batch_context_destroy(ctx);

heapstore_shutdown();
```

Core API surface (`include/heapstore.h`):

| Function | Description |
|----------|-------------|
| `heapstore_init(config)` / `heapstore_shutdown()` | Lifecycle; init must be called first (`NULL` config = defaults) |
| `heapstore_ready()` / `heapstore_get_root()` | State and data-root inspection |
| `heapstore_get_path()` / `heapstore_get_full_path()` | Managed sub-paths |
| `heapstore_log_write_fast()` / `heapstore_log_write_slow()` | Dual-path log writes |
| `heapstore_batch_begin/add_*/commit/rollback/context_destroy` | Transactional batch writes |
| `heapstore_get_stats()` / `heapstore_get_metrics()` / `heapstore_reset_metrics()` | Statistics |
| `heapstore_health_check()` | Health of registry/trace/log/ipc/memory |
| `heapstore_get_circuit_state()` / `heapstore_reset_circuit()` | Circuit breaker |
| `heapstore_cleanup()` / `heapstore_flush()` | Retention cleanup and forced flush |
| `heapstore_strerror()` | Error code to description |
| `heapstore_reload_config()` | Hot config update |

Per-engine APIs live in `heapstore_registry.h`, `heapstore_trace.h`, `heapstore_log.h`, `heapstore_memory.h`, `heapstore_token.h`, `heapstore_batch.h`, `heapstore_ipc.h`, `heapstore_integration.h`, `heapstore_migration.h`. Convenience macros: `HEAPSTORE_LOG_ERROR` / `_WARN` / `_INFO` / `_DEBUG`.

Error codes are a single enum (`heapstore_error_t`, `heapstore_SUCCESS` = 0 down to `heapstore_ERR_CIRCUIT_OPEN` = -15 … `heapstore_ERR_INTERNAL` = -99).

## Build

heapstore builds as part of an [AgentRT](https://atomgit.com/openairymax/agentrt) source tree (out-of-source):

```bash
cmake -S agentrt -B build -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_HEAPSTORE=ON -DBUILD_TESTS=ON
cmake --build build --target airy_heapstore --parallel
```

Tests and tools:

```bash
ctest --test-dir build -R heapstore --output-on-failure   # 8 suites
./build/tests/heapstore_benchmark                         # performance benchmark
```

The examples (`examples/quick_start.c`, `examples/batch_write.c`) ship their
own `CMakeLists.txt` defining `quick_start` and `batch_write` targets that
link `airy_heapstore`. They are illustrative sources: they are not wired into
the default AgentRT build tree, and their code predates the current header
signatures, so they need adaptation before compiling. See
[`examples/README.md`](examples/README.md).

**CMake options:**

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_HEAPSTORE` | `ON` | Build the heapstore module |
| `BUILD_TESTS` | `ON` | Build the test suites |
| `BUILD_BENCHMARK` | `ON` | Build `heapstore_benchmark` (defined in `tests/`) |
| `AIRY_HAS_SQLITE3` | auto | Set from `find_package(SQLite3)`; see below |

**Conditional compilation:**

| Dependency | Macro | Behavior when absent |
|------------|-------|----------------------|
| SQLite3 | `AIRY_HAS_SQLITE3` | Registry and migrations use the in-memory backend; all APIs stay functional, data is not persisted across process exit |

**Artifacts:** `airy_heapstore` static library; public headers install under
`include/agentrt/heapstore`.

## Configuration

Configuration is passed as the `heapstore_config_t` struct (there is no
config file):

| Field | Default | Meaning |
|-------|---------|---------|
| `root_path` | resolved (see below) | Data root directory |
| `max_log_size_mb` | 100 | Log size limit before rotation |
| `log_retention_days` | 7 | Log cleanup retention |
| `trace_retention_days` | 3 | Trace cleanup retention |
| `enable_auto_cleanup` / `enable_log_rotation` / `enable_trace_export` | `true` | Maintenance switches |
| `db_vacuum_interval_days` | 7 | SQLite vacuum interval |
| `circuit_breaker_threshold` | 5 | Consecutive failures before the breaker opens |
| `circuit_breaker_timeout_sec` | 30 | Time before an open breaker half-opens for probing |

Data root resolution order:

1. `root_path` in the config struct;
2. `AIRY_HEAPSTORE_ROOT` environment variable;
3. `<runtime data dir>/agentrt/heapstore` from the platform data-directory helper;
4. `/tmp/agentrt/heapstore`.

## Relationships

| Side | Module | Role |
|------|--------|------|
| Upstream | [commons](https://atomgit.com/openairymax/commons) | Platform/utils/sync/compat headers and the `airy_common` static library (memory macros, atomics, mutex wrappers) |
| Upstream (optional) | SQLite3 | Persistent registry/migration backend; in-memory fallback when absent |
| Downstream | [gateway](https://atomgit.com/openairymax/gateway) | Access logs and request traces |
| Downstream | runtime daemons | Agent/Skill/Session registries, per-daemon data under `services/`, token budgets |

heapstore itself has no runtime dependencies beyond a C11 toolchain, threads,
and optionally SQLite3; it compiles on POSIX and Windows platforms.

## License

This module is dual-licensed under the terms of either:

- **GNU Affero General Public License v3.0 or later**
  ([AGPL-3.0-or-later](https://www.gnu.org/licenses/agpl-3.0.txt)), or
- **Apache License, Version 2.0**
  ([Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0.txt))

SPDX-License-Identifier: `AGPL-3.0-or-later OR Apache-2.0`

The full license texts are in the [LICENSE](LICENSE) file; the copyright and
trademark notice is in [NOTICE](NOTICE). You may select either license to
comply with.
