#include <Analyzer/GQL/Passes/GQLPredicateNormalizationPass.h>

#include <Analyzer/ConstantNode.h>
#include <Analyzer/FunctionNode.h>
#include <Analyzer/GQL/GQLCombinedQueryNode.h>
#include <Analyzer/GQL/GQLEdgePatternNode.h>
#include <Analyzer/GQL/GQLLabelExpressionNode.h>
#include <Analyzer/GQL/GQLLinearQueryNode.h>
#include <Analyzer/GQL/GQLMatchNode.h>
#include <Analyzer/GQL/GQLNodePatternNode.h>
#include <Analyzer/GQL/GQLPathPatternNode.h>
#include <Analyzer/GQL/GQLPathTermNode.h>
#include <Analyzer/GQL/GQLPropertyItemNode.h>
#include <Analyzer/GQL/GQLPropertyMapNode.h>
#include <Analyzer/GQL/GQLPropertyResolution.h>
#include <Common/Exception.h>
#include <Core/Field.h>
#include <DataTypes/DataTypeString.h>
#include <Functions/FunctionFactory.h>

namespace DB::ErrorCodes {
extern const int LOGICAL_ERROR;
extern const int NOT_IMPLEMENTED;
}  // namespace DB::ErrorCodes

namespace DB::GQL {

namespace {

QueryTreeNodePtr makeFunction(const String &name, QueryTreeNodes arguments, const ContextPtr &context) {
  auto function = std::make_shared<FunctionNode>(name);
  function->getArguments().getNodes() = std::move(arguments);
  function->resolveAsFunction(FunctionFactory::instance().get(name, context));
  return function;
}

QueryTreeNodePtr makeStringConstant(const String &value) {
  return std::make_shared<ConstantNode>(Field(value), std::make_shared<DataTypeString>());
}

QueryTreeNodePtr combineConjuncts(QueryTreeNodes conjuncts, const ContextPtr &context) {
  if (conjuncts.empty()) return {};

  auto result = std::move(conjuncts.front());
  for (size_t i = 1; i < conjuncts.size(); ++i) result = makeFunction("and", {std::move(result), std::move(conjuncts[i])}, context);
  return result;
}

QueryTreeNodePtr normalizeLabelExpression(const GQLLabelExpressionNode &label, const GQLMatchBinding &binding,
                                          const QueryTreeNodePtr &source, const ContextPtr &context) {
  using Operator = GQLLabelExpressionNode::Operator;
  if (label.getOperator() == Operator::Label) {
    if (label.getLabelName().empty()) throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL label predicate has an empty label name");

    const char *const property_name = binding.kind == GraphElementKind::Vertex ? "labels" : "type";
    auto property = makePropertyColumn(binding, property_name, source, context);
    auto label_name = makeStringConstant(label.getLabelName());
    return makeFunction(binding.kind == GraphElementKind::Vertex ? "has" : "equals", {std::move(property), std::move(label_name)}, context);
  }

  const auto &operands = label.getOperands().getNodes();
  if (label.getOperator() == Operator::Not) {
    if (operands.size() != 1 || !operands.front()) throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL label negation must have one operand");
    return makeFunction("not", {normalizeLabelExpression(operands.front()->as<const GQLLabelExpressionNode &>(), binding, source, context)},
                        context);
  }

  if (label.getOperator() != Operator::And && label.getOperator() != Operator::Or)
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Unsupported GQL label expression operator");
  if (operands.size() != 2 || !operands[0] || !operands[1])
    throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL binary label expression must have two operands");

  return makeFunction(label.getOperator() == Operator::And ? "and" : "or",
                      {
                          normalizeLabelExpression(operands[0]->as<const GQLLabelExpressionNode &>(), binding, source, context),
                          normalizeLabelExpression(operands[1]->as<const GQLLabelExpressionNode &>(), binding, source, context),
                      },
                      context);
}

void appendElementPredicates(const String &variable, QueryTreeNodePtr &label_expression, QueryTreeNodePtr &property_map,
                             QueryTreeNodePtr &inline_where, const GQLMatchBindings &bindings, const QueryTreeNodePtr &source,
                             const ContextPtr &context, QueryTreeNodes &conjuncts) {
  const auto *binding = findMatchBinding(bindings, variable);
  if (!binding && (label_expression || property_map || inline_where))
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL predicates on anonymous pattern elements are not supported");

  if (inline_where) conjuncts.push_back(std::move(inline_where));

  if (property_map) {
    const auto &items = property_map->as<const GQLPropertyMapNode &>().getItems().getNodes();
    for (const auto &item_node : items) {
      const auto &item = item_node->as<const GQLPropertyItemNode &>();
      if (!item.getValue()) throw Exception(ErrorCodes::LOGICAL_ERROR, "GQL property map item has no value");

      conjuncts.push_back(
          makeFunction("equals", {makePropertyColumn(*binding, item.getPropertyName(), source, context), item.getValue()}, context));
    }
    property_map.reset();
  }

  if (label_expression) {
    conjuncts.push_back(normalizeLabelExpression(label_expression->as<const GQLLabelExpressionNode &>(), *binding, source, context));
    label_expression.reset();
  }
}

void normalizeMatch(GQLMatchNode &match, const QueryTreeNodePtr &source, const ContextPtr &context) {
  auto bindings = collectMatchBindings(match);
  QueryTreeNodes conjuncts;
  if (match.getWhere()) conjuncts.push_back(std::move(match.getWhere()));

  for (auto &pattern : match.getPathPatterns().getNodes()) {
    auto *path = pattern ? pattern->as<GQLPathPatternNode>() : nullptr;
    auto *term = path && path->getExpression() ? path->getExpression()->as<GQLPathTermNode>() : nullptr;
    if (!term) continue;

    for (auto &element : term->getElements().getNodes()) {
      if (auto *node = element->as<GQLNodePatternNode>()) {
        appendElementPredicates(node->getElementVariable(), node->getLabelExpression(), node->getPropertyMap(), node->getWhere(), bindings,
                                source, context, conjuncts);
      } else if (auto *edge = element->as<GQLEdgePatternNode>()) {
        appendElementPredicates(edge->getElementVariable(), edge->getLabelExpression(), edge->getPropertyMap(), edge->getWhere(), bindings,
                                source, context, conjuncts);
      }
    }
  }

  match.getWhere() = combineConjuncts(std::move(conjuncts), context);
}

void normalizeQuery(QueryTreeNodePtr &query, const ContextPtr &context) {
  if (auto *linear = query ? query->as<GQLLinearQueryNode>() : nullptr) {
    for (auto &step : linear->getSteps().getNodes()) {
      if (auto *match = step->as<GQLMatchNode>()) normalizeMatch(*match, step, context);
    }
    return;
  }

  if (auto *combined = query ? query->as<GQLCombinedQueryNode>() : nullptr) {
    for (auto &child : combined->getQueries().getNodes()) normalizeQuery(child, context);
  }
}

}  // namespace

void GQLPredicateNormalizationPass::run(QueryTreeNodePtr &query_tree_node, ContextPtr context) { normalizeQuery(query_tree_node, context); }

}  // namespace DB::GQL
