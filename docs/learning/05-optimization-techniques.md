# 查询优化技术

本文档总结 ClickHouse 中常见的查询优化技术及其在 InterpreterSelectQuery 中的应用。

## 一、查询优化概览

```
查询优化流程:

SQL 输入
    ↓
语法分析 (Parser)
    ↓
语义分析 (ExpressionAnalyzer)
    ↓
逻辑优化 (Logical Optimization)
    ↓
物理优化 (Physical Optimization)
    ↓
执行计划 (QueryPlan)
    ↓
执行 (Executor)
```

## 二、逻辑优化

### 2.1 谓词下推（Predicate Pushdown）

```
优化前:
  SELECT * FROM (SELECT * FROM t WHERE a > 1) WHERE b < 2

优化后:
  SELECT * FROM (SELECT * FROM t WHERE a > 1 AND b < 2)

效果: 减少中间结果集大小
```

### 2.2 列裁剪（Column Pruning）

```
优化前:
  SELECT a FROM (SELECT a, b, c FROM t)

优化后:
  SELECT a FROM (SELECT a FROM t)

效果: 减少读取的数据量
```

### 2.3 常量折叠（Constant Folding）

```
优化前:
  SELECT * FROM t WHERE a > 1 + 2

优化后:
  SELECT * FROM t WHERE a > 3

效果: 减少运行时计算
```

### 2.4 子查询优化

```
IN 子查询 → JOIN
EXISTS 子查询 → SEMI JOIN
标量子查询 → 常量替换
```

## 三、物理优化

### 3.1 分区裁剪（Partition Pruning）

```sql
-- 表定义
CREATE TABLE events (
    date Date,
    user_id UInt64,
    event_type String
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(date)
ORDER BY user_id;

-- 查询
SELECT * FROM events WHERE date >= '2024-01-01';

-- 优化: 只读取 2024年1月及之后的分区
```

### 3.2 索引使用

```sql
-- 主键索引
SELECT * FROM t WHERE user_id = 123;
-- 优化: 使用主键索引快速定位

-- 跳数索引
SELECT * FROM t WHERE value > 100;
-- 优化: 使用 minmax 跳数索引跳过不满足条件的 granules
```

### 3.3 排序优化

```sql
-- 利用主键排序
SELECT * FROM t ORDER BY user_id;

-- 优化: 如果表按 user_id 排序，避免额外排序
```

## 四、聚合优化

### 4.1 两阶段聚合

```
第一阶段: 局部聚合（每个线程/分片）
第二阶段: 全局聚合（合并局部结果）

局部聚合: GROUP BY key → 部分结果
全局聚合: 合并部分结果 → 最终结果
```

### 4.2 ORDER BY + LIMIT 优化

```sql
SELECT * FROM t ORDER BY a LIMIT 10;

-- 优化: 使用部分排序，不需要全局排序
```

## 五、JOIN 优化

### 5.1 JOIN 算法选择

```
Hash Join: 适用于大表 JOIN
Merge Join: 适用于有序数据
Nested Loop Join: 适用于小表
```

### 5.2 JOIN 顺序优化

```
-- 选择最优的 JOIN 顺序
-- 通常将小表放在前面
```

## 六、并行优化

### 6.1 并行读取

```cpp
// 并行副本设置
struct ParallelReplicasSettings {
    bool enabled = false;
    size_t num_replicas = 1;
};

// InterpreterSelectQuery 中的调整
bool adjustParallelReplicasAfterAnalysis();
```

### 6.2 多线程执行

```
数据分片 → 多线程并行处理 → 结果合并
```

## 七、物化视图

### 7.1 概念

```sql
-- 创建物化视图
CREATE MATERIALIZED VIEW mv_events
ENGINE = MergeTree()
ORDER BY user_id
AS SELECT user_id, count() as event_count
   FROM events
   GROUP BY user_id;

-- 查询时自动使用
SELECT user_id, event_count FROM mv_events WHERE user_id = 123;
```

### 7.2 查询重写

```
查询: SELECT user_id, count() FROM events GROUP BY user_id

优化器: 检查是否有可用的物化视图
       ↓
重写: 从物化视图读取
```

## 八、查询优化工具

### 8.1 EXPLAIN

```sql
-- 查看查询计划
EXPLAIN SELECT * FROM t WHERE a > 1;

-- 查看 AST
EXPLAIN AST SELECT * FROM t WHERE a > 1;

-- 查看优化后的查询
EXPLAIN SYNTAX SELECT * FROM t WHERE a > 1;
```

### 8.2 系统表

```sql
-- 查询日志
SELECT * FROM system.query_log WHERE query LIKE '%SELECT%';

-- 查询优化信息
SELECT * FROM system.query_views_log;
```

## 九、常见优化建议

1. **选择合适的主键**: 影响数据分布和查询性能
2. **使用分区**: 减少扫描数据量
3. **避免 SELECT ***: 只查询需要的列
4. **使用 PREWHERE**: 提前过滤数据
5. **合理设置索引**: 跳数索引、主键索引
6. **避免大 IN 子查询**: 考虑使用 JOIN
7. **使用物化视图**: 预计算常用查询

## 十、源码路径

```
src/
├── Optimizers/
│   ├── QueryOptimizer.h/.cpp
│   └── ...
├── Interpreters/
│   ├── InterpreterSelectQuery.h/.cpp
│   └── ...
└── Storages/
    └── ...
```

---

> 文档生成时间: 2025-05-18
