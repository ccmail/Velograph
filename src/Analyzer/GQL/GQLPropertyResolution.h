#pragma once

#include <Analyzer/GQL/GQLMatchNode.h>
#include <Analyzer/IQueryTreeNode.h>
#include <Interpreters/Context_fwd.h>
#include <Storages/Graph/IGraphStorage.h>

namespace DB::GQL {

struct GQLMatchBinding {
  String name;
  GraphElementKind kind;
};

using GQLMatchBindings = std::vector<GQLMatchBinding>;

GQLMatchBindings collectMatchBindings(const GQLMatchNode& match);
const GQLMatchBinding* findMatchBinding(const GQLMatchBindings& bindings, const String& name);

QueryTreeNodePtr makePropertyColumn(const GQLMatchBinding& binding, const String& property_name, const QueryTreeNodePtr& source,
                                    const ContextPtr& context);

}  // namespace DB::GQL
