#pragma once

#include <Interpreters/GQL/PatternBinder.h>
#include <Processors/QueryPlan/Graph/MatchSpec.h>

#include <vector>

namespace DB::GQL
{

Graph::MatchSpec buildMatchSpec(const MatchPlan & match);
Graph::MatchSpec buildMatchSpec(const std::vector<MatchPlan> & matches);

}
