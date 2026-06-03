#pragma once

#include <Parsers/IAST_fwd.h>
#include <base/types.h>

#include <vector>

namespace DB::Graph
{

enum class MatchEdgeDirection : UInt8
{
    Outgoing,
    Incoming,
    Undirected,
    IncomingOrOutgoing,
    IncomingOrUndirected,
    UndirectedOrOutgoing,
    Any,
};

enum class MatchMode : UInt8
{
    None,
    RepeatableElements,
    RepeatableElementBindings,
    DifferentEdges,
    DifferentEdgeBindings,
};

enum class MatchPathAlternationKind : UInt8
{
    None,
    Union,
    MultisetAlternation,
};

struct MatchNodeSpec
{
    String variable;
    ASTPtr label_expression;
    ASTPtr properties;
    ASTPtr predicate;
};

struct MatchEdgeSpec
{
    String variable;
    MatchEdgeDirection direction = MatchEdgeDirection::Any;
    ASTPtr label_expression;
    ASTPtr properties;
    ASTPtr predicate;
    ASTPtr quantifier;
};

struct MatchPathSpec
{
    String variable;
    ASTPtr prefix;
    MatchPathAlternationKind alternation_kind = MatchPathAlternationKind::None;
    std::vector<MatchPathSpec> alternatives;
    std::vector<MatchNodeSpec> nodes;
    std::vector<MatchEdgeSpec> edges;
};

struct MatchClauseSpec
{
    bool optional = false;
    bool has_match_mode = false;
    MatchMode match_mode = MatchMode::None;
    bool has_keep_clause = false;
    bool has_optional_operand_block = false;
    bool has_yield_items = false;
    ASTPtr keep_clause;
    ASTPtr optional_operand_block;
    ASTPtr where_clause;
    ASTs yield_items;
    std::vector<String> yield_variables;
    std::vector<MatchPathSpec> paths;
};

struct MatchSpec : public MatchClauseSpec
{
    ASTPtr graph_reference;
    std::vector<MatchClauseSpec> clauses;
};

}
