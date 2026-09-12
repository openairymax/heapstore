# heapstore — 堆式运行时数据存储

> 持久化运行时产生的一切：日志、注册表、追踪、内存记录、令牌计数、IPC 状态与批量写入。

**语言:** [English](README.md) | 简体中文

[![Version](https://img.shields.io/badge/version-0.1.15-5a6b7e)](https://atomgit.com/openairymax/heapstore)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)
[![C11](https://img.shields.io/badge/C-11-00599C?logo=c&logoColor=white)](https://en.cppreference.com/w/c/11)

- **仓库地址：** <https://atomgit.com/openairymax/heapstore>

---

## 是什么

**heapstore** 是 Airymax 智能体运行时（AgentRT）的运行时数据持久化库。它把运行中系统产生的数据——系统日志、智能体/技能/会话注册表、追踪 span、内存池记账、令牌用量与预算、IPC 通道/缓冲状态、事务性批量写入——统一到一套 C API 之后。

存储模型刻意保持务实：

- **注册表**在构建期存在 SQLite 时使用 SQLite，否则回退到功能完整的内存后端（进程退出后不持久化）。
- **日志**是按日期、按服务划分的追加式文件，带轮转与保留期清理。
- **追踪**在内存中缓冲，刷新为数据根下的 span 文件，并支持 JSON 导出。
- **令牌、内存记录、批量上下文**是受锁或原子操作保护的进程内结构。
- **IPC 状态**结合 POSIX 共享内存与数据根下的文件持久化。

**快/慢双路径**写入模型让高频日志远离关键路径（无锁异步写入），重要写入则走带完整校验、超时与 trace_id 传播的同步路径。**熔断器**（CLOSED → OPEN → HALF_OPEN）在连续失败后拒绝写入，避免故障级联扩散。

heapstore 以静态库 `airy_heapstore` 的形式被消费，由网关与运行时守护进程链接使用。

## 能力

| 能力 | 入口 |
|------|------|
| 划区的数据根与托管目录布局 | `heapstore_init` / `heapstore_get_path` / `heapstore_get_full_path` |
| 快速（异步无锁）与慢速（同步校验）日志写入 | `heapstore_log_write_fast` / `heapstore_log_write_slow`、`heapstore_log_write` |
| Agent / Skill / Session 注册表与迭代式查询 | `heapstore_registry_*` |
| 追踪 span 存储、批量刷新、JSON 导出 | `heapstore_trace_*` |
| 内存池 / 分配记录 | `heapstore_memory_*` |
| 令牌用量计数与按任务预算 | `heapstore_token_record` / `heapstore_token_set_budget` / `heapstore_token_check_budget` |
| 日志、span、记录的事务性批量写入 | `heapstore_batch_begin` / `heapstore_batch_add_*` / `heapstore_batch_commit` |
| 基于共享内存 + 文件的 IPC 通道/缓冲记录 | `heapstore_ipc_*` |
| 熔断器状态查询与手动重置 | `heapstore_get_circuit_state` / `heapstore_reset_circuit` |
| 用量统计与性能指标 | `heapstore_get_stats` / `heapstore_get_metrics` |
| 跨五个子系统的健康检查 | `heapstore_health_check` |
| SQLite 注册表的 Schema 迁移（正向 / 回滚） | `heapstore_migration_*`、`migrations/*.sql` |

## 构成

### 存储引擎

| 引擎 | 源码 | 后端 | 用途 |
|------|------|------|------|
| **core** | `heapstore_core*.c` | 进程内状态 | 初始化、路径布局、统计、指标、错误、熔断器、异步写入 |
| **log** | `heapstore_log.c`、`kernel/services/log_store_service.c` | 按日期划分的文件 | 日志持久化、按服务分文件、轮转、保留期清理 |
| **registry** | `heapstore_registry*.c` | SQLite，内存回退 | Agent/Skill/Session CRUD 与迭代查询 |
| **trace** | `heapstore_trace.c`、`kernel/services/trace_store_service.c` | 内存缓冲 + span 文件 | span 持久化、时间范围/trace 查询、JSON 导出 |
| **memory** | `heapstore_memory.c` | 内存表 | 内存池与分配记录 |
| **token** | `heapstore_token.c` | 内存原子计数 | 令牌用量统计、按任务预算（最多 1024 个任务） |
| **batch** | `heapstore_core_batch.c` | 内存缓冲 | 跨引擎的批量添加/提交/回滚 |
| **ipc** | `heapstore_ipc*.c` | 共享内存 + 文件 | 可持久化的 IPC 通道/缓冲状态 |
| **migration** | `heapstore_migration*.c` | SQLite + 版本文件 | Schema 升级与回滚（V001、V002） |
| **integration** | `heapstore_integration.c` | 数据 + 元数据文件 | 端到端写入/读取流程 |

### 仓库布局

```
heapstore/
├── CMakeLists.txt            # 定义 airy_heapstore 静态库
├── include/                  # 公共头文件（heapstore.h 及各引擎 API）
├── src/                      # 引擎实现（+ *_internal.h 私有头）
├── kernel/
│   ├── services/             # 内核级日志/追踪存储服务（编译进库）
│   └── README.md
├── services/README.md        # 按服务数据目录布局（运行时创建）
├── migrations/               # SQL Schema 脚本（V001 初始、V002 tags/retry、回滚）
├── tests/                    # 8 个 ctest 套件 + 基准
├── examples/                 # quick_start.c、batch_write.c
└── scripts/                  # 性能回归检测、@since 标签工具
```

### 运行时数据布局

`heapstore_init()` 会在数据根下创建如下结构（数据根解析见[配置](#配置)）：

```
<root>/
├── logs/{apps,kernel,services}/   # 按日期、按服务划分的日志文件
├── registry/                      # SQLite 数据库（编译带 SQLite 时）
├── traces/spans/                  # 刷新落盘的追踪 span 文件
├── services/{llm_d,market_d,tool_d}/  # 按守护进程的数据目录
└── kernel/
    ├── ipc/{channels,buffers}/    # IPC 状态持久化
    └── memory/{pools,allocations,stats,index,meta,patterns,raw}/
```

## 用法

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
heapstore_init(&config);                        /* 传 NULL 使用默认值 */

heapstore_log_write_fast("my_service", 1, "hot-path message");
heapstore_log_write_slow("my_service", 1, "important message",
                         "trace-001", 1000);    /* 同步、带校验 */

heapstore_batch_context_t *ctx = heapstore_batch_begin(1024);
heapstore_batch_add_log(ctx, "my_service", 1, "batched entry");
heapstore_batch_commit(ctx);
heapstore_batch_context_destroy(ctx);

heapstore_shutdown();
```

核心 API 面（`include/heapstore.h`）：

| 函数 | 说明 |
|------|------|
| `heapstore_init(config)` / `heapstore_shutdown()` | 生命周期；init 必须首先调用（配置传 `NULL` = 默认值） |
| `heapstore_ready()` / `heapstore_get_root()` | 状态与数据根查询 |
| `heapstore_get_path()` / `heapstore_get_full_path()` | 托管子路径 |
| `heapstore_log_write_fast()` / `heapstore_log_write_slow()` | 双路径日志写入 |
| `heapstore_batch_begin/add_*/commit/rollback/context_destroy` | 事务性批量写入 |
| `heapstore_get_stats()` / `heapstore_get_metrics()` / `heapstore_reset_metrics()` | 统计 |
| `heapstore_health_check()` | registry/trace/log/ipc/memory 健康检查 |
| `heapstore_get_circuit_state()` / `heapstore_reset_circuit()` | 熔断器 |
| `heapstore_cleanup()` / `heapstore_flush()` | 保留期清理与强制刷新 |
| `heapstore_strerror()` | 错误码转描述 |
| `heapstore_reload_config()` | 配置热更新 |

各引擎 API 位于 `heapstore_registry.h`、`heapstore_trace.h`、`heapstore_log.h`、`heapstore_memory.h`、`heapstore_token.h`、`heapstore_batch.h`、`heapstore_ipc.h`、`heapstore_integration.h`、`heapstore_migration.h`。便捷宏：`HEAPSTORE_LOG_ERROR` / `_WARN` / `_INFO` / `_DEBUG`。

错误码是单一枚举（`heapstore_error_t`，`heapstore_SUCCESS` = 0，向下至 `heapstore_ERR_CIRCUIT_OPEN` = -15 … `heapstore_ERR_INTERNAL` = -99）。

## 构建

heapstore 作为 [AgentRT](https://atomgit.com/openairymax/agentrt) 源码树的一部分构建（源外构建）：

```bash
cmake -S agentrt -B build -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_HEAPSTORE=ON -DBUILD_TESTS=ON
cmake --build build --target airy_heapstore --parallel
```

测试与工具：

```bash
ctest --test-dir build -R heapstore --output-on-failure   # 8 个套件
./build/tests/heapstore_benchmark                         # 性能基准
```

示例（`examples/quick_start.c`、`examples/batch_write.c`）自带
`CMakeLists.txt`，定义链接 `airy_heapstore` 的 `quick_start` 与
`batch_write` 两个 target。示例是说明性源码：未接入 AgentRT 默认构建
树，且其代码早于当前头文件签名，需按最新 API 调整后方可编译。详见
[`examples/README.md`](examples/README.md)。

**CMake 选项：**

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_HEAPSTORE` | `ON` | 构建 heapstore 模块 |
| `BUILD_TESTS` | `ON` | 构建测试套件 |
| `BUILD_BENCHMARK` | `ON` | 构建 `heapstore_benchmark`（定义于 `tests/`） |
| `AIRY_HAS_SQLITE3` | 自动 | 由 `find_package(SQLite3)` 设置；见下表 |

**条件编译：**

| 依赖 | 条件宏 | 缺失时行为 |
|------|--------|-----------|
| SQLite3 | `AIRY_HAS_SQLITE3` | 注册表与迁移使用内存后端；所有 API 保持可用，但数据不跨进程退出持久化 |

**构建产物：** `airy_heapstore` 静态库；公共头文件安装到
`include/agentrt/heapstore`。

## 配置

配置通过 `heapstore_config_t` 结构体传入（没有配置文件）：

| 字段 | 默认值 | 含义 |
|------|--------|------|
| `root_path` | 自动解析（见下） | 数据根目录 |
| `max_log_size_mb` | 100 | 触发日志轮转的大小上限 |
| `log_retention_days` | 7 | 日志清理保留期 |
| `trace_retention_days` | 3 | 追踪清理保留期 |
| `enable_auto_cleanup` / `enable_log_rotation` / `enable_trace_export` | `true` | 维护开关 |
| `db_vacuum_interval_days` | 7 | SQLite vacuum 间隔 |
| `circuit_breaker_threshold` | 5 | 熔断器跳闸前的连续失败次数 |
| `circuit_breaker_timeout_sec` | 30 | 跳闸后半开探测前的等待时间 |

数据根解析顺序：

1. 配置结构体中的 `root_path`；
2. 环境变量 `AIRY_HEAPSTORE_ROOT`；
3. 平台数据目录助手给出的 `<运行时数据目录>/agentrt/heapstore`；
4. `/tmp/agentrt/heapstore`。

## 关系

| 方向 | 模块 | 角色 |
|------|------|------|
| 上游 | [commons](https://atomgit.com/openairymax/commons) | platform/utils/sync/compat 头文件与 `airy_common` 静态库（内存宏、原子操作、互斥锁封装） |
| 上游（可选） | SQLite3 | 持久化的注册表/迁移后端；缺失时内存回退 |
| 下游 | [gateway](https://atomgit.com/openairymax/gateway) | 访问日志与请求追踪 |
| 下游 | 运行时守护进程 | Agent/Skill/Session 注册表、`services/` 下的按服务数据、令牌预算 |

heapstore 本身除 C11 工具链、线程库与可选的 SQLite3 外没有运行时依赖；
在 POSIX 与 Windows 平台上均可编译。

## 许可证

本模块采用双许可证，您可以选择以下任一许可证遵守：

- **GNU Affero General Public License v3.0 or later**
  ([AGPL-3.0-or-later](https://www.gnu.org/licenses/agpl-3.0.txt))，或
- **Apache License, Version 2.0**
  ([Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0.txt))

SPDX-License-Identifier: `AGPL-3.0-or-later OR Apache-2.0`

完整许可证文本见 [LICENSE](LICENSE) 文件；版权与商标声明见
[NOTICE](NOTICE)。
