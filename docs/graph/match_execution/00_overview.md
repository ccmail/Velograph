# GQL MATCH 执行架构总览

> 本目录是 `MATCH` 查询从 analyzer 到 `MergeTree` 存储的**权威设计**。执行本计划的
> worker agent 必须先读完本文，再按模块文档实现；所有决策已在文档中定死，
> 实现时不允许自行引入新的架构决策。若发现文档与代码现实冲突、或必须新增决策，
> 停下来向用户汇报，不要自行拍板。
>
> 阅读顺序：`00_overview`（本文）→ 各模块文档（按里程碑需要）→ `06_milestones`。

## 1. 北极星示例

全部设计围绕这条查询展开，后文简称"北极星查询"：

```
MATCH (a)-[e]->(b) WHERE a.age > 20 AND e.weight > 0.5
RETURN a.name, b.name
```

目标行为：`a.age > 20` 下推到点 `a` 的扫描算子、`e.weight > 0.5` 下推到边扩展算子，
均在 `MergeTree` 扫描时完成过滤（`KeyCondition` 跳 granule + prewhere 行过滤 + 列裁剪，
一次读取内完成）；无法归属单一算子的谓词（如 `a.age + b.age > 50`）保留为 `FilterStep`。

## 2. 五层架构

```text
GQL text
  │  ParserGQLQuery（Dialect::gql）
  ▼
GQL* IAST
  │  GQLQueryTreeBuilder + 通用 QueryTreePassManager（注册 GQL passes，含谓词归一化）
  ▼
GQL QueryTree                                        ← 层1 Analyzer
  │  buildGQLQueryPlanFromTree / planMatchFromTree
  ▼
朴素 QueryPlan：MatchStep(逻辑) + FilterStep + …      ← 层2 Planner（不做任何优化决策）
  │  QueryPlan::buildQueryPipeline → optimizeTree
  │    (a) expandMatchSteps：MatchStep → 物理子算子树   ← 层3 优化器
  │    (b) 现有 pass：FilterStep 下移、叶子源吸收谓词
  │    (c) 新增 pass：谓词绑定到 expand / lookup 算子
  ▼
物理 QueryPlan：MatchVertexStep / MatchExpandStep / …
  │  各算子 initializePipeline                          ← 层4 物理算子
  ▼
IGraphStorage 遍历原语（scan / getVertex / getEdge / getNeighbors）
  │  组装 SelectQueryInfo → MergeTreeDataSelectExecutor::read
  ▼
内部 MergeTree 表（vertices / edges_forward / edges_reverse）  ← 层5 存储
```

与 SQL `SELECT` 路径逐层类比（实现时优先模仿对应机制）：

| GQL 侧 | SQL 侧对应物 | 复用程度 |
|---|---|---|
| GQL 专用 passes 注册到通用 `QueryTreePassManager` | `QueryTreePassManager` | **直接复用** |
| `planMatchFromTree` 产出朴素计划 | `Planner` 产出 `ReadFromMergeTree` + `FilterStep` | 结构模仿 |
| `expandMatchSteps`（逻辑→物理） | `convertLogicalJoinToPhysical`（`Optimizations.h:195`） | 结构模仿 |
| `MatchVertexStep` 继承 `SourceStepWithFilterBase` | `ReadFromMergeTree` 同款基类 | **直接复用** |
| 叶子源吸收谓词 | `optimizePrimaryKeyConditionAndLimit`（对基类泛化，`optimizePrimaryKeyConditionAndLimit.cpp:12`） | **直接复用，零改动** |
| `FilterStep` 穿透 transform 下移 | `tryPushDownFilter`（`Optimizations.h:146`） | 扩展（加图算子分支） |
| 谓词绑定到 expand/lookup | 无直接对应（新增图 pass） | 新增 |
| 原语内 `SelectQueryInfo` 装配 | `filter_actions_dag`/`prewhere_info`/`prepared_sets`（`SelectQueryInfo.h:167,176,186`） | **直接复用** |

## 3. 核心设计原则（P1–P6）

- **P1 谓词等价性**：顶层 `WHERE`、模式内联 `WHERE`、属性 map `{age: 20}`、
  标签表达式 `(a:Person)`，一律在 analyzer 归一为同一个合取谓词。
  下推收益只取决于谓词引用的列，与书写位置无关。
- **P2 planner 零优化**：planner 只产出"逻辑 `MatchStep` + `FilterStep`"的朴素计划。
  所有下推、绑定、展开决策都发生在 QueryPlan 优化器。
- **P3 fail-closed**：不支持的模式形态必须抛 `NOT_IMPLEMENTED` 异常，
  禁止以空结果伪装成功（现 `MatchSource` 发空行的行为要消灭）。
- **P4 原生接入**：存储路径全程 C++ 原生 API（`ActionsDAG`、`KeyCondition`、
  `MergeTreeDataSelectExecutor::read`），禁止拼接 SQL 文本再解析。
- **P5 优先复用优化器机制**：能用现有 pass（`tryPushDownFilter`、
  `optimizePrimaryKeyConditionAndLimit`）解决的，不新写图专用逻辑。
- **P6 名称边界**：计划层列名带变量前缀（`a.age`）；存储原语只认存储列名（`age`、
  `__ID__`）。翻译发生在物理算子内部，原语签名不出现计划层名字。

## 4. 决策记录

| # | 决策 | 依据 |
|---|---|---|
| D1 | 只建设 analyzer 路径（`InterpreterGQLQueryAnalyzer`）；direct planner（`InterpreterGQLQuery`）冻结为遗留，不接存储，按清理轨道退役 | 用户拍板 |
| D2 | planner 产出单个逻辑 `MatchStep`；优化开始前由 `expandMatchSteps` 无条件展开为物理子算子树；**参与优化器的是子算子** | 用户拍板 |
| D3 | 物理算子集合：`MatchVertexStep`、`MatchEdgeStep`（叶子源）、`MatchExpandStep`、`MatchVertexLookupStep`（驱动型 transform）、`MatchJoinStep`（M5 预留） | 03 号文档 |
| D4 | 谓词载体一律 `ActionsDAG`；`IGraphStorage::getNeighbors` 现有 `ASTPtr filter_pushdown` 参数废除改型 | 04/05 号文档 |
| D5 | 所有谓词来源归一进单一 `WHERE` 合取（P1）；`MatchSpec` 中按元素存 AST 谓词的字段不再作为下推通道 | 用户拍板 |
| D6 | 属性列采用 eager 投影：analyzer 收集被引用属性，`MatchStep` header 直接携带 `a.age` 形式的列 | 用户认可 |
| D7 | 存储入口用 `MergeTreeDataSelectExecutor::read` + `SelectQueryInfo` 注入，不手工走 `readFromParts` | 机制核实 |
| D8 | 谓词下推初期 copy-not-remove：绑定到算子的谓词不从上层 `FilterStep` 移除，保证语义安全 | 04 号文档 |
| D9 | 标签/类型约束降为普通谓词：`(a:Person)` → `has(a.labels, 'Person')`，边 `[e:KNOWS]` → `e.type = 'KNOWS'`（`type` 在边表排序键内，天然享受 `KeyCondition` 前缀匹配） | P1 推论 |
| D10 | 展开锚点 M4 固定为模式最左点；代价驱动的锚点/顺序选择推迟到 M5 | 渐进原则 |

## 5. 模块文档索引

| 文档 | 内容 | 服务里程碑 |
|---|---|---|
| `01_planner.md` | analyzer 路径 planner 现状缺口、谓词归一化 pass、graph 解析 | M1 / M2 |
| `02_property_columns.md` | 属性列模型：命名、收集、类型、翻译边界 | M2 |
| `03_plan_expansion.md` | `expandMatchSteps` pass 与五个物理算子的完整契约 | M1 / M4 |
| `04_filter_pushdown.md` | 三条下推通道、conjunct 归属规则、安全语义 | M3 / M4 |
| `05_storage_primitives.md` | 原语签名演进、`SelectQueryInfo` 装配、运行时 id 集合注入、已知缺陷修复 | M3 / M4 |
| `06_milestones.md` | M0–M5 里程碑、验收标准、清理轨道 | 全部 |

## 6. 术语表

| 术语 | 含义 |
|---|---|
| 逻辑 `MatchStep` | planner 产出的单一 source step，携带 `MatchSpec`，只活到 `expandMatchSteps` 为止 |
| 物理算子 / 子算子 | `MatchVertexStep` 等，展开后真正参与优化与执行的 QueryPlan step |
| 锚点（anchor） | 展开链的起始叶子源（当前固定为模式最左点） |
| 驱动型算子 | 消费上游行、按行内 id 批量调存储原语的 transform（expand / lookup） |
| conjunct | 归一化后 `WHERE` 合取式中的单个合取项 |
| 属性列 | 计划 header 中形如 `a.age` 的列，见 `02_property_columns.md` |

## 7. 非目标（本轮不做）

- 分布式执行、事务、实时删除、CSR 内存热层。
- 变长路径量词、`OPTIONAL MATCH`、路径变量物化、多路径模式组合（M5 起步）。
- 代价模型与模式重排（M5 评估）。
- 图 catalog（映射用户已有表，见 `../catalog.md`）——当前用内部表引擎
  `GraphStorageEngine`，catalog 是后续独立轨道。
