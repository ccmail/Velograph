#pragma once

#include <Analyzer/IQueryTreePass.h>

namespace DB::GQL {

class GQLPredicateNormalizationPass final : public IQueryTreePass {
 public:
  String getName() override { return "GQLPredicateNormalization"; }
  String getDescription() override { return "Normalize GQL MATCH predicates into a single WHERE conjunction"; }

  void run(QueryTreeNodePtr& query_tree_node, ContextPtr context) override;
};

}  // namespace DB::GQL
