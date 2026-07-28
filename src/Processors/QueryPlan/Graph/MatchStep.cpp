#include <Processors/QueryPlan/Graph/MatchStep.h>

#include <Columns/ColumnsNumber.h>
#include <Common/Exception.h>
#include <Core/Names.h>
#include <DataTypes/DataTypesNumber.h>
#include <Parsers/IAST.h>
#include <QueryPipeline/QueryPipelineBuilder.h>
#include <Storages/Graph/IGraphStorage.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace DB {
namespace ErrorCodes {
extern const int LOGICAL_ERROR;
}
}  // namespace DB

namespace DB::Graph {
namespace {

ASTPtr cloneOrNull(const ASTPtr &ast) { return ast ? ast->clone() : nullptr; }

ASTs cloneASTs(const ASTs &asts) {
  ASTs result;
  result.reserve(asts.size());
  for (const auto &ast : asts) {
    if (ast) result.push_back(ast->clone());
  }

  return result;
}

MatchNodeSpec cloneNodeSpec(const MatchNodeSpec &node) {
  return MatchNodeSpec{
      .variable = node.variable,
      .label_expression = cloneOrNull(node.label_expression),
      .properties = cloneOrNull(node.properties),
      .predicate = cloneOrNull(node.predicate),
  };
}

MatchEdgeSpec cloneEdgeSpec(const MatchEdgeSpec &edge) {
  return MatchEdgeSpec{
      .variable = edge.variable,
      .direction = edge.direction,
      .label_expression = cloneOrNull(edge.label_expression),
      .properties = cloneOrNull(edge.properties),
      .predicate = cloneOrNull(edge.predicate),
      .quantifier = cloneOrNull(edge.quantifier),
  };
}

MatchPathSpec clonePathSpec(const MatchPathSpec &path) {
  MatchPathSpec result_path;
  result_path.variable = path.variable;
  result_path.prefix = cloneOrNull(path.prefix);
  result_path.alternation_kind = path.alternation_kind;

  result_path.alternatives.reserve(path.alternatives.size());
  for (const auto &alternative : path.alternatives) result_path.alternatives.push_back(clonePathSpec(alternative));

  result_path.nodes.reserve(path.nodes.size());
  for (const auto &node : path.nodes) result_path.nodes.push_back(cloneNodeSpec(node));

  result_path.edges.reserve(path.edges.size());
  for (const auto &edge : path.edges) result_path.edges.push_back(cloneEdgeSpec(edge));

  return result_path;
}

MatchClauseSpec cloneMatchClauseSpec(const MatchClauseSpec &match_spec) {
  MatchClauseSpec result;
  result.optional = match_spec.optional;
  result.has_match_mode = match_spec.has_match_mode;
  result.match_mode = match_spec.match_mode;
  result.has_keep_clause = match_spec.has_keep_clause;
  result.has_optional_operand_block = match_spec.has_optional_operand_block;
  result.has_yield_items = match_spec.has_yield_items;
  result.keep_clause = cloneOrNull(match_spec.keep_clause);
  result.optional_operand_block = cloneOrNull(match_spec.optional_operand_block);
  result.where_clause = cloneOrNull(match_spec.where_clause);
  result.yield_items = cloneASTs(match_spec.yield_items);
  result.yield_variables = match_spec.yield_variables;

  result.paths.reserve(match_spec.paths.size());
  for (const auto &path : match_spec.paths) result.paths.push_back(clonePathSpec(path));

  return result;
}

MatchSpec cloneMatchSpec(const MatchSpec &match_spec) {
  MatchSpec result;
  static_cast<MatchClauseSpec &>(result) = cloneMatchClauseSpec(match_spec);
  result.graph_reference = cloneOrNull(match_spec.graph_reference);
  result.clauses.reserve(match_spec.clauses.size());
  for (const auto &clause : match_spec.clauses) result.clauses.push_back(cloneMatchClauseSpec(clause));

  return result;
}

std::optional<GraphElementKind> findVariableKind(const MatchPathSpec &path, const String &variable) {
  for (const auto &alternative : path.alternatives) {
    if (auto kind = findVariableKind(alternative, variable)) return kind;
  }

  for (const auto &node : path.nodes) {
    if (node.variable == variable) return GraphElementKind::Vertex;
  }
  for (const auto &edge : path.edges) {
    if (edge.variable == variable) return GraphElementKind::Edge;
  }
  return {};
}

std::optional<GraphElementKind> findVariableKind(const MatchSpec &spec, const String &variable) {
  for (const auto &clause : spec.clauses) {
    for (const auto &path : clause.paths) {
      if (auto kind = findVariableKind(path, variable)) return kind;
    }
  }
  for (const auto &path : spec.paths) {
    if (auto kind = findVariableKind(path, variable)) return kind;
  }
  return {};
}

}  // namespace

MatchStep::MatchStep(MatchSpec match_spec_, GraphStoragePtr graph_storage_, Names referenced_columns_, ContextPtr context_)
    : ISourceStep(makeHeader(match_spec_, graph_storage_, referenced_columns_)),
      match_spec(std::move(match_spec_)),
      graph_storage(std::move(graph_storage_)),
      referenced_columns(std::move(referenced_columns_)),
      context(std::move(context_)) {
  setStepDescription("GQL MATCH");
}

SharedHeader MatchStep::makeHeader(const MatchSpec &match_spec, const GraphStoragePtr &graph_storage, const Names &referenced_columns) {
  if (!graph_storage) throw Exception(ErrorCodes::LOGICAL_ERROR, "Cannot build MatchStep header without graph storage");

  Block header;
  for (const auto &column_name : referenced_columns) {
    const auto separator = column_name.find('.');
    if (separator == String::npos) {
      header.insert({ColumnUInt64::create(), std::make_shared<DataTypeUInt64>(), column_name});
      continue;
    }

    const auto variable = column_name.substr(0, separator);
    const auto property_name = column_name.substr(separator + 1);
    const auto kind = findVariableKind(match_spec, variable);
    if (!kind) throw Exception(ErrorCodes::LOGICAL_ERROR, "Referenced GQL column '{}' has no matching pattern variable", column_name);

    const auto &storage_header = graph_storage->getGraphHeader(*kind);
    if (!storage_header.has(property_name))
      throw Exception(ErrorCodes::LOGICAL_ERROR, "Referenced GQL column '{}' is absent from the graph storage header", column_name);

    header.insert({storage_header.getByName(property_name).type, column_name});
  }

  return std::make_shared<const Block>(std::move(header));
}

QueryPlanStepPtr MatchStep::clone() const {
  return std::make_unique<MatchStep>(cloneMatchSpec(match_spec), graph_storage, referenced_columns, context);
}

void MatchStep::initializePipeline(QueryPipelineBuilder & /*pipeline*/, const BuildQueryPipelineSettings &) {
  throw Exception(ErrorCodes::LOGICAL_ERROR,
                  "MatchStep reached initializePipeline without being expanded by expandMatchSteps; "
                  "this means the pattern shape is not supported or the expansion pass did not run");
}

}  // namespace DB::Graph
