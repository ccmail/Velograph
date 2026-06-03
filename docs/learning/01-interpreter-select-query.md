# InterpreterSelectQuery 设计详解

本文档详细分析 `InterpreterSelectQuery` 类的设计思路、核心功能和实现细节。

## 一、类定位

`InterpreterSelectQuery` 是 ClickHouse 中处理 `SELECT` 查询的核心组件，位于查询执行流程的关键位置：

```
Parser (AST) → InterpreterSelectQuery → QueryPlan → Pipeline → Executor
                    ↑
            本文档分析对象
```

**职责**: 将 AST（抽象语法树）形式的 `SELECT` 查询转换为可执行的 `QueryPlan` 或数据流 `BlockIO`。

## 二、继承关系

```
IInterpreterUnionOrSelectQuery (基类)
    ↑
InterpreterSelectQuery
    ↑
可能还有其他 Interpreter 实现
```

**基类提供的能力**:
- 统一的查询执行接口 (`execute()`, `buildQueryPlan()`)
- 通用的查询选项 (`SelectQueryOptions`)
- 子查询/UNION 查询的抽象

## 三、多态构造设计（策略模式）

### 3.1 构造函数概览

```cpp
// 基础构造：从 AST 中的表读取
InterpreterSelectQuery(const ASTPtr &query_ptr_, const ContextPtr &context_,
                       const SelectQueryOptions &, ...);

// 从 Pipe 读取：子查询/CTE 场景
InterpreterSelectQuery(const ASTPtr &query_ptr_, const ContextPtr &context_,
                       Pipe input_pipe_, ...);

// 从指定 Storage 读取
InterpreterSelectQuery(const ASTPtr &query_ptr_, const ContextPtr &context_,
                       const StoragePtr &storage_, ...);

// 复用 PreparedSets：Projection 优化
InterpreterSelectQuery(const ASTPtr &query_ptr_, const ContextPtr &context_,
                       const SelectQueryOptions &, PreparedSetsPtr prepared_sets_);
```

### 3.2 设计意图分析

这是典型的**策略模式**应用，通过重载构造函数支持不同的数据来源：

| 构造方式 | 适用场景 | 数据来源 |
|---------|---------|---------|
| 基础构造 | 普通 SELECT | AST 中指定的表 |
| Pipe 输入 | 子查询/CTE | 上游 Pipe 的输出 |
| StoragePtr | 内部查询 | 直接指定存储引擎 |
| PreparedSets | Projection | 复用预计算集合 |

### 3.3 统一入口

所有构造函数最终都委托给私有构造函数：

```cpp
private:
  InterpreterSelectQuery(const ASTPtr &query_ptr_, const ContextPtr &context_,
                         std::optional<Pipe> input_pipe, const StoragePtr &storage_,
                         const SelectQueryOptions &, ...);
```

这种设计确保了初始化逻辑的**单一性**，避免重复代码。

## 四、查询执行阶段（管道模式）

### 4.1 执行阶段概览

```
┌─────────────────────────────────────────────────────────────┐
│                    查询执行流水线                               │
├─────────────────────────────────────────────────────────────┤
│  executeFetchColumns()   →  从存储读取数据                    │
│       ↓                                                      │
│  executeWhere()          →  过滤数据 (WHERE)                   │
│       ↓                                                      │
│  executeAggregation()    →  聚合计算 (GROUP BY)                │
│       ↓                                                      │
│  executeHaving()         →  聚合后过滤 (HAVING)                │
│       ↓                                                      │
│  executeWindow()         →  窗口函数                          │
│       ↓                                                      │
│  executeOrder()          →  排序 (ORDER BY)                   │
│       ↓                                                      │
│  executeLimit()          →  限制结果数 (LIMIT)                │
│       ↓                                                      │
│  executeOffset()         →  跳过结果 (OFFSET)                 │
│       ↓                                                      │
│  executeProjection()     →  投影计算 (SELECT 列)               │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 核心设计模式：管道（Pipeline）

每个 `executeXxx` 方法对应 SQL 执行的一个物理阶段：

```cpp
void executeFetchColumns(QueryPlan &query_plan);
void executeWhere(QueryPlan &query_plan, const ActionsAndProjectInputsFlagPtr &expression, bool remove_filter);
void executeAggregation(QueryPlan &query_plan, const ActionsAndProjectInputsFlagPtr &expression, ...);
void executeWindow(QueryPlan &query_plan);
void executeOrder(QueryPlan &query_plan, InputOrderInfoPtr sorting_info);
void executeLimit(QueryPlan &query_plan);
// ... 等等
```

**设计优势**:
1. **解耦**: 各阶段独立实现，互不影响
2. **可组合**: 根据查询条件动态选择需要执行的阶段
3. **可优化**: 方便在阶段间插入优化逻辑
4. **可扩展**: 新增执行阶段只需添加新方法

### 4.3 执行入口

```cpp
// 构建查询计划
void buildQueryPlan(QueryPlan &query_plan) override;

// 执行查询，返回数据流
BlockIO execute() override;
```

`execute()` 内部会调用 `buildQueryPlan()` 生成计划，然后执行。

## 五、成员变量组织

### 5.1 分析结果（计算一次，后续复用）

```cpp
TreeRewriterResultPtr syntax_analyzer_result;                    // 语法分析结果
std::unique_ptr<SelectQueryExpressionAnalyzer> query_analyzer;   // 查询分析器
SelectQueryInfo query_info;                                      // 查询信息
ExpressionAnalysisResult analysis_result;                        // 表达式分析结果
```

### 5.2 数据源抽象

```cpp
StoragePtr storage;                                              // 表存储引擎
std::optional<Pipe> input_pipe;                                  // 输入管道（子查询）
std::unique_ptr<InterpreterSelectWithUnionQuery> interpreter_subquery; // 子查询解释器
SharedHeader source_header;                                      // 源数据结构
```

### 5.3 安全与过滤

```cpp
RowPolicyFilterPtr row_policy_filter;                            // 行级安全策略
FilterDAGInfoPtr additional_filter_info;                         // 附加过滤条件
FilterDAGInfoPtr parallel_replicas_custom_filter_info;          // 多副本过滤
```

### 5.4 元数据

```cpp
StorageMetadataPtr metadata_snapshot;                            // 表元数据快照
StorageSnapshotPtr storage_snapshot;                             // 存储快照
TableLockHolder table_lock;                                      // 表锁（RAII）
```

## 六、关键辅助结构

### 6.1 LimitInfo

```cpp
struct LimitInfo {
    UInt64 limit_length{0};              // 限制行数
    UInt64 limit_offset{0};              // 偏移量
    Float64 fractional_limit{0};         // 分数限制（如 0.5 表示 50%）
    Float64 fractional_offset{0};        // 分数偏移
    bool is_limit_length_negative{false};  // 是否为负限制
    bool is_limit_offset_negative{false};  // 是否为负偏移
};
```

支持标准 `LIMIT` 和分数形式，体现 ClickHouse 对 LIMIT 语法的完整支持。

### 6.2 Modificator

```cpp
enum class Modificator : uint8_t {
    ROLLUP = 0,  // ROLLUP 聚合
    CUBE = 1,    // CUBE 聚合
};
```

用于 `GROUP BY ... WITH ROLLUP/CUBE` 场景。

## 七、静态工具方法

```cpp
// 添加空数据源
static void addEmptySourceToQueryPlan(QueryPlan &query_plan, const Block &source_header, const SelectQueryInfo &query_info);

// 获取排序描述
static SortDescription getSortDescription(const ASTSelectQuery &query, const ContextPtr &context);

// 获取排序限制
static UInt64 getLimitForSorting(const ASTSelectQuery &query, const ContextPtr &context);

// 判断是否包含 FINAL
static bool isQueryWithFinal(const SelectQueryInfo &info);

// 获取 LIMIT 信息
static LimitInfo getLimitLengthAndOffset(const ASTSelectQuery &query, const ContextPtr &context);
```

这些方法说明 `InterpreterSelectQuery` 不仅是"执行器"，还是"查询分析工具集"。

## 八、设计模式总结

| 模式 | 应用 | 说明 |
|------|------|------|
| **策略模式** | 多构造函数 | 不同数据来源策略 |
| **管道模式** | executeXxx 方法 | 构建查询执行管道 |
| **模板方法** | executeImpl | 固定顺序调用各阶段 |
| **享元模式** | PreparedSets | 复用预计算集合 |
| **RAII** | TableLockHolder | 自动管理表锁生命周期 |

## 九、与其他组件的关系

```
SQL Parser (AST)
    ↓
InterpreterSelectQuery
    ↓
QueryPlan → QueryPipeline → Processors → Executors
    ↑
Storage (MergeTree, Memory, etc.)
```

**关键交互**:
- **与 Parser**: 消费 AST，不做解析
- **与 Storage**: 通过 `StoragePtr` 读取数据
- **与 QueryPlan**: 在其上构建执行步骤
- **与 ExpressionAnalyzer**: 委托表达式分析

## 十、源码阅读建议

1. **构造函数**: 理解不同场景的初始化逻辑
2. `executeImpl()`: 核心执行流程，按顺序调用各阶段
3. `executeFetchColumns()`: 数据读取起点
4. `executeAggregation()`: 最复杂的阶段之一
5. `buildQueryPlan()`: 查询计划构建入口

---

> 文档生成时间: 2025-05-18
> 基于 ClickHouse 源码 `src/Interpreters/InterpreterSelectQuery.h` 分析
