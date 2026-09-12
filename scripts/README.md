# heapstore scripts — 工具脚本

**位置：** `heapstore/scripts/` ｜ **版本：** 0.1.15
**上游文档：** [heapstore 主文档（中文）](../README_zh.md) ｜ [English](../README.md)

## 概述

`scripts/` 提供两个仅依赖 Python 3 标准库的开发辅助脚本：性能回归
检测与头文件 `@since` 版本标签批量补全。

## 目录结构

```
scripts/
├── performance_regression_detector.py   # 性能回归检测
├── add_since_tags.py                    # @since 标签补全
└── README.md                            # 本文件
```

## performance_regression_detector.py

对比基线 JSON 与当前性能指标，按 warning / critical 两级阈值判定回归，
生成 Markdown 报告，并可把当前指标写回为新基线。

跟踪的指标（与内嵌基线一致）：

| 指标 | 单位 | 含义 |
|------|------|------|
| `batch_speedup` | x | 批量写相对单条写的加速比 |
| `single_insert_ms` | ms | 单条插入耗时 |
| `batch_insert_1000_ms` | ms | 1000 条批量插入耗时 |
| `log_fast_us` | μs | 快路径日志延迟 |
| `log_slow_ms` | ms | 慢路径日志延迟 |

用法：

```bash
python3 scripts/performance_regression_detector.py [基线文件] [报告文件] [--update-baseline]
```

- 基线文件默认 `performance_baseline.json`（不存在时使用内嵌默认基线）；
- 报告文件默认 `performance_regression_report.md`；
- `--update-baseline` 将本次采集的指标保存为新基线。

说明：当前版本的指标采集基于内嵌基线生成带波动的模拟值，用于跑通
“基线加载 → 对比 → 报告 / 更新基线”的完整流程；接入真实基准输出后
即可用于持续回归检测。

## add_since_tags.py

扫描 `include/` 下的 `*.h` 头文件，为缺少 `@since` 标记的公共 API
文档注释补插 `@since` 行（位置在 `@see` 或注释结束符之前），已有的
`@since` 标记保持不变。

用法：

```bash
python3 scripts/add_since_tags.py
```

脚本按自身所在目录解析相邻的 `include/` 路径；若模块目录布局不同，
运行前需相应调整路径。

## 依赖

| 组件 | 用途 |
|------|------|
| Python ≥ 3.10 | 脚本运行环境（仅标准库） |

---

**许可证：** 本模块采用双许可证 `AGPL-3.0-or-later OR Apache-2.0`，
您可以任选其一遵守；完整文本见 [LICENSE](../LICENSE)，版权与商标声明见
[NOTICE](../NOTICE)。
