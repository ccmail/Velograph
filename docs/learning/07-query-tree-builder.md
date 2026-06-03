# QueryTree 与 QueryTreeBuilder 设计详解

> 本文档分析 `QueryTree` 的设计理念、节点类型体系、以及 `QueryTreeBuilder` 如何将 AST 转换为 QueryTree。
> 源码: `src/Analyzer/IQueryTreeNode.h`、`src/Analyzer/QueryTreeBuilder.{h,cpp}` 及各节点类型头文件。

## 一、QueryTree 是什么

### 1.1 定义与定位

`QueryTree` 是 ClickHouse 新分析器路径下的 **语义表示（semantic representation）**，
与 AST（抽象语法树）的 **语法表示（syntactic representation）** 形成对比：

| 维度 | AST | QueryTree |
|------|-----|-----------|
| 表达内容 | 语法结构（怎么写的） | 语义结构（是什么） |
| 类型系统 | 弱类型，节点类型混杂 | 强类型，16 种明确节点类型 |
| 标识符 | 未解析的字符串 | 已解析为 `ColumnNode` / `TableNode` |
| 子查询 | 嵌套的 `ASTSubquery` | 独立的 `QueryNode`，可被引用 |
| 列来源 | 无明确追踪 | `ColumnNode` 通过 weak pointer 指向来源 |
| 别名处理 | 散落在各处 | 统一在 `IQueryTreeNode::alias` |
| 适用阶段 | Parser → Analyzer | Analyzer → Planner |

**核心设计思想**（来自 `IQueryTreeNode.h:51-61`）：

```
Query tree is a semantic representation of query.
Query tree node represent node in query tree.
Important property of query tree is that each query tree node 
can contain weak pointers to other query tree nodes.
```

### 1.2 为什么需要 QueryTree

旧路径的问题：
- `ExpressionAnalyzer` 直接在 AST 上做语义分析，AST 节点类型不够精确，需要大量 `typeid` / `dynamic_cast`；
- 标识符解析、别名处理、子查询展开等逻辑散落在多个类中，难以复用；
- 优化 pass 直接修改 AST，容易破坏语法结构。

QueryTree 的优势：
- **强类型节点**：每种语义概念对应一个节点类型，编译期类型安全；
- **统一的 pass 框架**：`QueryTreePassManager` 可以按顺序跑多个 pass，每个 pass 只关注自己的职责；
- **清晰的所有权**：通过 weak pointer 追踪列来源，避免循环引用；
- **可逆转换**：`toAST` 可以把 QueryTree 转回 AST，用于分布式查询下发。

## 二、QueryTree 节点类型体系

### 2.1 16 种节点类型

定义在 `IQueryTreeNode.h:27-46`：

```cpp
enum class QueryTreeNodeType : uint8_t {
  IDENTIFIER,        // 未解析的标识符（分析前）
  MATCHER,           // * 或 COLUMNS(...) 表达式
  TRANSFORMER,       // APPLY / EXCEPT / REPLACE 转换器
  LIST,              // 节点列表容器
  CONSTANT,          // 字面量常量
  FUNCTION,          // 函数调用
  COLUMN,            // 已解析的列（分析后）
  LAMBDA,            // lambda 表达式
  SORT,              // ORDER BY 子句项
  INTERPOLATE,       // INTERPOLATE 子句项
  WINDOW,            // 窗口定义
  TABLE,             // 表引用
  TABLE_FUNCTION,    // 表函数调用
  QUERY,             // SELECT 查询
  ARRAY_JOIN,        // ARRAY JOIN 操作
  JOIN,              // JOIN 操作
  UNION              // UNION / INTERSECT / EXCEPT
};
```

### 2.2 核心节点类型详解

#### 2.2.1 IDENTIFIER — 未解析标识符

**定义**（`IdentifierNode.h`）：

```cpp
class IdentifierNode : public IQueryTreeNode {
  Identifier identifier;  // 标识符本身
  std::optional<TableExpressionModifiers> table_expression_modifiers;
};
```

**用途**：表示分析前的未解析标识符，例如 `SELECT a FROM test_table` 中的 `a` 和 `test_table`。

**生命周期**：
- **构建阶段**：`QueryTreeBuilder` 从 `ASTIdentifier` 创建；
- **分析阶段**：`QueryTreePassManager` 的 resolve pass 将其解析为 `ColumnNode` / `TableNode` / `FunctionNode` 等；
- **计划阶段**：理论上不应再存在未解析的 `IdentifierNode`。

**特殊用法**：当 `IdentifierNode` 出现在 JOIN TREE 中时，可携带 `TableExpressionModifiers`（`SAMPLE` / `OFFSET` / `FINAL`）。

#### 2.2.2 MATCHER — 通配符与列匹配器

**定义**（`MatcherNode.h`）：

```cpp
enum class MatcherNodeType { ASTERISK, COLUMNS_REGEXP, COLUMNS_LIST };

class MatcherNode : public IQueryTreeNode {
  MatcherNodeType matcher_type;
  Identifier qualified_identifier;       // 限定符，如 t.*
  Identifiers columns_identifiers;       // COLUMNS(a, b, c)
  std::shared_ptr<re2::RE2> columns_matcher;  // COLUMNS('regex')
};
```

**6 种变体**：

| 限定 | 类型 | 示例 |
|------|------|------|
| 无限定 | ASTERISK | `SELECT *` |
| 无限定 | COLUMNS_REGEXP | `SELECT COLUMNS('a.*')` |
| 无限定 | COLUMNS_LIST | `SELECT COLUMNS(a, b)` |
| 有限定 | ASTERISK | `SELECT t.*` |
| 有限定 | COLUMNS_REGEXP | `SELECT t.COLUMNS('a.*')` |
| 有限定 | COLUMNS_LIST | `SELECT t.COLUMNS(a, b)` |

**列转换器**：`MatcherNode` 可携带 `APPLY` / `EXCEPT` / `REPLACE` 转换器（存储在 child 0），例如：

```sql
SELECT * EXCEPT (a) APPLY x -> x + 1
```

**解析时机**：必须在 query analysis pass 中展开为具体的 `ColumnNode` 列表。

#### 2.2.3 COLUMN — 已解析列

**定义**（`ColumnNode.h`）：

```cpp
class ColumnNode : public IQueryTreeNode {
  NameAndTypePair column;  // 列名与类型
  // weak pointer to column source (table / lambda / subquery)
};
```

**核心特性**：
- **列来源追踪**：通过 weak pointer（child index 0）指向列的来源节点（`TableNode` / `QueryNode` / `LambdaNode`）；
- **ALIAS 列**：对于表的 `ALIAS` 列，`ColumnNode` 包含 expression child（child index 1）；
- **ARRAY JOIN 列**：对于 `ARRAY JOIN` 产生的列，也包含 expression child。

**示例**：

```sql
SELECT t.a FROM test_table AS t
```

分析后，`t.a` 变成 `ColumnNode`，其 weak pointer 指向代表 `test_table` 的 `TableNode`。

#### 2.2.4 LAMBDA — Lambda 表达式

**定义**（`LambdaNode.h`）：

```cpp
class LambdaNode : public IQueryTreeNode {
  Names argument_names;   // 参数名列表
  DataTypePtr result_type;
  bool is_operator;       // 是否是 -> 语法
};
```

**两个 children**：
- child 0：`ListNode`，包含 lambda 参数（`IdentifierNode` 或 `ColumnNode`）；
- child 1：lambda body 表达式。

**示例**：

```sql
SELECT arrayMap(x -> x + 1, [1, 2, 3])
```

`x -> x + 1` 被解析为 `LambdaNode`，`argument_names = ["x"]`，body 是 `FunctionNode(+, [ColumnNode(x), ConstantNode(1)])`。

**类型推导**：lambda 的 `result_type` 依赖于参数类型，必须在 query analysis pass 中确定。

#### 2.2.5 FUNCTION — 函数调用

**定义**（`FunctionNode.h`）：

```cpp
enum class FunctionKind { UNKNOWN, ORDINARY, AGGREGATE, WINDOW };

class FunctionNode : public IQueryTreeNode {
  String function_name;
  FunctionKind function_kind;
  FunctionOverloadResolverPtr function;  // 解析后的函数对象
  AggregateFunctionPtr aggregate_function;
};
```

**三类 children**：
- child 0：parameters（`ListNode`，用于参数化函数，如 `quantile(0.5)`）；
- child 1：arguments（`ListNode`，函数参数）；
- child 2：window definition（仅 window function）。

**示例**：

```sql
SELECT sum(a) OVER (PARTITION BY b)
```

`sum(a)` 是 `FunctionNode`，`function_kind = WINDOW`，arguments 包含 `ColumnNode(a)`，child 2 指向 `WindowNode`。

#### 2.2.6 QUERY — SELECT 查询

**定义**（`QueryNode.h`）：

```cpp
class QueryNode : public IQueryTreeNode {
  // 17 个 child 索引，对应 SELECT 的各个子句
};
```

**17 个子句**（按 child index 顺序）：

| Index | 子句 | 类型 | 说明 |
|-------|------|------|------|
| 0 | WITH | `ListNode` | CTE 定义 |
| 1 | PROJECTION | `ListNode` | SELECT 列表 |
| 2 | JOIN TREE | 任意 table expression | FROM / JOIN |
| 3 | PREWHERE | expression | PREWHERE 过滤 |
| 4 | WHERE | expression | WHERE 过滤 |
| 5 | GROUP BY | `ListNode` | GROUP BY 表达式 |
| 6 | HAVING | expression | HAVING 过滤 |
| 7 | WINDOW | `ListNode` | WINDOW 定义 |
| 8 | QUALIFY | expression | QUALIFY 过滤 |
| 9 | ORDER BY | `ListNode` | ORDER BY 项 |
| 10 | INTERPOLATE | `ListNode` | INTERPOLATE 项 |
| 11 | LIMIT BY LIMIT | expression | LIMIT BY 的 limit |
| 12 | LIMIT BY OFFSET | expression | LIMIT BY 的 offset |
| 13 | LIMIT BY | `ListNode` | LIMIT BY 列 |
| 14 | LIMIT | expression | LIMIT |
| 15 | OFFSET | expression | OFFSET |
| 16 | CORRELATED COLUMNS | `ListNode` | 相关子查询的外层列 |

**示例**：

```sql
SELECT a, sum(b) FROM t WHERE c > 10 GROUP BY a HAVING sum(b) > 100 ORDER BY a LIMIT 10
```

对应的 `QueryNode`：
- child 1 (PROJECTION)：`[ColumnNode(a), FunctionNode(sum, [ColumnNode(b)])]`
- child 2 (JOIN TREE)：`TableNode(t)`
- child 4 (WHERE)：`FunctionNode(>, [ColumnNode(c), ConstantNode(10)])`
- child 5 (GROUP BY)：`[ColumnNode(a)]`
- child 6 (HAVING)：`FunctionNode(>, [FunctionNode(sum, [ColumnNode(b)]), ConstantNode(100)])`
- child 9 (ORDER BY)：`[SortNode(ColumnNode(a), ASC)]`
- child 14 (LIMIT)：`ConstantNode(10)`

#### 2.2.7 TABLE — 表引用

**定义**（`TableNode.h`）：

```cpp
class TableNode : public IQueryTreeNode {
  StoragePtr storage;
  StorageID storage_id;
  TableLockHolder table_lock;
  StorageSnapshotPtr storage_snapshot;
};
```

**用途**：表示对具体表的引用，包含存储层对象、表锁、快照等。

**示例**：

```sql
SELECT * FROM test_table
```

`test_table` 被解析为 `TableNode`，`storage` 指向 `StoragePtr`（如 `StorageMergeTree`）。

#### 2.2.8 TABLE_FUNCTION — 表函数

**定义**（`TableFunctionNode.h`）：

```cpp
class TableFunctionNode : public IQueryTreeNode {
  String table_function_name;
  TableFunctionPtr table_function;
  StoragePtr storage;  // 表函数返回的临时表
};
```

**示例**：

```sql
SELECT * FROM numbers(10)
```

`numbers(10)` 被解析为 `TableFunctionNode`，`table_function_name = "numbers"`，arguments 包含 `ConstantNode(10)`。

#### 2.2.9 JOIN — JOIN 操作

**定义**（`JoinNode.h`）：

```cpp
class JoinNode : public IQueryTreeNode {
  JoinKind join_kind;       // INNER / LEFT / RIGHT / FULL
  JoinStrictness strictness; // ALL / ANY / ASOF
  JoinLocality locality;     // LOCAL / GLOBAL
};
```

**三个 children**：
- child 0：left table expression；
- child 1：right table expression；
- child 2：join expression（ON 子句）。

**示例**：

```sql
SELECT * FROM t1 INNER JOIN t2 ON t1.id = t2.id
```

对应 `JoinNode`：
- `join_kind = INNER`
- child 0：`TableNode(t1)`
- child 1：`TableNode(t2)`
- child 2：`FunctionNode(=, [ColumnNode(t1.id), ColumnNode(t2.id)])`

#### 2.2.10 ARRAY_JOIN — ARRAY JOIN 操作

**定义**（`ArrayJoinNode.h`）：

```cpp
class ArrayJoinNode : public IQueryTreeNode {
  bool is_left;  // 是否是 LEFT ARRAY JOIN
};
```

**两个 children**：
- child 0：table expression（被 ARRAY JOIN 的表）；
- child 1：`ListNode`，包含 ARRAY JOIN 的列表达式。

**示例**：

```sql
SELECT a, b FROM t ARRAY JOIN arr AS b
```

对应 `ArrayJoinNode`：
- child 0：`TableNode(t)`
- child 1：`[ColumnNode(arr)]`

#### 2.2.11 UNION — UNION / INTERSECT / EXCEPT

**定义**（`UnionNode.h`）：

```cpp
enum class UnionMode { UNION_DEFAULT, UNION_ALL, UNION_DISTINCT, EXCEPT_DEFAULT, ... };

class UnionNode : public IQueryTreeNode {
  UnionMode union_mode;
  SelectUnionMode union_mode_from_ast;  // 原始 AST 的 mode
};
```

**children**：`ListNode`，包含所有 union 的查询（`QueryNode`）。

**示例**：

```sql
SELECT a FROM t1 UNION ALL SELECT b FROM t2
```

对应 `UnionNode`：
- `union_mode = UNION_ALL`
- children：`[QueryNode(SELECT a FROM t1), QueryNode(SELECT b FROM t2)]`

#### 2.2.12 其他辅助节点

| 节点类型 | 用途 | 示例 |
|---------|------|------|
| `CONSTANT` | 字面量常量 | `1`, `'hello'`, `[1, 2, 3]` |
| `LIST` | 节点列表容器 | SELECT 列表、GROUP BY 列表 |
| `SORT` | ORDER BY 项 | `a ASC`, `b DESC NULLS LAST` |
| `INTERPOLATE` | INTERPOLATE 项 | `INTERPOLATE (x AS x + 1)` |
| `WINDOW` | 窗口定义 | `OVER (PARTITION BY a ORDER BY b)` |
| `TRANSFORMER` | 列转换器 | `APPLY x -> x + 1`, `EXCEPT (a)` |

## 三、QueryTreeBuilder 转换过程

### 3.1 入口函数

```cpp
QueryTreeNodePtr buildQueryTree(ASTPtr query, ContextPtr context);
```

**职责**：将 AST 转换为 QueryTree 的顶层入口。

**支持的 AST 类型**：
- `ASTSelectQuery` → `QueryNode`
- `ASTSelectWithUnionQuery` → `UnionNode` 或 `QueryNode`
- `ASTExpressionList` → `ListNode`
- 其他表达式 AST → 对应的 expression node

**转换流程**：

```
buildQueryTree(ASTPtr)
  ├─ ASTSelectQuery → buildSelectExpression
  ├─ ASTSelectWithUnionQuery → buildUnionExpression
  └─ 其他 → buildExpression
```

### 3.2 SELECT 查询转换

**核心函数**：`buildSelectExpression(ASTSelectQuery, context)`

**转换步骤**：

1. **创建 QueryNode**：
   ```cpp
   auto query_node = std::make_shared<QueryNode>();
   ```

2. **转换 WITH 子句**（CTE）：
   ```cpp
   if (select_query.with())
     query_node->getWith() = buildExpression(select_query.with(), context);
   ```

3. **转换 SELECT 列表**：
   ```cpp
   query_node->getProjection() = buildExpression(select_query.select(), context);
   ```
   - `SELECT *` → `MatcherNode(ASTERISK)`
   - `SELECT a, b` → `ListNode([IdentifierNode(a), IdentifierNode(b)])`
   - `SELECT a AS x` → `IdentifierNode(a)` with alias "x"

4. **转换 FROM / JOIN 子句**：
   ```cpp
   if (select_query.tables())
     query_node->getJoinTree() = buildJoinTree(select_query.tables(), context);
   ```

5. **转换过滤子句**：
   ```cpp
   if (select_query.prewhere())
     query_node->getPrewhere() = buildExpression(select_query.prewhere(), context);
   if (select_query.where())
     query_node->getWhere() = buildExpression(select_query.where(), context);
   ```

6. **转换 GROUP BY / HAVING**：
   ```cpp
   if (select_query.groupBy())
     query_node->getGroupBy() = buildExpression(select_query.groupBy(), context);
   if (select_query.having())
     query_node->getHaving() = buildExpression(select_query.having(), context);
   ```

7. **转换 ORDER BY / LIMIT**：
   ```cpp
   if (select_query.orderBy())
     query_node->getOrderBy() = buildSortColumnList(select_query.orderBy(), context);
   if (select_query.limitLength())
     query_node->getLimit() = buildExpression(select_query.limitLength(), context);
   ```

### 3.3 表达式转换

**核心函数**：`buildExpression(ASTPtr ast, context)`

**转换映射表**：

| AST 类型 | QueryTree 节点 | 说明 |
|---------|---------------|------|
| `ASTIdentifier` | `IdentifierNode` | 未解析标识符 |
| `ASTAsterisk` | `MatcherNode(ASTERISK)` | `*` |
| `ASTColumnsMatchers` | `MatcherNode(REGEXP/LIST)` | `COLUMNS(...)` |
| `ASTLiteral` | `ConstantNode` | 字面量 |
| `ASTFunction` | `FunctionNode` 或 `LambdaNode` | 函数调用或 lambda |
| `ASTSubquery` | `QueryNode` | 子查询 |
| `ASTTableExpression` | `TableNode` / `TableFunctionNode` / `QueryNode` | 表表达式 |

**关键转换逻辑**：

#### 3.3.1 函数与 Lambda

```cpp
if (ast->as<ASTFunction>()) {
  auto & function = ast->as<ASTFunction &>();
  
  // 检查是否是 lambda (-> 语法)
  if (function.name == "lambda") {
    auto lambda_node = std::make_shared<LambdaNode>();
    lambda_node->getArguments() = buildExpression(function.arguments->children[0]);
    lambda_node->getExpression() = buildExpression(function.arguments->children[1]);
    return lambda_node;
  }
  
  // 普通函数
  auto function_node = std::make_shared<FunctionNode>(function.name);
  if (function.parameters)
    function_node->getParameters() = buildExpression(function.parameters);
  if (function.arguments)
    function_node->getArguments() = buildExpression(function.arguments);
  return function_node;
}
```

#### 3.3.2 子查询

```cpp
if (ast->as<ASTSubquery>()) {
  auto & subquery = ast->as<ASTSubquery &>();
  return buildQueryTree(subquery.children[0], context);
}
```

#### 3.3.3 列转换器

```cpp
if (matcher->column_list) {
  // APPLY / EXCEPT / REPLACE
  auto transformer_node = std::make_shared<ListNode>();
  for (auto & transformer_ast : matcher->column_list->children) {
    if (transformer_ast->as<ASTColumnsApplyTransformer>())
      transformer_node->getNodes().push_back(buildApplyTransformer(...));
    else if (transformer_ast->as<ASTColumnsExceptTransformer>())
      transformer_node->getNodes().push_back(buildExceptTransformer(...));
    else if (transformer_ast->as<ASTColumnsReplaceTransformer>())
      transformer_node->getNodes().push_back(buildReplaceTransformer(...));
  }
  matcher_node->setColumnTransformers(transformer_node);
}
```

### 3.4 JOIN TREE 转换

**核心函数**：`buildJoinTree(ASTTablesInSelectQuery, context)`

**转换逻辑**：

1. **单表**：
   ```cpp
   if (tables->children.size() == 1) {
     return buildTableExpression(tables->children[0], context);
   }
   ```

2. **多表 JOIN**：
   ```cpp
   QueryTreeNodePtr join_tree = buildTableExpression(tables->children[0]);
   
   for (size_t i = 1; i < tables->children.size(); ++i) {
     auto join_node = std::make_shared<JoinNode>();
     join_node->getLeftTableExpression() = join_tree;
     join_node->getRightTableExpression() = buildTableExpression(tables->children[i]);
     
     // 提取 JOIN 类型和条件
     auto & table_join = tables->children[i]->as<ASTTableJoin &>();
     join_node->setJoinKind(table_join.kind);
     join_node->setJoinStrictness(table_join.strictness);
     if (table_join.on_expression)
       join_node->getJoinExpression() = buildExpression(table_join.on_expression);
     
     join_tree = join_node;
   }
   
   return join_tree;
   ```

3. **ARRAY JOIN**：
   ```cpp
   if (table_element->array_join) {
     auto array_join_node = std::make_shared<ArrayJoinNode>();
     array_join_node->getTableExpression() = buildTableExpression(table_element->table_expression);
     array_join_node->getJoinExpressions() = buildExpression(table_element->array_join);
     array_join_node->setIsLeft(table_element->array_join->kind == ASTArrayJoin::Kind::Left);
     return array_join_node;
   }
   ```

### 3.5 表表达式转换

**核心函数**：`buildTableExpression(ASTTableExpression, context)`

**转换分支**：

```cpp
if (table_expression.database_and_table_name) {
  // 表引用
  auto identifier = table_expression.database_and_table_name->as<ASTIdentifier>();
  auto identifier_node = std::make_shared<IdentifierNode>(identifier->name());
  
  // 处理 FINAL / SAMPLE
  if (table_expression.final || table_expression.sample_size) {
    TableExpressionModifiers modifiers;
    modifiers.has_final = table_expression.final;
    if (table_expression.sample_size)
      modifiers.sample_size_ratio = buildExpression(table_expression.sample_size);
    identifier_node->setTableExpressionModifiers(modifiers);
  }
  
  return identifier_node;
}

if (table_expression.table_function) {
  // 表函数
  auto function = table_expression.table_function->as<ASTFunction>();
  auto table_function_node = std::make_shared<TableFunctionNode>(function->name);
  if (function->arguments)
    table_function_node->getArguments() = buildExpression(function->arguments);
  return table_function_node;
}

if (table_expression.subquery) {
  // 子查询
  return buildQueryTree(table_expression.subquery->children[0], context);
}
```

### 3.6 别名处理

**统一机制**：所有 `IQueryTreeNode` 都有 `alias` 成员。

**转换时机**：

```cpp
auto node = buildExpression(ast, context);

if (ast->tryGetAlias()) {
  node->setAlias(ast->getAliasOrColumnName());
}

return node;
```

**示例**：

```sql
SELECT a AS x, sum(b) AS total
```

转换为：

```
ListNode [
  IdentifierNode("a") with alias "x",
  FunctionNode("sum", [IdentifierNode("b")]) with alias "total"
]
```

## 四、关键转换模式

### 4.1 聚合查询转换

**SQL 示例**：

```sql
SELECT dept, sum(salary) AS total
FROM employees
WHERE age > 30
GROUP BY dept
HAVING sum(salary) > 100000
```

**转换后的 QueryNode 结构**：

```
QueryNode
├─ child 1 (PROJECTION): ListNode [
│    IdentifierNode("dept"),
│    FunctionNode("sum", [IdentifierNode("salary")]) with alias "total"
│  ]
├─ child 2 (JOIN TREE): IdentifierNode("employees")
├─ child 4 (WHERE): FunctionNode(">", [IdentifierNode("age"), ConstantNode(30)])
├─ child 5 (GROUP BY): ListNode [IdentifierNode("dept")]
└─ child 6 (HAVING): FunctionNode(">", [
     FunctionNode("sum", [IdentifierNode("salary")]),
     ConstantNode(100000)
   ])
```

**关键点**：
- `sum(salary)` 在 PROJECTION 和 HAVING 中都是 `FunctionNode`，但在分析阶段会被识别为同一个聚合函数；
- GROUP BY 中的 `dept` 是 `IdentifierNode`，分析后会解析为 `ColumnNode`。

### 4.2 窗口函数转换

**SQL 示例**：

```sql
SELECT name, salary, rank() OVER (PARTITION BY dept ORDER BY salary DESC) AS rnk
FROM employees
```

**转换后的结构**：

```
QueryNode
├─ child 1 (PROJECTION): ListNode [
│    IdentifierNode("name"),
│    IdentifierNode("salary"),
│    FunctionNode("rank", [], window=WindowNode) with alias "rnk"
│  ]
├─ child 2 (JOIN TREE): IdentifierNode("employees")
└─ FunctionNode("rank") 的 child 2 (WINDOW): WindowNode
     ├─ partition_by: ListNode [IdentifierNode("dept")]
     └─ order_by: ListNode [SortNode(IdentifierNode("salary"), DESC)]
```

**关键点**：
- 窗口函数的 `FunctionNode` 有 `function_kind = WINDOW`；
- `WindowNode` 作为 child 2 存储 `PARTITION BY` 和 `ORDER BY` 子句。

### 4.3 CTE（WITH 子句）转换

**SQL 示例**：

```sql
WITH high_earners AS (
  SELECT * FROM employees WHERE salary > 100000
)
SELECT dept, count(*) FROM high_earners GROUP BY dept
```

**转换后的结构**：

```
QueryNode (外层查询)
├─ child 0 (WITH): ListNode [
│    QueryNode (CTE: high_earners) with alias "high_earners"
│      ├─ child 1 (PROJECTION): MatcherNode(ASTERISK)
│      ├─ child 2 (JOIN TREE): IdentifierNode("employees")
│      └─ child 4 (WHERE): FunctionNode(">", [IdentifierNode("salary"), ConstantNode(100000)])
│  ]
├─ child 1 (PROJECTION): ListNode [
│    IdentifierNode("dept"),
│    FunctionNode("count", [MatcherNode(ASTERISK)])
│  ]
├─ child 2 (JOIN TREE): IdentifierNode("high_earners")
└─ child 5 (GROUP BY): ListNode [IdentifierNode("dept")]
```

**关键点**：
- CTE 定义存储在外层 `QueryNode` 的 child 0（WITH）中；
- CTE 本身是一个独立的 `QueryNode`，带有 alias；
- 外层查询的 JOIN TREE 中引用 CTE 时，使用 `IdentifierNode("high_earners")`，分析阶段会解析为指向 CTE 的 `QueryNode`。

### 4.4 相关子查询转换

**SQL 示例**：

```sql
SELECT name, salary
FROM employees e1
WHERE salary > (SELECT AVG(salary) FROM employees e2 WHERE e2.dept = e1.dept)
```

**转换后的结构**：

```
QueryNode (外层查询)
├─ child 1 (PROJECTION): ListNode [IdentifierNode("name"), IdentifierNode("salary")]
├─ child 2 (JOIN TREE): IdentifierNode("employees") with alias "e1"
└─ child 4 (WHERE): FunctionNode(">", [
     IdentifierNode("salary"),
     QueryNode (子查询)
       ├─ child 1 (PROJECTION): ListNode [
       │    FunctionNode("AVG", [IdentifierNode("salary")])
       │  ]
       ├─ child 2 (JOIN TREE): IdentifierNode("employees") with alias "e2"
       ├─ child 4 (WHERE): FunctionNode("=", [
       │    IdentifierNode("e2.dept"),
       │    IdentifierNode("e1.dept")  ← 外层列引用
       │  ])
       └─ child 16 (CORRELATED COLUMNS): ListNode [
            ColumnNode("e1.dept")  ← 分析后填充
          ]
   ])
```

**关键点**：
- 子查询是嵌套的 `QueryNode`；
- 相关列（`e1.dept`）在构建阶段是 `IdentifierNode`，分析阶段会解析为 `ColumnNode` 并记录在子查询的 child 16（CORRELATED COLUMNS）中；
- 这使得 planner 可以识别相关子查询并选择合适的执行策略（如 lateral join）。

### 4.5 UNION 转换

**SQL 示例**：

```sql
SELECT a FROM t1
UNION ALL
SELECT b FROM t2
```

**转换后的结构**：

```
UnionNode (union_mode = UNION_ALL)
└─ children: ListNode [
     QueryNode (SELECT a FROM t1)
       ├─ child 1 (PROJECTION): ListNode [IdentifierNode("a")]
       └─ child 2 (JOIN TREE): IdentifierNode("t1"),
     QueryNode (SELECT b FROM t2)
       ├─ child 1 (PROJECTION): ListNode [IdentifierNode("b")]
       └─ child 2 (JOIN TREE): IdentifierNode("t2")
   ]
```

**关键点**：
- `UnionNode` 的 children 是所有 union 的查询（`QueryNode`）列表；
- `union_mode` 区分 `UNION ALL` / `UNION DISTINCT` / `EXCEPT` / `INTERSECT`。

### 4.6 复杂 JOIN 转换

**SQL 示例**：

```sql
SELECT *
FROM t1
INNER JOIN t2 ON t1.id = t2.id
LEFT JOIN t3 ON t2.id = t3.id
```

**转换后的 JOIN TREE**：

```
JoinNode (LEFT JOIN)
├─ child 0 (left): JoinNode (INNER JOIN)
│    ├─ child 0 (left): IdentifierNode("t1")
│    ├─ child 1 (right): IdentifierNode("t2")
│    └─ child 2 (join expr): FunctionNode("=", [IdentifierNode("t1.id"), IdentifierNode("t2.id")])
├─ child 1 (right): IdentifierNode("t3")
└─ child 2 (join expr): FunctionNode("=", [IdentifierNode("t2.id"), IdentifierNode("t3.id")])
```

**关键点**：
- 多表 JOIN 构建为**左深树（left-deep tree）**：最左的表在最底层，每次 JOIN 向右添加一个表；
- 每个 `JoinNode` 有三个 children：left table expression、right table expression、join expression。

## 五、与其他组件的关系

### 5.1 与 QueryTreePassManager 的关系

**职责分工**：
- `QueryTreeBuilder`：负责 AST → QueryTree 的**结构转换**，不做语义分析；
- `QueryTreePassManager`：负责在 QueryTree 上跑**语义分析和优化 passes**。

**典型 passes**（在 `src/Analyzer/Passes/` 中）：

| Pass 名称 | 职责 | 示例 |
|----------|------|------|
| `QueryAnalysisPass` | 标识符解析、类型推导、函数绑定 | `IdentifierNode("a")` → `ColumnNode(a, type=Int32)` |
| `FunctionToSubcolumnsPass` | 函数转子列访问 | `length(arr)` → `arr.size0` |
| `NormalizeCountVariantsPass` | 规范化 count 变体 | `count()` → `count(*)` |
| `AggregateFunctionsArithmetics` | 聚合函数算术优化 | `sum(x) + sum(y)` → `sum(x + y)` |
| `UniqInjectiveFunctionsEliminationPass` | 消除 uniq 中的单射函数 | `uniq(f(x))` → `uniq(x)` (if f is injective) |

**调用时机**：

```cpp
// 在 InterpreterSelectQueryAnalyzer 中
auto query_tree = buildQueryTree(query, context);  // 构建 QueryTree

QueryTreePassManager pass_manager(context);
addQueryTreePasses(pass_manager, only_analyze);
pass_manager.run(query_tree);  // 跑 passes

// 现在 query_tree 中的 IdentifierNode 已解析为 ColumnNode/TableNode 等
```

### 5.2 与 Planner 的关系

**职责分工**：
- `QueryTreeBuilder` + `QueryTreePassManager`：产出**语义完整的 QueryTree**（所有标识符已解析、类型已确定）；
- `Planner`：将 QueryTree 转换为**可执行的 QueryPlan**。

**转换流程**：

```
QueryTree (语义表示)
    ↓
Planner::buildPlanForQueryNode
    ↓
QueryPlan (物理执行计划)
├─ ReadFromMergeTree (读取数据)
├─ FilterStep (WHERE)
├─ AggregatingStep (GROUP BY)
├─ FilterStep (HAVING)
├─ SortingStep (ORDER BY)
└─ LimitStep (LIMIT)
```

**关键接口**：

```cpp
class Planner {
  void buildQueryPlanIfNeeded();  // 按需构建 QueryPlan
  QueryPlan & getQueryPlan();     // 获取构建好的 QueryPlan
  
 private:
  QueryTreeNodePtr query_tree;    // 输入：已分析的 QueryTree
  QueryPlan query_plan;           // 输出：可执行的 QueryPlan
};
```

**示例**：对于 `SELECT a FROM t WHERE b > 10`，Planner 会：
1. 从 `QueryNode` 的 child 2（JOIN TREE）提取 `TableNode(t)`，生成 `ReadFromMergeTree` step；
2. 从 child 4（WHERE）提取 `FunctionNode(>, [ColumnNode(b), ConstantNode(10)])`，生成 `FilterStep`；
3. 从 child 1（PROJECTION）提取 `ColumnNode(a)`，生成投影逻辑（可能合并到 `ReadFromMergeTree` 的列裁剪中）。

### 5.3 在整体查询流程中的位置

```
┌─────────────────────────────────────────────────────────────────┐
│                      查询执行完整流程                              │
└─────────────────────────────────────────────────────────────────┘

SQL 文本
   ↓
Parser (src/Parsers/)
   ↓
AST (ASTSelectQuery, ASTFunction, ASTIdentifier, ...)
   ↓
QueryTreeBuilder::buildQueryTree  ← 本文档分析对象
   ↓
QueryTree (QueryNode, ColumnNode, FunctionNode, ...)
   ↓
QueryTreePassManager::run
   ├─ QueryAnalysisPass (resolve identifiers, infer types)
   ├─ FunctionToSubcolumnsPass
   ├─ NormalizeCountVariantsPass
   └─ ... (其他优化 passes)
   ↓
QueryTree (已解析、已优化)
   ↓
Planner::buildQueryPlanIfNeeded
   ↓
QueryPlan (ReadFromMergeTree, FilterStep, AggregatingStep, ...)
   ↓
QueryPlan::buildQueryPipeline
   ↓
QueryPipeline (Processors: SourceFromInputStream, FilterTransform, ...)
   ↓
Executors (PipelineExecutor, PullingPipelineExecutor)
   ↓
结果输出
```

**QueryTreeBuilder 的定位**：
- **输入**：Parser 产出的 AST（语法表示）；
- **输出**：未解析的 QueryTree（语义结构，但标识符尚未绑定）；
- **下游**：`QueryTreePassManager` 完成语义分析，`Planner` 生成执行计划。

### 5.4 与旧路径的对比

| 组件 | 旧路径（`InterpreterSelectQuery`） | 新路径（`InterpreterSelectQueryAnalyzer`） |
|------|-----------------------------------|------------------------------------------|
| 语义表示 | 直接在 AST 上操作 | AST → QueryTree（强类型） |
| 标识符解析 | `TreeRewriter` + `ExpressionAnalyzer` | `QueryTreePassManager` + `QueryAnalysisPass` |
| 优化 | 散落在 `ExpressionAnalyzer` / `SyntaxAnalyzer` | 统一在 `QueryTreePassManager` 的 passes 中 |
| 执行计划构建 | `InterpreterSelectQuery::executeXxx` | `Planner::buildPlanForQueryNode` |
| 子查询处理 | 通过 `Pipe input_pipe` 等多个构造重载 | QueryTree 自身的节点体系承载 |

**新路径的优势**：
- **强类型**：编译期类型安全，减少 `dynamic_cast`；
- **清晰的职责分离**：Builder 只做转换，PassManager 做分析，Planner 做计划；
- **可扩展的 pass 框架**：新增优化只需添加新 pass，不影响其他逻辑；
- **可逆转换**：`toAST` 可以把 QueryTree 转回 AST，用于分布式查询下发。

## 六、总结

### 6.1 QueryTree 的核心设计思想

1. **语义表示 vs 语法表示**：QueryTree 是查询的**语义结构**，与 AST 的**语法结构**形成对比。每个节点类型对应一个明确的语义概念（列、表、函数、查询等），而不是语法元素。

2. **强类型节点体系**：16 种节点类型覆盖查询的所有语义元素，编译期类型安全，避免了 AST 中大量的 `typeid` / `dynamic_cast`。

3. **Weak pointer 追踪来源**：通过 weak pointer 追踪列的来源（表、子查询、lambda），避免循环引用，同时保留语义关联。

4. **统一的别名机制**：所有节点都有 `alias` 成员，别名处理逻辑统一，不再散落在各处。

5. **清晰的子查询表示**：子查询是独立的 `QueryNode`，可以被引用、分析、优化，不再是嵌套的 `ASTSubquery`。

### 6.2 QueryTreeBuilder 的职责边界

- **只做结构转换**：将 AST 的语法结构转换为 QueryTree 的语义结构，不做标识符解析、类型推导、函数绑定等语义分析；
- **保留原始信息**：转换过程中保留 AST 的所有信息（别名、修饰符、子句顺序等），供后续 passes 使用；
- **不做优化**：优化工作交给 `QueryTreePassManager` 的 passes，Builder 只负责忠实转换。

### 6.3 学习要点

1. **理解 16 种节点类型**：每种节点类型的用途、children 结构、生命周期（构建 → 分析 → 计划）；
2. **掌握转换映射**：AST 类型到 QueryTree 节点的映射关系（见 4.1-4.6 的转换模式）；
3. **理解 JOIN TREE 构建**：多表 JOIN 如何构建为左深树，ARRAY JOIN 如何表示；
4. **理解与 passes 的配合**：Builder 产出未解析的 QueryTree，passes 完成语义分析和优化；
5. **理解与 Planner 的衔接**：QueryTree 是 Planner 的输入，Planner 将其转换为可执行的 QueryPlan。

### 6.4 阅读建议

1. **先看节点定义**：`src/Analyzer/IQueryTreeNode.h` 及各节点类型头文件（`ColumnNode.h`、`FunctionNode.h` 等）；
2. **再看 Builder 入口**：`QueryTreeBuilder.h` 的 `buildQueryTree` 函数；
3. **跟踪转换流程**：从 `buildSelectExpression` 开始，跟踪 SELECT 各子句的转换；
4. **对照 AST 定义**：对照 `src/Parsers/` 中的 AST 定义，理解转换映射；
5. **结合 passes 阅读**：看 `src/Analyzer/Passes/QueryAnalysisPass.cpp`，理解 `IdentifierNode` 如何解析为 `ColumnNode`；
6. **结合 Planner 阅读**：看 `src/Planner/Planner.cpp` 的 `buildPlanForQueryNode`，理解 QueryTree 如何转换为 QueryPlan。

---

> 文档生成时间: 2026-05-19
> 基于 `src/Analyzer/IQueryTreeNode.h`、`src/Analyzer/QueryTreeBuilder.{h,cpp}` 及各节点类型头文件
