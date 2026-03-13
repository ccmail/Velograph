#include <Parsers/graph/ASTGraphQuery.h>
#include <IO/WriteHelpers.h>
#include <IO/Operators.h>

namespace DB
{

ASTPtr ASTLabelExpression::clone() const
{
    auto res = make_intrusive<ASTLabelExpression>(*this);
    res->children.clear();
    for (const auto & child : children)
        res->children.push_back(child->clone());
    return res;
}

void ASTLabelExpression::formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const
{
    switch (op)
    {
        case GraphLabelOp::NAME:
            ostr << label_name;
            break;
        case GraphLabelOp::WILDCARD:
            ostr << "%";
            break;
        case GraphLabelOp::NEGATION:
            ostr << "!";
            if (!children.empty())
                children[0]->format(ostr, settings, state, frame);
            break;
        case GraphLabelOp::CONJUNCTION:
            if (children.size() >= 2)
            {
                children[0]->format(ostr, settings, state, frame);
                ostr << "&";
                children[1]->format(ostr, settings, state, frame);
            }
            break;
        case GraphLabelOp::DISJUNCTION:
            if (children.size() >= 2)
            {
                children[0]->format(ostr, settings, state, frame);
                ostr << "|";
                children[1]->format(ostr, settings, state, frame);
            }
            break;
    }
}


ASTPtr ASTPathQuantifier::clone() const
{
    return make_intrusive<ASTPathQuantifier>(*this);
}

void ASTPathQuantifier::formatImpl(WriteBuffer & ostr, const FormatSettings &, FormatState &, FormatStateStacked) const
{
    if (min_hops == 0 && max_hops == UNLIMITED)
        ostr << "*";
    else if (min_hops == 1 && max_hops == UNLIMITED)
        ostr << "+";
    else if (min_hops == 0 && max_hops == 1)
        ostr << "?";
    else if (min_hops == max_hops)
        ostr << "{" << min_hops << "}";
    else if (max_hops == UNLIMITED)
        ostr << "{" << min_hops << ",}";
    else
        ostr << "{" << min_hops << "," << max_hops << "}";
}


ASTPtr ASTNodePattern::clone() const
{
    auto res = make_intrusive<ASTNodePattern>(*this);
    res->children.clear();
    res->label_expression = nullptr;
    res->where_predicate = nullptr;

    for (const auto & child : children)
    {
        auto cloned = child->clone();
        if (label_expression && child.get() == label_expression)
            res->label_expression = cloned->as<ASTLabelExpression>();
        if (where_predicate && child.get() == where_predicate.get())
            res->where_predicate = cloned;
        res->children.push_back(cloned);
    }
    return res;
}

void ASTNodePattern::formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const
{
    ostr << "(";
    if (!variable.empty())
        ostr << variable;
    if (label_expression)
    {
        ostr << ":";
        label_expression->format(ostr, settings, state, frame);
    }
    else if (!label.empty())
    {
        ostr << ":" << label;
    }
    if (where_predicate)
    {
        ostr << " WHERE ";
        where_predicate->format(ostr, settings, state, frame);
    }
    ostr << ")";
}


ASTPtr ASTEdgePattern::clone() const
{
    auto res = make_intrusive<ASTEdgePattern>(*this);
    res->children.clear();
    res->label_expression = nullptr;
    res->quantifier = nullptr;
    res->where_predicate = nullptr;

    for (const auto & child : children)
    {
        auto cloned = child->clone();
        if (label_expression && child.get() == label_expression)
            res->label_expression = cloned->as<ASTLabelExpression>();
        if (quantifier && child.get() == quantifier)
            res->quantifier = cloned->as<ASTPathQuantifier>();
        if (where_predicate && child.get() == where_predicate.get())
            res->where_predicate = cloned;
        res->children.push_back(cloned);
    }
    return res;
}

void ASTEdgePattern::formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const
{
    bool left_arrow = (direction == GraphEdgeDirection::LEFT
                    || direction == GraphEdgeDirection::LEFT_OR_UNDIRECTED
                    || direction == GraphEdgeDirection::LEFT_OR_RIGHT);
    bool right_arrow = (direction == GraphEdgeDirection::RIGHT
                     || direction == GraphEdgeDirection::UNDIRECTED_OR_RIGHT
                     || direction == GraphEdgeDirection::LEFT_OR_RIGHT);

    if (left_arrow)
        ostr << "<-";
    else
        ostr << "-";

    ostr << "[";
    if (!variable.empty())
        ostr << variable;
    if (label_expression)
    {
        ostr << ":";
        label_expression->format(ostr, settings, state, frame);
    }
    else if (!label.empty())
    {
        ostr << ":" << label;
    }
    if (quantifier)
        quantifier->format(ostr, settings, state, frame);
    if (where_predicate)
    {
        ostr << " WHERE ";
        where_predicate->format(ostr, settings, state, frame);
    }
    ostr << "]";

    if (right_arrow)
        ostr << "->";
    else
        ostr << "-";
}


ASTPtr ASTPathPattern::clone() const
{
    auto res = make_intrusive<ASTPathPattern>(*this);
    res->children.clear();
    res->where_predicate = nullptr;

    for (const auto & child : children)
    {
        auto cloned = child->clone();
        if (where_predicate && child.get() == where_predicate.get())
            res->where_predicate = cloned;
        res->children.push_back(cloned);
    }
    return res;
}

void ASTPathPattern::formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const
{
    if (search_prefix != GraphPathSearch::NONE)
    {
        switch (search_prefix)
        {
            case GraphPathSearch::ALL: ostr << "ALL "; break;
            case GraphPathSearch::ANY:
                ostr << "ANY ";
                if (search_count > 0)
                    ostr << search_count << " ";
                break;
            case GraphPathSearch::SHORTEST:
                ostr << "SHORTEST ";
                if (search_count > 0)
                    ostr << search_count << " ";
                break;
            case GraphPathSearch::ALL_SHORTEST: ostr << "ALL SHORTEST "; break;
            case GraphPathSearch::ANY_SHORTEST: ostr << "ANY SHORTEST "; break;
            default: break;
        }
    }

    if (path_mode != GraphPathMode::DEFAULT)
    {
        switch (path_mode)
        {
            case GraphPathMode::WALK: ostr << "WALK "; break;
            case GraphPathMode::TRAIL: ostr << "TRAIL "; break;
            case GraphPathMode::SIMPLE: ostr << "SIMPLE "; break;
            case GraphPathMode::ACYCLIC: ostr << "ACYCLIC "; break;
            default: break;
        }
    }

    if (!path_variable.empty())
        ostr << path_variable << " = ";

    for (const auto & child : children)
    {
        if (child.get() != where_predicate.get())
            child->format(ostr, settings, state, frame);
    }

    if (where_predicate)
    {
        ostr << " WHERE ";
        where_predicate->format(ostr, settings, state, frame);
    }
}


ASTPtr ASTGraphPattern::clone() const
{
    auto res = make_intrusive<ASTGraphPattern>(*this);
    res->children.clear();
    for (const auto & child : children)
        res->children.push_back(child->clone());
    return res;
}

void ASTGraphPattern::formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const
{
    bool first = true;
    for (const auto & child : children)
    {
        if (!first)
            ostr << ", ";
        first = false;
        child->format(ostr, settings, state, frame);
    }
}


ASTPtr ASTGraphReturnItem::clone() const
{
    auto res = make_intrusive<ASTGraphReturnItem>(*this);
    res->children.clear();
    for (const auto & child : children)
        res->children.push_back(child->clone());
    return res;
}

void ASTGraphReturnItem::formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const
{
    if (!children.empty())
        children[0]->format(ostr, settings, state, frame);
    if (!alias.empty())
        ostr << " AS " << alias;
}


ASTPtr ASTGraphReturnClause::clone() const
{
    auto res = make_intrusive<ASTGraphReturnClause>(*this);
    res->children.clear();
    res->group_by = nullptr;
    res->order_by = nullptr;
    res->limit = nullptr;
    res->offset = nullptr;

    for (const auto & child : children)
    {
        auto cloned = child->clone();
        if (group_by && child.get() == group_by.get())
            res->group_by = cloned;
        if (order_by && child.get() == order_by.get())
            res->order_by = cloned;
        if (limit && child.get() == limit.get())
            res->limit = cloned;
        if (offset && child.get() == offset.get())
            res->offset = cloned;
        res->children.push_back(cloned);
    }
    return res;
}

void ASTGraphReturnClause::formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const
{
    ostr << "RETURN ";
    if (distinct)
        ostr << "DISTINCT ";

    bool first = true;
    for (const auto & child : children)
    {
        if (child.get() == group_by.get() || child.get() == order_by.get()
            || child.get() == limit.get() || child.get() == offset.get())
            continue;

        if (!first)
            ostr << ", ";
        first = false;
        child->format(ostr, settings, state, frame);
    }

    if (group_by)
    {
        ostr << " GROUP BY ";
        group_by->format(ostr, settings, state, frame);
    }
    if (order_by)
    {
        ostr << " ORDER BY ";
        order_by->format(ostr, settings, state, frame);
    }
    if (offset)
    {
        ostr << " OFFSET ";
        offset->format(ostr, settings, state, frame);
    }
    if (limit)
    {
        ostr << " LIMIT ";
        limit->format(ostr, settings, state, frame);
    }
}


ASTPtr ASTGraphQuery::clone() const
{
    auto res = make_intrusive<ASTGraphQuery>(*this);
    res->children.clear();
    res->match_pattern = nullptr;
    res->where_condition = nullptr;
    res->return_clause = nullptr;

    if (match_pattern)
    {
        auto cloned = match_pattern->clone();
        res->match_pattern = cloned->as<ASTGraphPattern>();
        res->children.push_back(cloned);
    }
    if (where_condition)
    {
        res->where_condition = where_condition->clone();
        res->children.push_back(res->where_condition);
    }
    if (return_clause)
    {
        auto cloned = return_clause->clone();
        res->return_clause = cloned->as<ASTGraphReturnClause>();
        res->children.push_back(cloned);
    }
    return res;
}

void ASTGraphQuery::formatImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const
{
    if (is_optional_match)
        ostr << "OPTIONAL ";
    ostr << "MATCH ";
    if (match_pattern)
        match_pattern->format(ostr, settings, state, frame);

    if (where_condition)
    {
        ostr << " WHERE ";
        where_condition->format(ostr, settings, state, frame);
    }

    if (return_clause)
    {
        ostr << " ";
        return_clause->format(ostr, settings, state, frame);
    }
}

}
