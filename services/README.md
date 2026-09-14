# heapstore services — 按服务数据目录说明

**位置：** `heapstore/services/` ｜ **版本：** 0.1.16
**上游文档：** [heapstore 主文档（中文）](../README_zh.md) ｜ [English](../README.md)

## 概述

本目录是一份说明文档，不含源码。它描述 heapstore **数据根**下的
`services/` 分区：运行时由 `heapstore_init()` 创建，为各守护进程
划分独立的持久化数据目录。

## 运行时目录布局

```
<数据根>/services/
├── llm_d/      # LLM 推理服务数据
├── market_d/   # 市场服务数据
└── tool_d/     # 工具服务数据
```

| 目录 | 对应守护进程 | 内容 |
|------|-------------|------|
| `llm_d/` | llm_d | LLM 服务运行数据（会话上下文、缓存等） |
| `market_d/` | market_d | 市场服务数据（Agent/Skill 目录相关） |
| `tool_d/` | tool_d | 工具服务数据（工具注册与执行记录） |

## 数据根位置

数据根按以下顺序解析（详见[主文档](../README_zh.md)的配置一节）：

1. `heapstore_config_t.root_path`；
2. 环境变量 `AIRY_HEAPSTORE_ROOT`；
3. 平台运行时数据目录下的 `agentrt/heapstore`；
4. `/tmp/agentrt/heapstore`。

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../LICENSE)，版权与商标声明见
[NOTICE](../NOTICE)。
