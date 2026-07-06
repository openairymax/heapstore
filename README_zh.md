**语言:** [English](README.md) | 简体中文

# Airymax Heapstore — 运行时数据存储

`agentrt/heapstore/`

**版本：** 0.1.1
**许可证：** AGPL-3.0-or-later OR Apache-2.0（双许可证）
**分支：** `feature/official-hubs-01`

---

## 1. 模块定位

Heapstore 是 Airymax 智能体运行时的**运行时数据持久化层**（A 类）。
它持久化运行时产生的全部数据——系统日志、Agent/Skill/Session 注册信息、
分布式链路追踪、内存池分配记录、Token 计数、IPC 缓冲区、批量写入——
通过 **混合存储引擎**（SQLite 可用时 + 内存后端）承载。

Heapstore 围绕**快速/慢速双路径**写入模型设计：快速路径无锁异步，面向
高频写入；慢速路径同步执行，带完整参数校验、超时与 trace_id 传播。内置
**熔断器**在连续失败时切断写入以防级联故障；**事务性批量写入**在高负载下
摊薄 I/O 开销。

设计目标：

- **高吞吐写入** —— 快速/慢速双路径针对高频日志优化，最小化写入延迟。
- **混合存储** —— SQLite + 内存后端；SQLite 不可用时自动回退。
- **数据可观测** —— 为每个子引擎提供完整统计、性能指标与健康检查。
- **低开销** —— 快速路径无锁写入，最小化对主业务逻辑的影响。
- **熔断保护** —— 内置熔断器在重复写入失败时自动切断，防止级联故障。
- **批量优化** —— 事务性批量写入减少高频场景下的 I/O 开销。

---

## 2. 目录结构

```
heapstore/
├── CMakeLists.txt                       # CMake 构建配置
├── README.md                            # 英文版
├── README_zh.md                         # 本文件（中文）
├── LICENSE                              # 双许可证文本（AGPL-3.0 + Apache-2.0）
├── NOTICE                               # 版权声明
├── include/                             # 公共头文件
│   ├── heapstore.h                      # 核心接口（初始化/关闭/统计/熔断/批量）
│   ├── heapstore_types.h                # 共享类型定义（打破循环依赖）
│   ├── heapstore_log.h                  # 日志管理接口
│   ├── heapstore_registry.h             # 注册表接口（Agent/Skill/Session）
│   ├── heapstore_trace.h                # 链路追踪存储接口
│   ├── heapstore_memory.h               # 内存管理数据存储接口
│   ├── heapstore_token.h                # Token 计数与预算接口
│   ├── heapstore_batch.h                # 批量写入接口
│   ├── heapstore_ipc.h                  # IPC 数据存储接口
│   ├── heapstore_integration.h          # 集成测试接口
│   └── utils.h                          # 内部工具函数
├── src/                                 # 源代码实现
│   ├── private.h                        # 内部私有头文件
│   ├── heapstore_core.c                 # 核心（初始化/路径/统计/熔断器）
│   ├── heapstore_log.c                  # 日志系统
│   ├── heapstore_registry.c             # 注册表（SQLite + 内存回退）
│   ├── heapstore_trace.c                # 链路追踪
│   ├── heapstore_memory.c               # 内存数据存储
│   ├── heapstore_token.c                # Token 计数
│   ├── heapstore_batch.c                # 批量写入（链表缓冲 + 事务提交）
│   ├── heapstore_ipc.c                  # IPC 数据存储
│   ├── heapstore_integration.c          # 集成层
│   ├── heapstore_migration.c            # Schema 迁移
│   └── utils.c                          # 工具函数
├── kernel/                              # 内核级服务
│   ├── services/
│   │   ├── log_store_service.c          # 日志存储内核服务
│   │   └── trace_store_service.c        # 追踪存储内核服务
│   ├── ipc/                             # IPC 内核数据（channels/、binder/）
│   └── memory/                          # 内存内核数据（patterns/、index/、meta/、raw/）
├── services/                            # 各 Daemon 数据目录（market_d、tool_d、llm_d）
├── migrations/                          # Schema 迁移脚本
├── tests/                               # 测试套件
│   ├── test_heapstore_core.c            # 核心测试
│   ├── test_heapstore_log.c             # 日志测试
│   ├── test_heapstore_registry.c        # 注册表测试
│   ├── test_heapstore_trace.c           # 追踪测试
│   ├── test_heapstore_memory.c          # 内存存储测试
│   ├── test_heapstore_ipc.c             # IPC 存储测试
│   ├── test_heapstore_batch.c           # 批量写入测试
│   ├── test_heapstore_integration.c     # 集成测试
│   ├── test_batch_performance.c         # 批量性能测试
│   ├── test_security_path_traversal.c   # 路径遍历安全测试
│   ├── test_fuzzing_concurrency.c       # 并发模糊测试
│   ├── test_edge_cases.c                # 边界情况测试
│   └── benchmark_heapstore.c            # 性能基准
├── examples/                            # 示例（quick_start.c、batch_write.c）
└── scripts/                             # 运维脚本（性能回归、版本标签）
```

### 7 大存储引擎

| 存储引擎 | 源文件 | 后端 | 用途 |
|----------|--------|------|------|
| **heapstore_core** | `heapstore_core.c` | LMDB/SQLite/Redis | 核心初始化、路径管理、统计、熔断器 |
| **heapstore_log** | `heapstore_log.c` | SQLite | 系统日志持久化，轮转与清理 |
| **heapstore_registry** | `heapstore_registry.c` | SQLite | Agent/Skill/Session 注册信息，迭代查询 |
| **heapstore_trace** | `heapstore_trace.c` | SQLite | 分布式链路追踪 Span 存储，导出 |
| **heapstore_memory** | `heapstore_memory.c` | LMDB | 内存池与分配记录，读写性能优先 |
| **heapstore_token** | `heapstore_token.c` | SQLite | Token 使用统计与预算管理 |
| **heapstore_batch** | `heapstore_batch.c` | LMDB | 批量数据写入，链表缓冲 + 事务提交 |

### 条件编译

| 依赖 | 条件宏 | 缺失时行为 |
|------|--------|-----------|
| SQLite3 | `AGENTRT_HAS_SQLITE3` | 注册表回退到内存后端（功能完整但进程退出后数据丢失） |

---

## 3. 上游 / 下游依赖关系

### 上游（Heapstore 依赖）

| 依赖 | 必需 | 用途 |
|------|------|------|
| **commons** | 是 | 公共工具——`platform/include`、`utils/include`、`utils/sync/include`、`utils/compat/include`；并链接 `agentrt_common` 静态库 |
| **atoms** | 是（逻辑） | 提供 CoreKern IPC 缓冲区原语（heapstore IPC 数据存储对其镜像），以及 Syscall 表面触发 `syscall/session.c`、`syscall/telemetry.c` 中的 `BUILD_HEAPSTORE` 代码路径 |
| SQLite3 | 否 | 注册表/日志/追踪/Token 持久化后端；缺失时回退到内存 |
| Threads::Threads | 是 | 多线程写入路径 |
| agentrt_compile_defs | 是 | 伞仓编译定义 |

### 下游（消费 Heapstore）

| 消费者 | 用途 |
|--------|------|
| **daemons** | 12 个守护进程通过 heapstore 持久化状态——`market_d`/`tool_d`/`llm_d` 有专用数据目录；注册表追踪 Agent/Skill/Session；Token 引擎预算 LLM 用量 |
| **gateway** | 网关通过 `heapstore_log` 与 `heapstore_trace` 写入访问日志与请求追踪；熔断器在高负载下保护网关写入 |

---

## 4. 核心机制

### 快速/慢速双路径写入

| 路径 | 函数 | 特点 |
|------|------|------|
| **快速路径** | `heapstore_log_write_fast()` | 无锁异步写入，适用于高频日志，熔断器打开时返回错误 |
| **慢速路径** | `heapstore_log_write_slow()` | 同步写入，完整参数验证和错误处理，支持超时和 trace_id |

### 熔断器

```c
typedef enum {
    HEAPSTORE_CIRCUIT_CLOSED    = 0,  // 正常状态
    HEAPSTORE_CIRCUIT_OPEN,           // 熔断器打开（拒绝写入）
    HEAPSTORE_CIRCUIT_HALF_OPEN       // 半开状态（试探性恢复）
} heapstore_circuit_state_t;
```

- 连续失败次数超过 `circuit_breaker_threshold` 时熔断器打开。
- 超时后进入半开状态，允许少量请求试探。
- 可通过 `heapstore_reset_circuit()` 手动重置。

### 公共 API 摘要

| 函数 | 说明 |
|------|------|
| `heapstore_init(config)` | 初始化数据分区（必须首先调用） |
| `heapstore_shutdown()` | 关闭并清理资源 |
| `heapstore_get_stats(stats)` | 磁盘使用统计 |
| `heapstore_get_metrics(metrics)` | 性能指标 |
| `heapstore_health_check(...)` | 5 个子系统健康检查 |
| `heapstore_get_circuit_state(info)` | 获取熔断器状态 |
| `heapstore_reset_circuit()` | 手动重置熔断器 |
| `heapstore_batch_begin(size)` / `heapstore_batch_commit(ctx)` / `heapstore_batch_rollback(ctx)` | 事务性批量写入 |
| `heapstore_log_write_fast()` / `heapstore_log_write_slow()` | 快速/慢速日志写入 |
| `heapstore_registry_add_agent()` / `heapstore_registry_get_agent()` / `heapstore_registry_query_agents()` | Agent 注册表 CRUD |
| `heapstore_token_record()` / `heapstore_token_check_budget()` | Token 追踪与预算控制 |

便捷宏：`HEAPSTORE_LOG_ERROR` / `HEAPSTORE_LOG_WARN` /
`HEAPSTORE_LOG_INFO` / `HEAPSTORE_LOG_DEBUG`。

---

## 5. 构建说明

```bash
# 标准构建（在伞仓根目录或本仓独立构建）
cmake -B build -DBUILD_TESTS=ON
cmake --build build --target agentrt_heapstore

# 运行 heapstore 测试
ctest --test-dir build -R heapstore

# 运行性能基准
./build/benchmark_heapstore

# 运行示例
./build/quick_start
./build/batch_write
```

### CMake 选项

| 选项 | 默认 | 说明 |
|------|------|------|
| `BUILD_TESTS` | `ON` | 构建测试套件（单元/集成/模糊/基准） |
| `AGENTRT_HAS_SQLITE3` | 自动 | 由伞仓 CMake 自动检测，门控 SQLite 后端 |

### 构建产物

- `agentrt_heapstore` —— 包含所有存储引擎的静态库
- 公共头文件安装到 `include/agentrt/heapstore`

### 安装

```bash
cmake --install build --prefix /opt/airymax
```

### 配置示例

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

## 6. 许可证

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

本模块采用双许可证，您可以选择以下任一许可证遵守：

- **GNU Affero General Public License v3.0 or later**
  ([AGPL-3.0-or-later](https://www.gnu.org/licenses/agpl-3.0.txt))，或
- **Apache License, Version 2.0**
  ([Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0.txt))

SPDX-License-Identifier: `AGPL-3.0-or-later OR Apache-2.0`

完整许可证文本见 [LICENSE](LICENSE) 文件，版权声明见 [NOTICE](NOTICE)。
默认适用 AGPL-3.0-or-later 条款；Apache-2.0 备选用于 AGPL 无法覆盖的
下游集成场景（如闭源或专有分发）。
