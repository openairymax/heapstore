# heapstore examples — 使用示例

**位置：** `heapstore/examples/` ｜ **版本：** 0.1.16
**上游文档：** [heapstore 主文档（中文）](../README_zh.md) ｜ [English](../README.md)

## 概述

`examples/` 提供两个示例源文件与一份 `CMakeLists.txt`，演示 heapstore
核心 API 的典型调用流程：初始化、日志写入、追踪 span、令牌统计、健康
检查与事务性批量写入。

## 示例清单

| 文件 | Target | 演示流程 |
|------|--------|----------|
| `quick_start.c` | `quick_start` | `heapstore_init` → 令牌计数（`heapstore_token_init` / `heapstore_token_record` / `heapstore_token_get_stats`）→ 日志写入（`heapstore_log_write`）→ 写入单个 span（`heapstore_trace_write_span`）→ 内存池统计 → 整体统计（`heapstore_get_stats`）→ 健康检查（`heapstore_health_check`）→ 清理 |
| `batch_write.c` | `batch_write` | `heapstore_init` → `heapstore_batch_begin(1024)` → 添加 10 条日志 → 添加 5 个 span → 添加 3 条会话记录 → `heapstore_batch_commit` → 销毁与清理 |

## 目录结构

```
examples/
├── CMakeLists.txt    # 定义 quick_start、batch_write 两个 target
├── quick_start.c
├── batch_write.c
└── README.md         # 本文件
```

## 构建与运行

`CMakeLists.txt` 定义了两个可执行 target，均链接 `airy_heapstore`
静态库（并依赖 commons 头文件与线程库）。

需要如实说明当前状态：

- 该目录**未接入 AgentRT 默认构建树**——仓库中没有任何
  `add_subdirectory` 挂接 `heapstore/examples`；
- 两个示例的源码早于当前公共头文件签名（例如
  `heapstore_health_check`、`heapstore_cleanup`、
  `heapstore_memory_get_stats` 的参数已变化，批量上下文销毁函数名
  为 `heapstore_batch_context_destroy`），**直接编译不会通过**。

因此，示例目前的定位是 API 用法的**阅读参考**。若要实际运行，将
`heapstore/examples` 挂接进一个已提供 `airy_heapstore` target 的
构建树，并把示例调用点调整到当前头文件签名，然后：

```bash
cmake --build build --target quick_start batch_write
./build/examples/quick_start
./build/examples/batch_write
```

## 依赖

| 依赖 | 用途 |
|------|------|
| `airy_heapstore` | 数据存储 API（见[主文档](../README_zh.md)的构建一节） |
| commons 头文件 | `airy_memory.h` 等平台/工具宏 |
| CMake ≥ 3.16 | 构建系统 |

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../LICENSE)，版权与商标声明见
[NOTICE](../NOTICE)。
