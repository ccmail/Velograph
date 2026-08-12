#include <Analyzer/GQL/GQLPropertyResolution.h>

#include <Analyzer/ColumnNode.h>
#include <Analyzer/GQL/GQLEdgePatternNode.h>
#include <Analyzer/GQL/GQLMatchNode.h>
#include <Analyzer/GQL/GQLNodePatternNode.h>
#include <Analyzer/GQL/GQLPathPatternNode.h>
#include <Analyzer/GQL/GQLPathTermNode.h>
#include <Common/Exception.h>
#include <Interpreters/GQL/GraphResolver.h>

namespace DB::ErrorCodes {
extern const int UNKNOWN_IDENTIFIER;
}

namespace DB::GQL {

namespace {

void addBinding(GQLMatchBindings& bindings, const String& name, GraphElementKind kind) {
  if (name.empty()) return;

  for (const auto& binding : bindings) {
    if (binding.name == name) return;
  }

  bindings.push_back({name, kind});
}

void collectPathBindings(const QueryTreeNodePtr& pattern, GQLMatchBindings& bindings) {
  const auto* path = pattern ? pattern->as<GQLPathPatternNode>() : nullptr;
  if (!path) return;

  const auto& expression = path->getExpression();
  const auto* term = expression ? expression->as<GQLPathTermNode>() : nullptr;
  if (!term) return;

  for (const auto& element : term->getElements().getNodes()) {
    if (const auto* node = element->as<GQLNodePatternNode>())
      addBinding(bindings, node->getElementVariable(), GraphElementKind::Vertex);
    else if (const auto* edge = element->as<GQLEdgePatternNode>())
      addBinding(bindings, edge->getElementVariable(), GraphElementKind::Edge);
  }
}

}  // namespace

GQLMatchBindings collectMatchBindings(const GQLMatchNode& match) {
  GQLMatchBindings bindings;
  for (const auto& pattern : match.getPathPatterns().getNodes()) collectPathBindings(pattern, bindings);
  return bindings;
}

const GQLMatchBinding* findMatchBinding(const GQLMatchBindings& bindings, const String& name) {
  for (const auto& binding : bindings) {
    if (binding.name == name) return &binding;
  }
  return nullptr;
}

QueryTreeNodePtr makePropertyColumn(const GQLMatchBinding& binding, const String& property_name, const QueryTreeNodePtr& source,
                                    const ContextPtr& context) {
  const auto storage = resolveActiveGraphStorage(context);
  const auto& header = storage->getGraphHeader(binding.kind);
  if (!header.has(property_name))
    throw Exception(ErrorCodes::UNKNOWN_IDENTIFIER, "Unknown property '{}.{}' in graph element header", binding.name, property_name);

  const auto& storage_column = header.getByName(property_name);
  return std::make_shared<ColumnNode>(NameAndTypePair{binding.name + "." + property_name, storage_column.type}, source);
}

}  // namespace DB::GQL
