# 06 里程碑与验收

> 执行规则：里程碑严格串行交付（M2 依赖 M1，依此类推）；每个里程碑一个
> `storage/dev-*` 或 `interpreter/dev-*` 开发分支；实现中禁止引入文档未记录的
> 架构决策（`00_overview.md` 开头的约束）。构建、测试、提交信息遵循仓库根
> `AGENTS.md` 既有约定。

## M0 文档定稿与清理（本轮）

设计文档（本目录）由设计负责人产出，已完成。遗留给 worker 的清理任务：

1. `.claude/learnings.md`（下列编号为当前行序数，仅供初始定位；移动条目会使
   行号漂移，**操作前必须按括号内内容摘要确认目标条目**）：
   - 文末新增 `## Archived（历史参考）` 节，将 #1（`visitSimpleMatchStatement`
     的 `WHERE` 分歧）、#5、#6（clang-format 增量机制）、#7、#8（reformat 事故）、
     #12（AST root 重构旧头文件）、#17（`USE` 原文 clause）、#25
     （`visitSelectStatement` 时序）、#41（删 Praktika CI）移入；
     #94（删 `PlanEnvironment`）精简并入 #95，原条目移入 Archived。
   - **#65（`MATCH WHERE` 存入 `MatchSpec` 供存储下推）移入 Archived**，标注
     "superseded：谓词下推改由 QueryPlan 优化器绑定，见
     `docs/graph/match_execution/04_filter_pushdown.md`"。
   - **#100 改写**为："构造 `SELECT ... WHERE` AST 走 `InterpreterSelectWithUnionQuery`
     的做法已废弃（禁止拼 SQL）；图原语经 `read()` + `SelectQueryInfo`
     （`filter_actions_dag` / `prewhere_info` / 内嵌 `ColumnSet` 的 IN 条件）
     获得 `KeyCondition` 与 prewhere，见 `05_storage_primitives.md`"。
   - 新增一条："GQL 顶层活路径是 `InterpreterGQLQueryAnalyzer` →
     `buildGQLQueryPlanFromTree` → `planMatchFromTree`；`MatchPlanner.cpp` 的
     `resolveGraphStorage` 属 direct planner 遗留路径，顶层查询不经过。"
2. `.claude/gql_refactor_status.md`：文首加"本文件为历史脉络，interpreter/analyzer
   与存储接入现状以 `docs/graph/match_execution/` 为准"；`*Lowering` 命名与
   `PlanEnvironment`/`RootLowering`/`MatchSourceFactory` 段落标注已废弃；
   dispatch 描述改为 `InterpreterGQLQueryAnalyzer`（`InterpreterFactory.cpp:140`）。
3. 已删除文档（设计负责人已执行，git 历史可查）：`gql_runtime_flow.md`、
   `gql_ast_interpreter_todo.md`、`gql_analyzer_refactoring_plan.md`、
   `roadmap.md`、`operators.md`。若发现引用残留，指向本目录对应文档。

## M1 竖切：`MATCH (n) RETURN n` 出真实数据

- 范围：01 号文档第 5 节（graph 解析）+ 第 3 节（`MatchStep` 逻辑化）+
  03 号文档单点展开 + 05 号文档缺陷表的 switch 兜底修复。
- 交付物：
  - `GraphResolver.{h,cpp}`；`planMatchFromTree` 接真实 storage，失败抛异常；
  - `expandMatchSteps` pass（仅支持单点模式）+ `MatchVertexStep`（暂不含谓词，
    继承 `SourceStepWithFilterBase` 但 `applyFilters` 收到的 DAG 先忽略）；
  - 删除 `MatchSource` 与 `MatchStep::initializePipeline` 的临时 scan 分支；
  - 不支持形态在展开 pass 抛 `NOT_IMPLEMENTED`。
- 验收：内部表写入数据后，`clickhouse local --dialect=gql` 执行
  `MATCH (n) RETURN n` 返回全部顶点 id；`MATCH (a)-[e]->(b) ...` 抛
  `NOT_IMPLEMENTED`（不再静默空结果）；gtest 覆盖 graph 解析失败路径。
- 明确不做：属性列、谓词、多元素模式。

## M2 属性列 + 谓词归一化（朴素执行）

- 范围：02 号文档全部 + 01 号文档第 4 节。
- 交付物：QueryTree 属性访问节点与解析（builder + `GQLNameResolutionPass` 扩展）、
  `GQLPredicateNormalizationPass`、`referenced_columns` 收集、
  `MatchStep`/`MatchVertexStep` header 按约定携带属性列、
  存储 Stage-1 `NameSet` 投影接通。
- 验收：`MATCH (a) WHERE a.age > 20 RETURN a.name` 全扫描 + `FilterStep`
  结果正确；四类谓词来源（顶层/内联/属性 map/标签）等价（同一数据四种写法
  同结果）；列裁剪生效（02 号文档第 6 节）；未知属性抛 `UNKNOWN_IDENTIFIER`。
- 明确不做：任何下推（谓词仍在 `FilterStep` 求值）。

## M3 叶子源谓词下推

- 范围：04 号文档通道 B + 05 号文档签名演进与 `buildReadQueryInfo`
  （`scan`/`getVertex` 先行；`getEdge`/`getNeighbors` 签名同步改、实现留空）。
- 交付物：`MatchVertexStep::applyFilters` 生效；计划层→存储层谓词翻译；
  `buildReadQueryInfo` 装配 `filter_actions_dag` + `prewhere_info` + IN 集合。
- 验收：04 号文档第 6 节 M3 条目（`EXPLAIN` 断言 + `SelectedMarks` 下降 +
  两态对照）；05 号文档第 6 节的 `scan`/`getVertex` gtest。
- 明确不做：通道 A/C、expand。

## M4 单跳 expand 链（北极星查询）

- 范围：03 号文档展开规则与驱动型算子 + 04 号文档通道 A/C +
  05 号文档 `getNeighbors`/`getVertex` 运行时 id 注入 + 缺陷表 UNDIRECTED
  分支删除与 `edges_reverse` 布局验证。
- 交付物：`MatchExpandStep`、`MatchVertexLookupStep` 及其 transform processor；
  `tryPushDownFilter` 图算子分支；`bindFiltersToMatchOperators` pass。
- 验收：北极星查询端到端正确（与 M2 朴素基线两态对照）；谓词归属符合
  04 号文档第 5 节矩阵；`Incoming`/`Undirected` 方向语义正确
  （含 reverse 表对齐 gtest）；多跳线性模式 `(a)-[]->(b)-[]->(c)` 顺带验收
  （展开规则天然支持链式）。
- 明确不做：join 型组合、变长路径、`limit_per_vertex` 实现、多流 expand。

## M5 展望（开工前需补充设计，本轮不实现）

- `MatchJoinStep`（多 path、环闭合；候选复用 `JoinStepLogical`）；
- 变长路径量词（迭代 BFS/frontier 模型，历史设计见已删除的 `operators.md`，
  git 历史可查）；`OPTIONAL MATCH`；
- 代价驱动：锚点选择、expand-vs-join 择优、谓词下推后的冗余 `FilterStep` 消除；
- direct planner 退役：`SubqueryPlanner.cpp:117` 子查询迁移 QueryTree 路径后，
  删除 `InterpreterGQLQuery`、`SourcePlanner`/`MatchPlanner`/`MatchSpecBuilder`
  等旧路径文件与 `MatchSpec` 中废弃字段。

## 全局验收基线（每个里程碑重复执行）

1. 正确性对照：涉及下推的里程碑必须提供"下推关闭（或注释掉 pass）vs 开启"
   的结果一致性测试。
2. fail-closed 回归：不支持形态必须抛异常，禁止空结果；每个里程碑把自己
   新排除的形态补进 gtest。
3. `EXPLAIN` 快照：代表查询的 `EXPLAIN PLAN` 输出纳入测试断言，
   计划形态变化必须显式过 review。
