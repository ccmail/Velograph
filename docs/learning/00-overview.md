# ClickHouse 查询执行流程总览

本文档记录 ClickHouse 中 SQL 查询从输入到执行的完整流程，帮助理解各组件的协作关系。

## 一、查询执行的整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         SQL 查询执行流程                          │
├─────────────────────────────────────────────────────────────────┤
│  1. Parser         →  AST (抽象语法树)                           │
│  2. Interpreter    →  QueryPlan (查询计划)                       │
│  3. QueryPipeline  →  Processors (执行器)                        │
│  4. Executors      →  结果输出                                   │
└─────────────────────────────────────────────────────────────────┘
```

## 二、各阶段详解

### 阶段 1: 解析 (Parser)

- **输入**: SQL 字符串
- **输出**: `ASTPtr` (抽象语法树)
- **关键类**: `ParserSelectQuery`, `ASTSelectQuery`
- **说明**: 将 SQL 文本转换为树形结构的 AST，此时仅做语法分析，不做语义检查。

### 阶段 2: 解释 (Interpreter)

- **输入**: `ASTPtr` + `Context`
- **输出**: `QueryPlan` / `BlockIO`
- **关键类**: `InterpreterSelectQuery`, `InterpreterSelectWithUnionQuery`
- **说明**: 这是核心阶段，负责语义分析、优化、生成查询计划。

### 阶段 3: 管道构建 (QueryPipeline)

- **输入**: `QueryPlan`
- **输出**: `QueryPipeline`
- **关键类**: `QueryPlan`, `QueryPipeline`
- **说明**: 将逻辑计划转换为物理执行管道，确定数据流。

### 阶段 4: 执行 (Executors)

- **输入**: `QueryPipeline`
- **输出**: 数据块 (Block)
- **关键类**: `PullingPipelineExecutor`, `PushingPipelineExecutor`
- **说明**: 实际执行查询，输出结果。

## 三、核心组件关系图

```
SQL String
    │
    ▼
┌──────────────┐
│    Parser    │
└──────┬───────┘
       │ ASTPtr
       ▼
┌──────────────────────┐
│ InterpreterSelectQuery │ ◄── 本文档重点分析对象
└──────────┬───────────┘
           │ QueryPlan
           ▼
    ┌──────────────┐
    │ QueryPipeline │
    └──────┬───────┘
           │ Processors
           ▼
    ┌──────────────┐
    │  Executors   │
    └──────┬───────┘
           │ Blocks
           ▼
        结果输出
```

## 四、关键设计原则

1. **分层解耦**: Parser → Interpreter → Pipeline → Executor，每层职责单一
2. **延迟执行**: Interpreter 阶段只构建计划，不执行实际数据读取
3. **资源管理**: 通过 RAII 管理表锁 (`TableLockHolder`) 等资源
4. **上下文传递**: `Context` 贯穿整个执行流程，传递配置和状态

## 五、推荐阅读顺序

1. [01-interpreter-select-query.md](01-interpreter-select-query.md) - `InterpreterSelectQuery` 详解
2. [02-query-plan-pipeline.md](02-query-plan-pipeline.md) - QueryPlan 与 QueryPipeline
3. [03-expression-analyzer.md](03-expression-analyzer.md) - 表达式分析器
4. [04-storage-read.md](04-storage-read.md) - 存储层与数据读取
5. [05-optimization-techniques.md](05-optimization-techniques.md) - 查询优化技术

## 六、相关源码路径

```
src/
├── Interpreters/
│   ├── InterpreterSelectQuery.h/.cpp      # 本文档核心
│   ├── InterpreterSelectWithUnionQuery.h   # UNION 查询
│   └── IInterpreterUnionOrSelectQuery.h    # 基类
├── QueryPipeline/
│   ├── QueryPlan.h/.cpp                    # 查询计划
│   └── Pipe.h/.cpp                         # 数据管道
├── Processors/                             # 执行器
└── Storages/                               # 存储引擎
```

---

> 文档生成时间: 2025-05-18
> 基于 ClickHouse 源码分析
