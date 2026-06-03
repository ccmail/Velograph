# ExpressionAnalyzer 与查询分析

本文档分析 ClickHouse 中的表达式分析器（ExpressionAnalyzer）及其在查询执行中的作用。

## 一、ExpressionAnalyzer 概述

`ExpressionAnalyzer` 是 ClickHouse 中负责表达式分析和转换的核心组件。

### 1.1 类层次

```
ExpressionAnalyzer
    ↑
SelectQueryExpressionAnalyzer (用于 SELECT 查询)
    ↑
其他特化分析器
```

### 1.2 主要职责

1. **表达式解析**: 将 AST 表达式转换为可执行的 ActionsDAG
2. **类型推导**: 推导表达式的返回类型
3. **别名解析**: 处理列别名
4. **子查询分析**: 识别并处理子查询
5. **聚合分析**: 识别聚合函数和 GROUP BY 表达式

## 二、ActionsDAG

### 2.1 概念

`ActionsDAG`（Directed Acyclic Graph，有向无环图）是 ClickHouse 中表示表达式计算的数据结构。

```
输入列 → [节点1] → [节点2] → [节点3] → 输出列
              ↑
         函数/操作
```

### 2.2 节点类型

```cpp
enum class Action {
    INPUT,      // 输入列
    ALIAS,      // 别名
    FUNCTION,   // 函数调用
    ARRAY_JOIN, // ARRAY JOIN
    // ...
};
```

### 2.3 与 InterpreterSelectQuery 的关系

```cpp
// InterpreterSelectQuery 中使用 query_analyzer
std::unique_ptr<SelectQueryExpressionAnalyzer> query_analyzer;

// 获取分析结果
const ExpressionAnalysisResult &getAnalysisResult() const { return analysis_result; }
```

## 三、查询分析流程

### 3.1 分析阶段

```
1. 语法分析 (Syntax Analysis)
   - 解析 AST 结构
   - 识别查询类型

2. 语义分析 (Semantic Analysis)
   - 列引用解析
   - 类型检查
   - 别名展开

3. 表达式转换 (Expression Transformation)
   - 构建 ActionsDAG
   - 优化表达式

4. 子查询处理 (Subquery Processing)
   - 识别子查询
   - 确定执行策略
```

### 3.2 关键数据结构

```cpp
// 表达式分析结果
struct ExpressionAnalysisResult {
    // WHERE 条件
    std::optional<FilterDAGInfoPtr> filter_info;

    // 聚合信息
    std::optional<AggregationInfo> aggregation_info;

    // 窗口函数信息
    std::optional<WindowInfo> window_info;

    // ORDER BY 信息
    std::optional<SortDescription> sort_description;

    // LIMIT 信息
    std::optional<LimitInfo> limit_info;

    // ...
};
```

## 四、别名处理

### 4.1 别名解析流程

```
SELECT a + b AS c, c * 2 AS d
       ↑
   原始表达式
       ↓
   解析别名 'c'
       ↓
   在后续表达式中 'c' 引用 a + b
       ↓
   构建 ActionsDAG
```

### 4.2 代码示例

```cpp
// InterpreterSelectQuery 中的别名处理
void InterpreterSelectQuery::addPrewhereAliasActions() {
    // 添加 PREWHERE 阶段的别名动作
}
```

## 五、子查询分析

### 5.1 子查询类型

```
1. 标量子查询: SELECT (SELECT max(x) FROM t)
2. IN 子查询: SELECT * FROM t WHERE x IN (SELECT y FROM s)
3. EXISTS 子查询: SELECT * FROM t WHERE EXISTS (SELECT * FROM s)
4. 派生表: SELECT * FROM (SELECT * FROM t) AS sub
```

### 5.2 执行策略

```
1. 独立执行: 子查询先执行，结果作为常量
2. 相关子查询: 子查询与外部查询关联执行
3. 转换为 JOIN: 某些 IN 子查询可转换为 JOIN
```

## 六、聚合分析

### 6.1 聚合识别

```cpp
// 判断是否包含聚合
bool hasAggregation() const { return query_analyzer->hasAggregation(); }

// 聚合执行
void executeAggregation(QueryPlan &query_plan,
                        const ActionsAndProjectInputsFlagPtr &expression,
                        bool overflow_row, bool final,
                        InputOrderInfoPtr group_by_info);
```

### 6.2 聚合优化

- **两阶段聚合**: 先局部聚合，再全局聚合
- **ORDER BY 优化**: 利用排序减少聚合计算
- **GROUP BY 键优化**: 优化 GROUP BY 键的选择

## 七、源码路径

```
src/
├── Interpreters/
│   ├── ExpressionAnalyzer.h/.cpp
│   ├── SelectQueryExpressionAnalyzer.h/.cpp
│   └── ActionsDAG.h/.cpp
└── Core/
    └── ActionsDAG.h/.cpp
```

---

> 文档生成时间: 2025-05-18
