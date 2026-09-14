# heapstore kernel — 内核级存储服务

**位置：** `heapstore/kernel/` ｜ **版本：** 0.1.16
**上游文档：** [heapstore 主文档（中文）](../README_zh.md) ｜ [English](../README.md)

## 概述

`kernel/` 包含两个内核级存储服务源文件，它们**编译进 `airy_heapstore`
静态库**，为上层 `heapstore_log_*` 与 `heapstore_trace_*` API 提供
直接操作文件的存储原语：按日期分文件的日志追加与查询、追踪点的批量
存储、时间范围查询与导出。

## 目录结构

```
kernel/
├── services/
│   ├── log_store_service.c     # 日志存储内核服务
│   └── trace_store_service.c   # 追踪存储内核服务
└── README.md                   # 本文件
```

注意：数据根下的 `kernel/` 目录（`ipc/`、`memory/` 等）是
`heapstore_init()` **运行时创建的数据目录**，与本源码目录不是一回事
（运行时布局见[主文档](../README_zh.md)）。

## 服务与 API

### log_store_service — 日志存储内核服务

按日期分文件追加日志（文件名形如 `log_YYYYMMDD.log`），支持按时间
范围 / 级别 / 组件查询、过期文件清理与存储状态统计。

| 函数 | 说明 |
|------|------|
| `log_store_service_init(storage_path, max_storage_bytes)` | 初始化服务并创建存储目录 |
| `log_store_service_store_entry(level, component, message, timestamp)` | 追加一条日志（`timestamp` 传 NULL 用当前时间） |
| `log_store_service_query_entries(start_time, end_time, level, component, ...)` | 按条件查询日志条目 |
| `log_store_service_free_entries(entries, count)` | 释放查询结果 |
| `log_store_service_cleanup_old_files(days_to_keep)` | 删除超过保留天数的日志文件 |
| `log_store_service_get_status(out_total_bytes, out_file_count, ...)` | 获取存储用量状态 |
| `log_store_service_shutdown()` | 关闭服务 |

### trace_store_service — 追踪存储内核服务

按采样率存储追踪点，支持批量写入、条件查询、按时间范围导出与统计。

| 函数 | 说明 |
|------|------|
| `trace_store_service_init(storage_path, max_storage_bytes, sampling_rate)` | 初始化服务（`sampling_rate` 为 1 时全量存储） |
| `trace_store_point(trace_point)` | 存储单个追踪点 |
| `trace_store_service_store_batch(trace_points, count)` | 批量存储追踪点 |
| `trace_store_service_query_traces(query, ...)` | 按条件查询追踪数据 |
| `trace_store_service_free_traces(traces, count)` | 释放查询结果 |
| `trace_store_service_export_traces(start_time, end_time, ...)` | 导出指定时间范围的追踪数据 |
| `trace_store_service_get_stats(out_total_traces, out_total_bytes, ...)` | 获取存储统计 |
| `trace_store_service_cleanup_old_files(days_to_keep)` | 清理过期追踪文件 |
| `trace_store_service_shutdown()` | 关闭服务 |

## 运行时数据目录

`heapstore_init()` 在数据根下创建以下内核数据目录（源码树中不预置
占位目录）：

```
<root>/kernel/
├── ipc/{channels,buffers}/                    # IPC 通道/缓冲状态持久化
└── memory/{pools,allocations,stats,index,meta,patterns,raw}/  # 内存引擎数据
```

## 依赖

| 组件 | 用途 |
|------|------|
| `airy_heapstore` | 两个服务作为源文件编译进该静态库 |
| commons（platform/utils/sync/compat 头文件） | 目录/时间/原子操作等平台封装，POSIX 与 Windows 均可编译 |

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../LICENSE)，版权与商标声明见
[NOTICE](../NOTICE)。
