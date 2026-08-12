#include <Parsers/graph/visitor/GQLVisitorCommon.h>

namespace DB {

namespace OPENGQL {

Ptr makeOrderByAndPageClause(ResultTail &&tail) {
  auto clause = make_intrusive<GQLPageClause>();
  clause->order_by = std::move(tail.order_by);
  clause->offset = std::move(tail.offset);
  clause->limit = std::move(tail.limit);

  appendClause(clause->children, clause->order_by);
  appendClause(clause->children, clause->offset);
  appendClause(clause->children, clause->limit);

  return Ptr(clause);
}

std::any GQLParseTreeVisitor::visitPrimitiveResultStatement(GQLParser::PrimitiveResultStatementContext *context) {
  if (context->FINISH()) {
    return Ptr(make_intrusive<GQLFinishClause>());
  }

  auto clause = castAny<Ptr>(visit(context->returnStatement()));

  if (!context->orderByAndPageStatement()) return clause;

  auto tail = castAny<ResultTail>(visit(context->orderByAndPageStatement()));
  auto page_clause = make_intrusive<GQLPageClause>();
  page_clause->order_by = std::move(tail.order_by);
  page_clause->offset = std::move(tail.offset);
  page_clause->limit = std::move(tail.limit);
  appendClause(page_clause->children, page_clause->order_by);
  appendClause(page_clause->children, page_clause->offset);
  appendClause(page_clause->children, page_clause->limit);

  PtrList clauses;
  appendClause(clauses, std::move(clause));
  appendClause(clauses, Ptr(page_clause));
  return makeSingleQuery(std::move(clauses));
}

std::any GQLParseTreeVisitor::visitReturnStatement(GQLParser::ReturnStatementContext *context) {
  return castAny<Ptr>(visit(context->returnStatementBody()));
}

std::any GQLParseTreeVisitor::visitReturnStatementBody(GQLParser::ReturnStatementBodyContext *context) {
  auto clause = make_intrusive<GQLReturnClause>();

  if (context->setQuantifier() && context->setQuantifier()->DISTINCT()) {
    clause->distinct = true;
  }

  if (context->ASTERISK()) {
    clause->return_all = true;
  } else if (context->returnItemList()) {
    clause->items = castAny<PtrList>(visit(context->returnItemList()));

    for (const auto &item : clause->items) {
      if (item) {
        clause->children.push_back(item);
      }
    }
  }

  if (context->groupByClause()) {
    clause->group_by = castAny<Ptr>(visit(context->groupByClause()));

    if (clause->group_by) clause->children.push_back(clause->group_by);
  }

  return Ptr(clause);
}

std::any GQLParseTreeVisitor::visitReturnItemList(GQLParser::ReturnItemListContext *context) {
  PtrList items;

  for (auto *item : context->returnItem()) {
    items.push_back(castAny<Ptr>(visit(item)));
  }

  return items;
}

std::any GQLParseTreeVisitor::visitReturnItem(GQLParser::ReturnItemContext *context) {
  auto expression = castAny<Ptr>(visit(context->aggregatingValueExpression()));
  String alias;

  if (context->returnItemAlias()) alias = getText(context->returnItemAlias()->identifier());

  return makeAliasedItem(expression, std::move(alias));
}

std::any GQLParseTreeVisitor::visitGroupByClause(GQLParser::GroupByClauseContext *context) {
  auto clause = make_intrusive<GQLGroupByClause>();

  if (auto *grouping_list = context->groupingElementList()) {
    clause->empty_grouping_set = grouping_list->emptyGroupingSet() != nullptr;

    for (auto *element : grouping_list->groupingElement()) {
      auto *binding_reference = element->bindingVariableReference();
      auto *binding_variable = binding_reference ? binding_reference->bindingVariable() : nullptr;
      auto item = GQLExpr::identifier(getText(binding_variable));
      clause->items.push_back(item);
      appendClause(clause->children, item);
    }
  }

  return Ptr(clause);
}

std::any GQLParseTreeVisitor::visitOrderByAndPageStatement(GQLParser::OrderByAndPageStatementContext *context) {
  ResultTail result;

  if (context->orderByClause()) {
    result.order_by = castAny<Ptr>(visit(context->orderByClause()));
  }

  if (context->offsetClause()) {
    result.offset = castAny<Ptr>(visit(context->offsetClause()));
  }

  if (context->limitClause()) {
    result.limit = castAny<Ptr>(visit(context->limitClause()));
  }

  return result;
}

std::any GQLParseTreeVisitor::visitOrderByClause(GQLParser::OrderByClauseContext *context) {
  return Ptr(make_intrusive<GQLOrderByClause>(castAny<PtrList>(visit(context->sortSpecificationList()))));
}

std::any GQLParseTreeVisitor::visitSortSpecificationList(GQLParser::SortSpecificationListContext *context) {
  PtrList items;

  for (auto *item : context->sortSpecification()) {
    items.push_back(castAny<Ptr>(visit(item)));
  }

  return items;
}

std::any GQLParseTreeVisitor::visitSortSpecification(GQLParser::SortSpecificationContext *context) {
  const bool descending =
      context->orderingSpecification() && (context->orderingSpecification()->DESC() || context->orderingSpecification()->DESCENDING());

  GQLOrderByItem::NullOrdering null_ordering = GQLOrderByItem::NullOrdering::Unspecified;
  if (auto *null_ordering_context = context->nullOrdering()) {
    if (null_ordering_context->FIRST())
      null_ordering = GQLOrderByItem::NullOrdering::NullsFirst;
    else if (null_ordering_context->LAST())
      null_ordering = GQLOrderByItem::NullOrdering::NullsLast;
  }

  return Ptr(
      make_intrusive<GQLOrderByItem>(castAny<Ptr>(visit(context->sortKey()->aggregatingValueExpression())), descending, null_ordering));
}

std::any GQLParseTreeVisitor::visitLimitClause(GQLParser::LimitClauseContext *context) {
  return GQLExpr::constant(getText(context->nonNegativeIntegerSpecification()));
}

std::any GQLParseTreeVisitor::visitOffsetClause(GQLParser::OffsetClauseContext *context) {
  return GQLExpr::constant(getText(context->nonNegativeIntegerSpecification()));
}

}  // namespace OPENGQL

}  // namespace DB
