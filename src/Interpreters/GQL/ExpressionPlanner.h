#pragma once

#include <Interpreters/ActionsDAG.h>
#include <Interpreters/Context.h>
#include <Parsers/graph/AST/GQLExpr.h>

namespace DB::OPENGQL::AST
{
class GQLTypeExpression;
}

namespace DB::GQL
{

class PlanScope;

const ActionsDAG::Node & buildExpressionNode(
    const IAST & expr,
    ActionsDAG & dag,
    ContextPtr context);

const ActionsDAG::Node & buildExpressionNode(
    const OPENGQL::AST::GQLExpr & expr,
    ActionsDAG & dag,
    ContextPtr context);

const ActionsDAG::Node & buildExpressionNode(
    const IAST & expr,
    ActionsDAG & dag,
    ContextPtr context,
    const PlanScope & scope);

const ActionsDAG::Node & buildExpressionNode(
    const OPENGQL::AST::GQLExpr & expr,
    ActionsDAG & dag,
    ContextPtr context,
    const PlanScope & scope);

const ActionsDAG::Node & buildExpressionNodeAsType(
    const IAST & expr,
    const OPENGQL::AST::GQLTypeExpression & type,
    ActionsDAG & dag,
    ContextPtr context,
    const PlanScope & scope);

}
