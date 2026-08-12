# 04 谓词下推：三条通道与归属规则

> 服务里程碑：M3（叶子源通道）、M4（驱动型算子通道）。
> 前置阅读：`00_overview.md`、`03_plan_expansion.md`。

## 1. 全景

谓词归一化（01 号文档）保证：进入优化器时，谓词以标准 `FilterStep` 形态
悬在展开后的算子链上方。下推分三条通道，全部作用于**物理子算子**（D2）：

```text
FilterStep(a.age>20 AND e.weight>0.5)          ┐ 通道A: tryPushDownFilter（现有 pass，扩展）
  MatchVertexLookupStep(b)                      │   把只引用下游列的 conjunct 拆出、下移
    MatchExpandStep(e: a→b)                     ┤ 通道C: 图专用绑定 pass（新增）
      MatchVertexStep(a)                        ┘ 通道B: optimizePrimaryKeyConditionAndLimit（现有 pass，零改动）
```

- **通道A（first pass，谓词下移）**：`tryPushDownFilter` 沿树把 `FilterStep`
  中"仅引用某子树输出列"的 conjunct 拆分下移。北极星查询里 `a.age>20`
  借此从链顶一路移到 `MatchVertexStep(a)` 正上方。
- **通道B（second pass，叶子吸收）**：`optimizePrimaryKeyConditionAndLimit`
  对一切 `SourceStepWithFilterBase` 泛化生效（`.cpp:12` 的 `dynamic_cast`），
  自动把叶子源上方的 `FilterStep` DAG `addFilter` 进 step。
  `MatchVertexStep`/`MatchEdgeStep` 继承基类即接入，**不写一行优化器代码**。
- **通道C（second pass，驱动型算子绑定）**：驱动型算子不是叶子，通道B 不覆盖；
  新增图 pass 把悬在 expand/lookup 正上方的 conjunct 绑定进算子（第 3 节）。

## 2. 通道A 扩展：教 `tryPushDownFilter` 认识图算子

`tryPushDownFilter`（`filterPushDown.cpp`）按 step 类型白名单决定
"哪些列穿过该 step 后保持不变、可以继续下移"。为两类驱动型算子增加分支：

- `MatchExpandStep(e: src→tgt)`：输入列全部原样穿过 → 仅引用输入列的
  conjunct 可下移到其下（`a.age>20` 走此分支）。引用 `e.*` / `tgt` 列的
  conjunct 不可下移（它们由本算子产出），留在原地等通道C。
- `MatchVertexLookupStep(v)`：同理，仅引用输入列的 conjunct 可下移。

实现模式照抄该 pass 内现有的 transform 分支（拆分 `FilterStep` DAG →
`splitActionsForFilterPushDown` → 下移子 DAG、残余保留）。这是机械扩展，
不引入新决策。

## 3. 通道C：`bindFiltersToMatchOperators`（新增图 pass）

- 位置：`optimizeTreeSecondPass`，紧邻 `optimizePrimaryKeyConditionAndLimit`
  的调用点之后（同为自顶向下 Stack 遍历风格）。
- 规则：命中 `MatchExpandStep` / `MatchVertexLookupStep` 时，检查其正上方是否为
  `FilterStep`（允许隔 `ExpressionStep`，处理方式与
  `optimizePrimaryKeyConditionAndLimit.cpp:30-53` 的 expression 穿透一致）：
  把 filter DAG **克隆**并 `bindFilter` 进算子。
- **绑定语义 = 扫描端预过滤提示**，与正确性解耦：
  - expand 收到的 DAG 里，引用 `e.*` / `tgt` id 的部分可转成存储谓词
    （05 号文档），引用不满足的部分算子内忽略——因为上层 `FilterStep` 仍在。
  - 判定"DAG 的哪部分可转"：对 conjunct 逐个检查输入列 ⊆ 本算子可下推列集
    （expand 可下推列 = `e.*` 全体 + `tgt`；lookup = `v.*` 全体）。
    拆分复用 `ActionsDAG::splitActionsForFilterPushDown` 系列工具。

## 4. 安全语义：copy-not-remove（D8）

三条通道统一遵守：**下推/绑定只复制谓词，从不删除上层 `FilterStep`**。

- 通道B 天生如此（`ReadFromMergeTree` 同款语义：谓词用于索引裁剪与 prewhere，
  plan 上的 `FilterStep` 保留）。通道A 的"下移"是 `FilterStep` 位置变化，
  不改变过滤总语义。通道C 显式克隆。
- 推论：任何通道对某 conjunct 的下推**不完整或干脆不做，结果仍正确**，
  只损失性能。这使 M3/M4 可以逐算子渐进交付，也使"下推开/关对照"成为
  每个里程碑的标准验收手段。
- 移除冗余 `FilterStep`（prewhere 化）是 M5 之后的优化，需要精确的
  "已完全消化" 证明，本轮禁止做。

## 5. 归属矩阵（北极星查询及扩展示例）

| conjunct | 引用列 | 最终归属 | 通道 |
|---|---|---|---|
| `a.age > 20` | `a.age` | `MatchVertexStep(a)` → 存储 prewhere | A 下移 + B 吸收 |
| `has(a.labels,'Person')` | `a.labels` | 同上 | 同上 |
| `e.weight > 0.5` | `e.weight` | `MatchExpandStep(e)` → `getNeighbors` 谓词 | C 绑定 |
| `e.type = 'KNOWS'` | `e.type` | 同上（存储侧命中排序键第 2 列，D9） | C 绑定 |
| `b.age > 25` | `b.age` | `MatchVertexLookupStep(b)` → `getVertex` 谓词 | C 绑定 |
| `a.age + b.age > 50` | `a.age`,`b.age` | 不归属，留 `FilterStep` | 无 |
| `id(a) IN (1,2,3)`（字面量；解析为 id 列 `a` 的 IN，见 02 号文档第 2 节） | `a` | `MatchVertexStep(a)` → `KeyCondition` 跳 granule | A + B |

## 6. 验收要点

- M3：`MATCH (a) WHERE a.age > 20 AND id(a) IN (...) RETURN a.name`：
  `EXPLAIN actions=1` 显示 `MatchVertex` 携带 filter；对 id 谓词，存储读取的
  granule 数下降（`ProfileEvents::SelectedMarks` 断言）；`a.age` 谓词在
  存储侧生效（读出行数 < 全表）。下推开关两态结果一致。
- M4：北极星查询三个归属全部按第 5 节矩阵落位（`EXPLAIN` 断言）；
  不可归属谓词保留为 `FilterStep`。
- 负例：谓词引用两个变量时不得绑定到任何单算子（防止过度下推的正确性回归）。
