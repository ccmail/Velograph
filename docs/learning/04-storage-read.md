# 存储层与数据读取

本文档分析 ClickHouse 中存储层的设计及其与查询执行的关系。

## 一、Storage 接口

### 1.1 核心接口

```cpp
class IStorage {
public:
    // 读取数据
    virtual Pipe read(const Names &column_names,
                      const StorageSnapshotPtr &storage_snapshot,
                      SelectQueryInfo &query_info,
                      ContextPtr context,
                      QueryProcessingStage::Enum processed_stage,
                      size_t max_block_size,
                      size_t num_streams) = 0;

    // 获取表结构
    virtual ColumnsDescription getColumns() const = 0;

    // 获取主键描述
    virtual std::optional<UInt64> getTrivialCount() const { return {}; }

    // ...
};
```

### 1.2 与 InterpreterSelectQuery 的关系

```cpp
class InterpreterSelectQuery {
private:
    StoragePtr storage;                    // 表存储引擎
    StorageMetadataPtr metadata_snapshot;  // 元数据快照
    StorageSnapshotPtr storage_snapshot;     // 存储快照
    TableLockHolder table_lock;            // 表锁
};
```

## 二、MergeTree 存储引擎

### 2.1 架构

```
MergeTree
    ├── Data Parts (数据片段)
    │       ├── Primary.idx (主键索引)
    │       ├── MinMax.idx (分区索引)
    │       ├── Column.bin (列数据)
    │       └── Column.mrk (标记文件)
    └── Mutations (变更)
```

### 2.2 读取流程

```
1. 确定需要读取的 Parts
2. 应用分区裁剪
3. 应用主键索引
4. 读取列数据
5. 合并结果
```

## 三、数据读取接口

### 3.1 ReadInOrder

```cpp
// 有序读取（利用主键索引）
class ReadInOrder {
public:
    // 获取读取范围
    std::vector<Range> getReadRanges();

    // 获取排序信息
    InputOrderInfoPtr getInputOrderInfo() const;
};
```

### 3.2 并行读取

```cpp
// 并行读取设置
struct ParallelReplicasSettings {
    bool enabled = false;
    size_t num_replicas = 1;
    // ...
};

// InterpreterSelectQuery 中的并行副本调整
bool adjustParallelReplicasAfterAnalysis();
```

## 四、列裁剪与投影

### 4.1 列裁剪

```cpp
// 只读取需要的列
Names required_columns;

// 从查询中提取需要的列
void extractRequiredColumns() {
    // 分析 SELECT, WHERE, GROUP BY 等子句
    // 确定需要读取的列
}
```

### 4.2 PREWHERE 优化

```cpp
// PREWHERE 条件
void addPrewhereAliasActions();
bool shouldMoveToPrewhere() const;

// PREWHERE 执行
void executeWhere(QueryPlan &query_plan,
                  const ActionsAndProjectInputsFlagPtr &expression,
                  bool remove_filter);
```

## 五、分区裁剪

### 5.1 分区键

```sql
CREATE TABLE t (
    date Date,
    user_id UInt64,
    value Float64
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(date)
ORDER BY user_id;
```

### 5.2 裁剪逻辑

```
查询: SELECT * FROM t WHERE date >= '2024-01-01'

1. 分析 WHERE 条件
2. 确定 affected partitions: ['202401', '202402', ...]
3. 只读取这些分区的数据
```

## 六、索引使用

### 6.1 主键索引

```
主键: (user_id, event_time)

查询: WHERE user_id = 123
效果: 快速定位到 user_id = 123 的数据范围
```

### 6.2 跳数索引

```sql
CREATE TABLE t (
    user_id UInt64,
    value Float64,
    INDEX idx_value value TYPE minmax GRANULARITY 4
) ENGINE = MergeTree()
ORDER BY user_id;
```

## 七、数据缓存

### 7.1 缓存层次

```
1. Page Cache (OS 层)
2. Mark Cache (ClickHouse 层)
3. Uncompressed Cache (ClickHouse 层)
4. Query Cache (ClickHouse 层)
```

### 7.2 缓存策略

```cpp
// Mark Cache
class MarkCache {
public:
    // 获取标记
    MarkInCompressedFilePtr get(const String &key);

    // 设置标记
    void set(const String &key, MarkInCompressedFilePtr mark);
};
```

## 八、源码路径

```
src/
├── Storages/
│   ├── IStorage.h/.cpp
│   ├── StorageMergeTree.h/.cpp
│   ├── StorageDistributed.h/.cpp
│   └── ...
└── Interpreters/
    ├── InterpreterSelectQuery.h/.cpp
    └── ...
```

---

> 文档生成时间: 2025-05-18
