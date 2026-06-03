---
name: code-arch-doc
description: |
  将代码架构分析对话自动归档为学习文档。当用户询问 ClickHouse 代码架构相关问题后，
  调用此 skill 将本次（或之前）的问答内容整理为结构化的 Markdown 文档，
  保存到 docs/learning/ 目录下，方便后续 review。
argument-hint: "[可选: 文档主题描述]"
disable-model-invocation: false
allowed-tools: Task, Read, Write, Edit, Bash(ls:*), Bash(grep:*)
---

# 代码架构文档归档 Skill

## 用途

将关于 ClickHouse 代码架构的问答对话，整理为结构化的学习文档，保存到 `docs/learning/` 目录。

## 调用时机

- 用户明确要求"记录一下"、"写成文档"、"归档"等
- 用户询问代码架构相关问题后，希望保存分析结果
- 每次分析完代码架构后，主动调用此 skill 进行归档

## 参数

- `$0` (可选): 文档主题描述，如 "InterpreterSelectQuery 设计分析"

## 执行步骤

### Step 1 — 确定文档信息

1. **提取对话主题**: 从最近的问答中提取核心主题
   - 如果是关于某个类的分析，使用类名作为主题
   - 如果是关于某个流程的分析，使用流程名作为主题
   - 如果用户提供了主题描述，优先使用用户的描述

2. **确定文件名**: 使用 `NN-主题名.md` 格式，其中 NN 是序号
   - 查看 `docs/learning/` 目录下已有文件，确定下一个序号
   - 例如：`01-interpreter-select-query.md`, `02-query-plan-pipeline.md`

3. **确定文档结构**:
   ```markdown
   # 标题

   ## 一、概述
   简要说明这个组件/流程是什么，在整体架构中的位置

   ## 二、核心设计
   分析核心设计思路、关键数据结构、算法等

   ## 三、关键代码分析
   分析关键代码片段、函数调用链等

   ## 四、与其他组件的关系
   说明与上下游组件的交互

   ## 五、总结
   总结设计要点、学习收获
   ```

### Step 2 — 整理文档内容

将对话中的分析内容整理为结构化的 Markdown 文档：

1. **保留核心分析**: 保留对话中的关键分析、设计模式、代码解读
2. **补充上下文**: 添加必要的背景信息，使文档独立可读
3. **代码片段**: 保留重要的代码片段，使用代码块格式化
4. **图表**: 使用 ASCII 图表或文字描述架构关系
5. **引用源码**: 标注相关源码路径，方便后续查阅

### Step 3 — 写入文档

1. **检查目录**: 确认 `docs/learning/` 目录存在
2. **写入文件**: 使用 Write 工具写入 Markdown 文件
3. **更新索引**: 更新 `docs/learning/README.md` 索引文件

### Step 4 — 确认完成

向用户报告文档已保存，包括：
- 文档路径
- 文档主题
- 建议的后续阅读文档

## 文档命名规范

```
docs/learning/
├── README.md                          # 文档索引
├── 00-overview.md                     # 总览
├── 01-topic-name.md                   # 具体主题
├── 02-another-topic.md
└── ...
```

- 序号从 00 开始，00 用于总览类文档
- 主题名使用英文小写，单词间用连字符分隔
- 文件名不要包含特殊字符

## 文档模板

```markdown
# 标题

> 本文档分析 XXX 的设计与实现。

## 一、概述

简要说明这个组件/流程是什么，在整体架构中的位置。

## 二、核心设计

### 2.1 设计思路
分析核心设计思路。

### 2.2 关键数据结构
分析关键数据结构。

### 2.3 算法/流程
分析关键算法或流程。

## 三、代码分析

### 3.1 关键代码片段

\`\`\`cpp
// 代码示例
\`\`\`

### 3.2 调用链分析

\`\`\`
函数A
  → 函数B
    → 函数C
\`\`\`

## 四、与其他组件的关系

```
组件A → 本文档组件 → 组件B
```

## 五、总结

总结设计要点、学习收获。

---

> 文档生成时间: YYYY-MM-DD
> 基于 ClickHouse 源码分析
```

## 索引更新

在 `docs/learning/README.md` 中添加新文档的条目：

```markdown
### N. [NN-topic-name.md](NN-topic-name.md) - 文档标题

**内容**: 文档内容摘要

**适用阶段**: 学习阶段建议

**关键词**: 关键词1、关键词2、关键词3
```

## 示例

### 示例 1: 分析 InterpreterSelectQuery 后归档

用户问: "讲一下 InterpreterSelectQuery 怎么设计的"

AI 分析后，调用此 skill：

```
/code-arch-doc InterpreterSelectQuery 设计分析
```

生成文档: `docs/learning/01-interpreter-select-query.md`

### 示例 2: 分析 QueryPipeline 后归档

用户问: "QueryPipeline 是怎么执行的？"

AI 分析后，调用此 skill：

```
/code-arch-doc QueryPipeline 执行流程分析
```

生成文档: `docs/learning/02-query-pipeline-execution.md`

## 注意事项

1. **不要重复**: 检查是否已有类似主题的文档，避免重复
2. **保持简洁**: 文档应精炼，保留核心分析，去除闲聊内容
3. **独立可读**: 文档应包含足够的上下文，独立阅读也能理解
4. **及时更新**: 如果分析有更新或修正，更新已有文档而非新建
5. **版本管理**: 文档中包含生成时间，方便追踪

## 相关文档

- [00-overview.md](docs/learning/00-overview.md) - 查询执行流程总览
- [README.md](docs/learning/README.md) - 文档索引
