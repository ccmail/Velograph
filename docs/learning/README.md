# 学习文档索引

本文档汇总所有学习文档，方便快速查阅。

## 文档列表

### 1. [00-overview.md](00-overview.md) - 查询执行流程总览

**内容**: ClickHouse 查询执行的完整流程，从 Parser 到 Executor 的架构概览。

**适用阶段**: 入门阶段，建立整体认知。

**关键词**: 查询流程、架构设计、组件关系

---

### 2. [01-interpreter-select-query.md](01-interpreter-select-query.md) - InterpreterSelectQuery 设计详解

**内容**: `InterpreterSelectQuery` 类的详细分析，包括构造函数、执行阶段、成员变量、设计模式等。

**适用阶段**: 核心学习，深入理解查询解释器。

**关键词**: InterpreterSelectQuery、策略模式、管道模式、执行阶段

---

### 3. [02-query-plan-pipeline.md](02-query-plan-pipeline.md) - QueryPlan 与 QueryPipeline

**内容**: 查询计划和查询管道的设计，步骤类型、数据流、优化器集成。

**适用阶段**: 理解查询计划的构建和执行。

**关键词**: QueryPlan、QueryPipeline、Step、Processor

---

### 4. [03-expression-analyzer.md](03-expression-analyzer.md) - ExpressionAnalyzer 与查询分析

**内容**: 表达式分析器的工作原理，ActionsDAG、别名处理、子查询分析、聚合分析。

**适用阶段**: 理解查询分析和表达式转换。

**关键词**: ExpressionAnalyzer、ActionsDAG、别名、子查询、聚合

---

### 5. [04-storage-read.md](04-storage-read.md) - 存储层与数据读取

**内容**: 存储层接口、MergeTree 引擎、列裁剪、分区裁剪、索引使用、数据缓存。

**适用阶段**: 理解数据读取和存储优化。

**关键词**: Storage、MergeTree、列裁剪、分区裁剪、索引

---

### 6. [05-optimization-techniques.md](05-optimization-techniques.md) - 查询优化技术

**内容**: 逻辑优化、物理优化、聚合优化、JOIN 优化、并行优化、物化视图。

**适用阶段**: 学习查询优化技术。

**关键词**: 谓词下推、列裁剪、分区裁剪、两阶段聚合、物化视图

---

### 7. [06-interpreter-select-query-analyzer.md](06-interpreter-select-query-analyzer.md) - InterpreterSelectQueryAnalyzer 设计详解

**内容**: 新分析器路径下 `SELECT` 解释器的作用与四个阶段（normalize → analyze → plan → execute），与旧 `InterpreterSelectQuery` 的差异。

**适用阶段**: 理解新 analyzer 体系如何把语义分析和执行计划下沉给 `Analyzer` 与 `Planner`。

**关键词**: InterpreterSelectQueryAnalyzer、QueryTree、QueryTreePassManager、Planner、并行副本

---

### 8. [07-query-tree-builder.md](07-query-tree-builder.md) - QueryTree 与 QueryTreeBuilder 设计详解

**内容**: `QueryTree` 的设计理念、16 种节点类型体系、以及 `QueryTreeBuilder` 如何将 AST 转换为 QueryTree。

**适用阶段**: 理解新 analyzer 体系的语义表示层。

**关键词**: QueryTree、QueryTreeBuilder、16 种节点类型、AST 转换、语义表示

---

## 学习路径建议

### 路径一：快速入门（1-2 天）

1. [00-overview.md](00-overview.md) - 了解整体架构
2. [01-interpreter-select-query.md](01-interpreter-select-query.md) - 理解核心组件
3. [05-optimization-techniques.md](05-optimization-techniques.md) - 了解优化技术

### 路径二：深入理解（1 周）

1. [00-overview.md](00-overview.md)
2. [01-interpreter-select-query.md](01-interpreter-select-query.md)
3. [02-query-plan-pipeline.md](02-query-plan-pipeline.md)
4. [03-expression-analyzer.md](03-expression-analyzer.md)
5. [04-storage-read.md](04-storage-read.md)
6. [05-optimization-techniques.md](05-optimization-techniques.md)

### 路径三：源码阅读（持续）

结合文档阅读源码：

```
src/
├── Interpreters/
│   ├── InterpreterSelectQuery.h/.cpp      ← 重点
│   ├── ExpressionAnalyzer.h/.cpp          ← 重点
│   └── ...
├── QueryPipeline/
│   ├── QueryPlan.h/.cpp                   ← 重点
│   └── Pipe.h/.cpp
├── Processors/
│   └── ...
└── Storages/
    └── ...
```

## 补充资源

- [ClickHouse 官方文档](https://clickhouse.com/docs)
- [ClickHouse 源码](https://github.com/ClickHouse/ClickHouse)
- [ClickHouse 博客](https://clickhouse.com/blog)

---

> 文档生成时间: 2025-05-18
