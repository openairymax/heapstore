# heapstore — 堆式运行时数据存储

> 持久化运行时产生的一切：日志、注册表、追踪、内存记录、令牌计数、IPC 缓冲与批量写入。
> [agentrt](../) 管理仓下的叶子仓。

**语言:** [English](README.md) | 简体中文

[![Version](https://img.shields.io/badge/version-0.1.9-5a6b7e)](https://atomgit.com/openairymax/heapstore)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)
[![C11](https://img.shields.io/badge/C-11-00599C?logo=c&logoColor=white)](https://en.cppreference.com/w/c/11)

- **仓库地址：** `git@atomgit.com:openairymax/heapstore.git`
- **分支：** `develop/hubs-01`
- **版本：** 0.1.9（与 agentrt 管理仓对齐）

---

## 概述

**heapstore** 是 Airymax 智能体运行时的**运行时数据持久化层**。它持久化运行时产生的一切——系统日志、智能体/技能/会话注册表、分布式追踪、内存池分配记录、令牌计数、IPC 缓冲、批量写入——通过结合 SQLite（可用时）与内存后端的**混合存储引擎**。

heapstore 围绕**快/慢双路径**写入模型设计：快路径无锁异步，用于高频写入；慢路径同步，带完整参数校验、超时与 trace_id 传播。内置**熔断器**在连续失败后跳闸以防止级联故障，**事务性批量写入**在负载下摊销 I/O。它暴露 7 个存储引擎（`core`、`log`、`registry`、`trace`、`memory`、`token`、`batch`）加一个 IPC 数据存储，全部在统一的 init/shutdown/stats/circuit/batch API 之后。

`heapstore` 是 [agentrt](../) 管理仓聚合的 7 个叶子仓之一，构成循环架构中的**存储层**（位于安全层 `cupolas` 和内核层 `atoms` 之上，网关层与服务层之下）。

## 模块分类

**A 类 —— 基础 / 原子层。**

heapstore 是基础持久化基底：守护进程和网关在其上构建持久状态。它依赖 `commons`（platform、utils、sync、compat，以及 `airy_common` 静态库），逻辑上依赖 `atoms`（其 Syscall session/telemetry 路径触发 `BUILD_HEAPSTORE` 代码路径，其 CoreKern IPC 缓冲原语被 IPC 数据存储镜像）。作为 A 类模块，heapstore 保证跨运行时的稳定持久化 API，在 SQLite 不可用时优雅降级（内存回退）。

## 目录结构

```
heapstore/
├── CMakeLists.txt                       # CMake 构建配置（单一静态库 airy_heapstore）
├── README.md                            # 英文版
├── README_zh.md                         # 本文件（中文）
├── LICENSE                              # 双许可证文本（AGPL-3.0 + Apache-2.0）
├── NOTICE                               # 版权声明
├── include/                             # 公共头文件
│   ├── heapstore.h                      # 核心 API（init/shutdown/stats/circuit/batch）
│   ├── heapstore_types.h                # 共享类型定义（打破循环依赖）
│   ├── heapstore_log.h                  # 日志管理 API
│   ├── heapstore_registry.h             # 注册表 API（Agent / Skill / Session）
│   ├── heapstore_trace.h                # 追踪 span 存储 API
│   ├── heapstore_memory.h               # 内存管理数据存储 API
│   ├── heapstore_token.h                # 令牌计数与预算 API
│   ├── heapstore_batch.h                # 批量写入 API
│   ├── heapstore_ipc.h                  # IPC 数据存储 API
│   ├── heapstore_integration.h          # 集成测试 API
│   ├── heapstore_migration.h            # Schema 迁移 API
│   └── utils.h                          # 内部工具
├── src/                                 # 源码实现
│   ├── private.h                        # 内部私有头
│   ├── heapstore_core.c                 # 核心（init / 路径 / 统计 / 熔断器）
│   ├── heapstore_log.c                  # 日志系统
│   ├── heapstore_registry.c             # 注册表（SQLite + 内存回退）
│   ├── heapstore_trace.c                # 追踪存储
│   ├── heapstore_memory.c               # 内存数据存储
│   ├── heapstore_token.c                # 令牌计数
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
├── services/                            # 各守护进程数据目录（market_d、tool_d、llm_d）
├── migrations/                          # Schema 迁移脚本
├── tests/                               # 测试套件（单元/集成/fuzz/基准）
├── examples/                            # 示例（quick_start.c、batch_write.c）
└── scripts/                             # 运维脚本（性能回归、版本标签）
```

## 核心组件

### 七大存储引擎

| 引擎 | 源文件 | 后端 | 用途 |
|------|--------|------|------|
| **heapstore_core** | `heapstore_core.c` | LMDB/SQLite/Redis | 初始化、路径管理、统计、熔断器 |
| **heapstore_log** | `heapstore_log.c` | SQLite | 系统日志持久化、轮转、清理 |
| **heapstore_registry** | `heapstore_registry.c` | SQLite | Agent/Skill/Session 注册表、迭代查询 |
| **heapstore_trace** | `heapstore_trace.c` | SQLite | 分布式追踪 span 存储、导出 |
| **heapstore_memory** | `heapstore_memory.c` | LMDB | 内存池/分配记录（读写性能） |
| **heapstore_token** | `heapstore_token.c` | SQLite | 令牌使用统计与预算管理 |
| **heapstore_batch** | `heapstore_batch.c` | LMDB | 批量写入——链表缓冲 + 事务提交 |

外加一个 **IPC 数据存储**（`heapstore_ipc.c`），镜像 CoreKern IPC 缓冲原语以持久化 IPC 状态。

### 条件编译

| 依赖 | 条件宏 | 缺失时行为 |
|------|--------|-----------|
| SQLite3 | `AIRY_HAS_SQLITE3` | 注册表回退到内存后端（功能完整但进程退出后无持久化） |

## 架构

```
┌──────────────────────────────────────────────┐
│             Applications (OpenLab)            │
├──────────────────────────────────────────────┤
│             Ecosystem (Toolkit / SDK)         │
├──────────────────────────────────────────────┤
│              守护进程服务（daemons）            │
├──────────────────────────────────────────────┤
│   网关层（gateway）                            │
├──────────────────────────────────────────────┤
│          ★ heapstore（存储层） ★              │
├──────────────────────────────────────────────┤
│   安全（cupolas）/ 内核（atoms）               │
├──────────────────────────────────────────────┤
│            commons / OS                       │
└──────────────────────────────────────────────┘

  heapstore（airy_heapstore 静态库）
  ┌────────────────────────────────────────────┐
  │  core  （init/路径/统计/熔断器）             │
  │  log   (SQLite)   registry (SQLite)        │
  │  trace (SQLite)   memory   (LMDB)          │
  │  token (SQLite)   batch    (LMDB)          │
  │  ipc   （持久化 IPC 状态）                   │
  └────────────────────────────────────────────┘
        ▲              ▲
        │              │
     daemons        gateway
   (15 个守护进程,  （访问日志,
    注册表,          请求追踪)
    令牌预算)

  写入路径：
    快路径  → 无锁异步    （高频日志）
    慢路径  → 同步 + 校验 （超时, trace_id）
    批量    → 链表缓冲 + 事务提交
```

**核心机制：** 快/慢双路径写入；熔断器（CLOSED → OPEN → HALF_OPEN）；事务性批量写入；SQLite + 内存混合，优雅回退。

## 上游依赖

> `commons` 是所有 agentrt 模块的基础库；heapstore 直接消费它并链接 `airy_common` 静态库。heapstore 还逻辑上依赖 `atoms`。

| 依赖 | 是否必需 | 用途 |
|------|----------|------|
| **commons** | 是 | 公共工具——`platform/include`、`utils/include`、`utils/sync/include`、`utils/compat/include`；同时链接 `airy_common` 静态库以获取 sync、error、types、内存宏 |
| **atoms** | 是（逻辑） | 提供 heapstore IPC 数据存储镜像的 CoreKern IPC 缓冲原语，以及触发 `syscall/session.c` 和 `syscall/telemetry.c` 中 `BUILD_HEAPSTORE` 代码路径的 Syscall 接口 |
| SQLite3 | 否 | registry/log/trace/token 的持久化后端；缺失时回退到内存（`AIRY_HAS_SQLITE3`） |
| Threads::Threads | 是 | 多线程写入路径 |
| airy_compile_defs | 是 | 伞仓编译宏（PUBLIC 链接） |

## 下游消费者

| 消费者 | 用途 |
|--------|------|
| **daemons** | 全部 15 个守护进程通过 heapstore 持久化状态——`market_d`/`tool_d`/`llm_d` 在 `services/` 下有专用数据目录；注册表跟踪 Agent/Skill/Session；令牌引擎预算 LLM 使用（`heapstore_token_check_budget`） |
| **gateway** | 网关通过 `heapstore_log` 和 `heapstore_trace` 写入访问日志和请求追踪；熔断器在负载下保护网关写入 |
| SDK 层 | SDK 消费者通过 heapstore 查询 API 读取运行时状态（注册表、追踪、令牌预算），用于可观测性和预算控制 |

## 构建

```bash
# 标准构建（源外构建，BAN-33 强制要求）
cmake -S . -B /tmp/heapstore-build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build /tmp/heapstore-build --target airy_heapstore --parallel $(nproc)

# 运行 heapstore 测试
ctest --test-dir /tmp/heapstore-build -R heapstore --output-on-failure

# 运行性能基准
/tmp/heapstore-build/benchmark_heapstore

# 运行示例
/tmp/heapstore-build/quick_start
/tmp/heapstore-build/batch_write

# 安装
cmake --install /tmp/heapstore-build --prefix /opt/airymax
```

**CMake 选项：**

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_TESTS` | `ON` | 构建测试套件（单元/集成/fuzz/基准） |
| `AIRY_HAS_SQLITE3` | 自动 | 由伞仓 CMake 自动探测；门控 SQLite 后端（回退到内存） |

**构建产物：**

- `airy_heapstore` —— 包含所有存储引擎的静态库
- 公共头文件安装到 `include/agentrt/heapstore`

**配置示例：**

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

公共 API 接口通过 `include/heapstore.h`（统一入口）及各引擎头导出。核心机制：

### 快/慢双路径写入

| 路径 | 函数 | 特性 |
|------|------|------|
| **快路径** | `heapstore_log_write_fast()` | 无锁异步写入；用于高频日志；熔断器开启时返回错误 |
| **慢路径** | `heapstore_log_write_slow()` | 同步写入；完整参数校验与错误处理；支持超时和 trace_id |

### 熔断器

```c
typedef enum {
    HEAPSTORE_CIRCUIT_CLOSED    = 0,  // 正常
    HEAPSTORE_CIRCUIT_OPEN,           // 跳闸（拒绝写入）
    HEAPSTORE_CIRCUIT_HALF_OPEN       // 探测（有限试探写入）
} heapstore_circuit_state_t;
```

- 连续失败超过 `circuit_breaker_threshold` 时跳闸。
- 超时后进入半开状态，允许试探写入。
- 通过 `heapstore_reset_circuit()` 手动重置。

### 公共 API 摘要

| 函数 | 说明 |
|------|------|
| `heapstore_init(config)` | 初始化数据分区（必须首先调用） |
| `heapstore_shutdown()` | 关闭并释放资源 |
| `heapstore_get_stats(stats)` | 磁盘使用统计 |
| `heapstore_get_metrics(metrics)` | 性能指标 |
| `heapstore_health_check(...)` | 跨 5 个子系统的健康检查 |
| `heapstore_get_circuit_state(info)` | 获取熔断器状态 |
| `heapstore_reset_circuit()` | 手动重置熔断器 |
| `heapstore_batch_begin(size)` / `heapstore_batch_commit(ctx)` / `heapstore_batch_rollback(ctx)` | 事务性批量写入 |
| `heapstore_log_write_fast()` / `heapstore_log_write_slow()` | 快/慢日志写入 |
| `heapstore_registry_add_agent()` / `heapstore_registry_get_agent()` / `heapstore_registry_query_agents()` | Agent 注册表 CRUD |
| `heapstore_token_record()` / `heapstore_token_check_budget()` | 令牌追踪与预算强制 |

便捷宏：`HEAPSTORE_LOG_ERROR` / `HEAPSTORE_LOG_WARN` / `HEAPSTORE_LOG_INFO` / `HEAPSTORE_LOG_DEBUG`。

## 许可证

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

本模块采用双许可证，您可以选择以下任一许可证遵守：

- **GNU Affero General Public License v3.0 or later**
  ([AGPL-3.0-or-later](https://www.gnu.org/licenses/agpl-3.0.txt))，或
- **Apache License, Version 2.0**
  ([Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0.txt))

SPDX-License-Identifier: `AGPL-3.0-or-later OR Apache-2.0`

完整许可证文本见 [LICENSE](LICENSE) 文件；版权声明见 [NOTICE](NOTICE)。默认适用 AGPL-3.0-or-later 条款；Apache-2.0 备选用于 AGPL 无法覆盖的下游集成场景（如闭源或专有分发）。
