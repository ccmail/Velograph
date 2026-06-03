# QueryPlan 与 QueryPipeline 设计分析

本文档分析 ClickHouse 中查询计划（QueryPlan）和查询管道（QueryPipeline）的设计。

## 一、QueryPlan 概述

`QueryPlan` 是逻辑执行计划的容器，由 `InterpreterSelectQuery` 构建。

### 1.1 核心概念

```
QueryPlan
    ├── QueryPlan::Node (步骤节点)
    │       ├── Step (执行步骤)
    │       └── Children (子节点)
    └── QueryPlan::Steps (步骤列表)
```

### 1.2 与 InterpreterSelectQuery 的关系

```cpp
// InterpreterSelectQuery 构建 QueryPlan
void InterpreterSelectQuery::buildQueryPlan(QueryPlan &query_plan) {
    // 1. 初始化
    // 2. 按顺序添加执行步骤
    executeImpl(query_plan, std::nullopt);
}
```

## 二、QueryPipeline 概述

`QueryPipeline` 是物理执行管道，由 `QueryPlan` 转换而来。

### 2.1 核心概念

```
QueryPipeline
    ├── Pipe (数据管道)
    │   ├── Processor (处理器)
    │   └── OutputPort (输出端口)
    └── Processors (处理器集合)
```

### 2.2 与 QueryPlan 的关系

```
QueryPlan (逻辑计划)
    ↓ (转换)
QueryPipeline (物理管道)
    ↓ (执行)
Blocks (数据块)
```

## 三、执行步骤（Step）设计

### 3.1 常见步骤类型

```cpp
// 读取步骤
class ReadFromMemoryStorageStep;
class ReadFromMergeTreeStep;

// 过滤步骤
class FilterStep;

// 聚合步骤
class AggregatingStep;

// 排序步骤
class SortingStep;

// 限制步骤
class LimitStep;

// 投影步骤
class ExpressionStep;
```

### 3.2 步骤的通用接口

```cpp
class IQueryPlanStep {
public:
    virtual ~IQueryPlanStep() = default;
    virtual String getName() const = 0;
    virtual void transformPipeline(QueryPipeline &pipeline) = 0;
    // ...
};
```

## 四、数据流设计

### 4.1 Pipe 与 Processor

```
Input → Processor → Processor → Processor → Output
        ↑           ↑           ↑
      Filter    Aggregation   Sort
```

### 4.2 端口模型

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  Processor  │────→│  Processor  │────→│  Processor  │
│   (Input)   │     │  (Transform)│     │  (Output)   │
└─────────────┘     └─────────────┘     └─────────────┘
       ↑
   OutputPort ──→ InputPort
```

## 五、优化器集成

### 5.1 查询优化流程

```
QueryPlan
    ↓
Optimizer (可选)
    ↓
Optimized QueryPlan
    ↓
QueryPipeline
```

### 5.2 常见优化

- **谓词下推**: 将过滤条件下推到存储层
- **列裁剪**: 只读取需要的列
- **分区裁剪**: 只读取匹配的分区
- **聚合下推**: 将聚合操作下推到存储层

## 六、关键设计原则

1. **逻辑与物理分离**: QueryPlan 是逻辑层，QueryPipeline 是物理层
2. **延迟执行**: 构建阶段不执行，执行阶段才实际运行
3. **流式处理**: 数据以 Block 为单位流式处理
4. **并行执行**: 支持多线程并行处理

## 七、源码路径

```
src/
├── QueryPipeline/
│   ├── QueryPlan.h/.cpp
│   ├── Pipe.h/.cpp
│   └── PipelineExecutor.h/.cpp
└── Processors/
    ├── IProcessor.h/.cpp
    ├── Port.h/.cpp
    └── ...
```

---

> 文档生成时间: 2025-05-18
