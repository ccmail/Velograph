#pragma once

#include <Parsers/IAST.h>

namespace DB
{

enum class GraphEdgeDirection : uint8_t
{
    RIGHT,
    LEFT,
    UNDIRECTED,
    LEFT_OR_UNDIRECTED,
    UNDIRECTED_OR_RIGHT,
    LEFT_OR_RIGHT,
    ANY,
};

enum class GraphPathMode : uint8_t
{
    DEFAULT,
    WALK,
    TRAIL,
    SIMPLE,
    ACYCLIC,
};

enum class GraphPathSearch : uint8_t
{
    NONE,
    ALL,
    ANY,
    SHORTEST,
    ALL_SHORTEST,
    ANY_SHORTEST,
    COUNTED_SHORTEST,
    COUNTED_SHORTEST_GROUP,
};

enum class GraphLabelOp : uint8_t
{
    NAME,
    CONJUNCTION,
    DISJUNCTION,
    NEGATION,
    WILDCARD,
};


class ASTLabelExpression : public IAST
{
public:
    GraphLabelOp op = GraphLabelOp::NAME;
    String label_name;

    String getID(char) const override { return "LabelExpression"; }
    ASTPtr clone() const override;

protected:
    void formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override;
};


class ASTPathQuantifier : public IAST
{
public:
    static constexpr uint64_t UNLIMITED = UINT64_MAX;

    uint64_t min_hops = 0;
    uint64_t max_hops = UNLIMITED;

    String getID(char) const override { return "PathQuantifier"; }
    ASTPtr clone() const override;

protected:
    void formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override;
};


class ASTNodePattern : public IAST
{
public:
    String variable;
    String label;

    ASTLabelExpression * label_expression = nullptr;
    ASTPtr where_predicate;

    String getID(char) const override { return "NodePattern"; }
    ASTPtr clone() const override;

protected:
    void formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override;
};


class ASTEdgePattern : public IAST
{
public:
    String variable;
    String label;
    GraphEdgeDirection direction = GraphEdgeDirection::RIGHT;

    ASTLabelExpression * label_expression = nullptr;
    ASTPathQuantifier * quantifier = nullptr;
    ASTPtr where_predicate;

    String getID(char) const override { return "EdgePattern"; }
    ASTPtr clone() const override;

protected:
    void formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override;
};


class ASTPathPattern : public IAST
{
public:
    GraphPathMode path_mode = GraphPathMode::DEFAULT;
    GraphPathSearch search_prefix = GraphPathSearch::NONE;
    uint64_t search_count = 0;
    String path_variable;
    String subpath_variable;

    ASTPtr where_predicate;

    String getID(char) const override { return "PathPattern"; }
    ASTPtr clone() const override;

protected:
    void formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override;
};


class ASTGraphPattern : public IAST
{
public:
    String getID(char) const override { return "GraphPattern"; }
    ASTPtr clone() const override;

protected:
    void formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override;
};


class ASTGraphReturnItem : public IAST
{
public:
    String alias;

    String getID(char) const override { return "GraphReturnItem"; }
    ASTPtr clone() const override;

protected:
    void formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override;
};


class ASTGraphReturnClause : public IAST
{
public:
    bool distinct = false;

    ASTPtr group_by;
    ASTPtr order_by;
    ASTPtr limit;
    ASTPtr offset;

    String getID(char) const override { return "GraphReturnClause"; }
    ASTPtr clone() const override;

protected:
    void formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override;
};


class ASTGraphQuery : public IAST
{
public:
    String graph_name;
    bool is_optional_match = false;

    ASTGraphPattern * match_pattern = nullptr;
    ASTPtr where_condition;
    ASTGraphReturnClause * return_clause = nullptr;

    String getID(char) const override { return "GraphQuery"; }
    ASTPtr clone() const override;

    QueryKind getQueryKind() const override { return QueryKind::Select; }

protected:
    void formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override;
};

}
