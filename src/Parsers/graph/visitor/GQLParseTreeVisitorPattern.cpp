#include <Parsers/graph/visitor/GQLVisitorCommon.h>

namespace DB {

namespace OPENGQL {

Ptr makeQuantifier(GQLParser::GraphPatternQuantifierContext *context) {
  if (!context) return {};

  if (context->ASTERISK()) return Ptr(make_intrusive<GQLQuantifier>(GQLQuantifier::Kind::Star));

  if (context->PLUS_SIGN()) return Ptr(make_intrusive<GQLQuantifier>(GQLQuantifier::Kind::Plus));

  if (auto *fixed = context->fixedQuantifier())
    return Ptr(make_intrusive<GQLQuantifier>(GQLQuantifier::Kind::Exact, getText(fixed->unsignedInteger())));

  if (auto *general = context->generalQuantifier()) {
    return Ptr(make_intrusive<GQLQuantifier>(GQLQuantifier::Kind::Range,
                                             general->lowerBound() ? getText(general->lowerBound()->unsignedInteger()) : String{},
                                             general->upperBound() ? getText(general->upperBound()->unsignedInteger()) : String{}));
  }

  return {};
}

Ptr makeQuestionQuantifier() { return Ptr(make_intrusive<GQLQuantifier>(GQLQuantifier::Kind::Question)); }

Ptr makeCountSpec(GQLParser::NonNegativeIntegerSpecificationContext *context) {
  if (!context) {
    return {};
  }

  if (auto *unsigned_integer = context->unsignedInteger()) {
    return Ptr(make_intrusive<GQLCountSpec>(CountSpecKind::Integer, getText(unsigned_integer)));
  }

  if (auto *dynamic_parameter = context->dynamicParameterSpecification()) {
    return Ptr(make_intrusive<GQLCountSpec>(CountSpecKind::DynamicParameter, getText(dynamic_parameter)));
  }

  return {};
}

void appendFlattenedSimplifiedOperands(PtrList &operands, GQLSimplifiedPathExpr::Kind kind, const Ptr &operand) {
  if (!operand) {
    return;
  }

  if (const auto *expression = operand->as<GQLSimplifiedPathExpr>();
      expression && expression->kind == kind && !expression->operands.empty()) {
    for (const auto &child : expression->operands) {
      operands.push_back(child);
    }

    return;
  }

  operands.push_back(operand);
}

Ptr makeSimplifiedNaryExpr(GQLSimplifiedPathExpr::Kind kind, std::initializer_list<Ptr> input_operands) {
  PtrList operands;

  for (const auto &operand : input_operands) {
    appendFlattenedSimplifiedOperands(operands, kind, operand);
  }

  auto expression = make_intrusive<GQLSimplifiedPathExpr>(kind);
  expression->operands = operands;

  for (const auto &operand : expression->operands) {
    expression->children.push_back(operand);
  }

  return Ptr(expression);
}

Ptr makePathPatternAlternation(GQLPathPatternAlternation::Kind kind, const std::vector<GQLParser::PathTermContext *> &path_terms,
                               GQLParseTreeVisitor &visitor) {
  PtrList operands;
  operands.reserve(path_terms.size());

  for (auto *path_term : path_terms) {
    auto operand = castAny<Ptr>(visitor.visit(path_term));
    chassert(operand);
    chassert(operand->as<GQLPathTerm>());
    operands.push_back(operand);
  }

  return Ptr(make_intrusive<GQLPathPatternAlternation>(kind, std::move(operands)));
}

EdgeDirection getSimplifiedDefaultingDirection(GQLParser::SimplifiedPathPatternExpressionContext *context) {
  if (context->simplifiedDefaultingLeft()) {
    return EdgeDirection::Left;
  }

  if (context->simplifiedDefaultingUndirected()) {
    return EdgeDirection::Undirected;
  }

  if (context->simplifiedDefaultingRight()) {
    return EdgeDirection::Right;
  }

  if (context->simplifiedDefaultingLeftOrUndirected()) {
    return EdgeDirection::LeftOrUndirected;
  }

  if (context->simplifiedDefaultingUndirectedOrRight()) {
    return EdgeDirection::UndirectedOrRight;
  }

  if (context->simplifiedDefaultingLeftOrRight()) {
    return EdgeDirection::LeftOrRight;
  }

  return EdgeDirection::Any;
}

GQLParser::SimplifiedContentsContext *getSimplifiedDefaultingContents(GQLParser::SimplifiedPathPatternExpressionContext *context) {
  if (auto *defaulting = context->simplifiedDefaultingLeft()) {
    return defaulting->simplifiedContents();
  }

  if (auto *defaulting = context->simplifiedDefaultingUndirected()) {
    return defaulting->simplifiedContents();
  }

  if (auto *defaulting = context->simplifiedDefaultingRight()) {
    return defaulting->simplifiedContents();
  }

  if (auto *defaulting = context->simplifiedDefaultingLeftOrUndirected()) {
    return defaulting->simplifiedContents();
  }

  if (auto *defaulting = context->simplifiedDefaultingUndirectedOrRight()) {
    return defaulting->simplifiedContents();
  }

  if (auto *defaulting = context->simplifiedDefaultingLeftOrRight()) {
    return defaulting->simplifiedContents();
  }

  if (auto *defaulting = context->simplifiedDefaultingAnyDirection()) {
    return defaulting->simplifiedContents();
  }

  return nullptr;
}

EdgeDirection getSimplifiedOverrideDirection(GQLParser::SimplifiedDirectionOverrideContext *context) {
  if (context->simplifiedOverrideLeft()) {
    return EdgeDirection::Left;
  }

  if (context->simplifiedOverrideUndirected()) {
    return EdgeDirection::Undirected;
  }

  if (context->simplifiedOverrideRight()) {
    return EdgeDirection::Right;
  }

  if (context->simplifiedOverrideLeftOrUndirected()) {
    return EdgeDirection::LeftOrUndirected;
  }

  if (context->simplifiedOverrideUndirectedOrRight()) {
    return EdgeDirection::UndirectedOrRight;
  }

  if (context->simplifiedOverrideLeftOrRight()) {
    return EdgeDirection::LeftOrRight;
  }

  if (context->simplifiedOverrideAnyDirection()) {
    return EdgeDirection::Any;
  }

  throwUnsupported("simplified direction override", context);
}

GQLParser::SimplifiedSecondaryContext *getSimplifiedOverrideSecondary(GQLParser::SimplifiedDirectionOverrideContext *context) {
  if (auto *override_context = context->simplifiedOverrideLeft()) {
    return override_context->simplifiedSecondary();
  }

  if (auto *override_context = context->simplifiedOverrideUndirected()) {
    return override_context->simplifiedSecondary();
  }

  if (auto *override_context = context->simplifiedOverrideRight()) {
    return override_context->simplifiedSecondary();
  }

  if (auto *override_context = context->simplifiedOverrideLeftOrUndirected()) {
    return override_context->simplifiedSecondary();
  }

  if (auto *override_context = context->simplifiedOverrideUndirectedOrRight()) {
    return override_context->simplifiedSecondary();
  }

  if (auto *override_context = context->simplifiedOverrideLeftOrRight()) {
    return override_context->simplifiedSecondary();
  }

  if (auto *override_context = context->simplifiedOverrideAnyDirection()) {
    return override_context->simplifiedSecondary();
  }

  return nullptr;
}

Ptr makeSimplifiedContents(GQLParser::SimplifiedContentsContext *context);

Ptr makeSimplifiedPrimary(GQLParser::SimplifiedPrimaryContext *context) {
  if (!context) {
    return {};
  }

  if (context->labelName()) {
    return GQLSimplifiedPathExpr::label(getText(context->labelName()));
  }

  return GQLSimplifiedPathExpr::group(makeSimplifiedContents(context->simplifiedContents()));
}

Ptr makeSimplifiedSecondary(GQLParser::SimplifiedSecondaryContext *context) {
  if (!context) {
    return {};
  }

  if (auto *negation = context->simplifiedNegation()) {
    return GQLSimplifiedPathExpr::negation(makeSimplifiedPrimary(negation->simplifiedPrimary()));
  }

  return makeSimplifiedPrimary(context->simplifiedPrimary());
}

Ptr makeSimplifiedDirectionOverride(GQLParser::SimplifiedDirectionOverrideContext *context) {
  if (!context) {
    return {};
  }

  return GQLSimplifiedPathExpr::directionOverride(getSimplifiedOverrideDirection(context),
                                                  makeSimplifiedSecondary(getSimplifiedOverrideSecondary(context)));
}

Ptr makeSimplifiedTertiary(GQLParser::SimplifiedTertiaryContext *context) {
  if (!context) {
    return {};
  }

  if (context->simplifiedDirectionOverride()) {
    return makeSimplifiedDirectionOverride(context->simplifiedDirectionOverride());
  }

  return makeSimplifiedSecondary(context->simplifiedSecondary());
}

Ptr makeSimplifiedFactorHigh(GQLParser::SimplifiedFactorHighContext *context) {
  if (!context) {
    return {};
  }

  if (context->simplifiedTertiary()) {
    return makeSimplifiedTertiary(context->simplifiedTertiary());
  }

  if (auto *quantified = context->simplifiedQuantified()) {
    return GQLSimplifiedPathExpr::repetition(makeSimplifiedTertiary(quantified->simplifiedTertiary()),
                                             makeQuantifier(quantified->graphPatternQuantifier()));
  }

  if (auto *questioned = context->simplifiedQuestioned()) {
    return GQLSimplifiedPathExpr::repetition(makeSimplifiedTertiary(questioned->simplifiedTertiary()), makeQuestionQuantifier());
  }

  return {};
}

Ptr makeSimplifiedFactorLow(GQLParser::SimplifiedFactorLowContext *context) {
  if (!context) {
    return {};
  }

  if (auto *conjunction = dynamic_cast<GQLParser::SimplifiedConjunctionLabelContext *>(context)) {
    return makeSimplifiedNaryExpr(
        GQLSimplifiedPathExpr::Kind::Conjunction,
        {makeSimplifiedFactorLow(conjunction->simplifiedFactorLow()), makeSimplifiedFactorHigh(conjunction->simplifiedFactorHigh())});
  }

  if (auto *factor_high = dynamic_cast<GQLParser::SimplifiedFactorHighLabelContext *>(context)) {
    return makeSimplifiedFactorHigh(factor_high->simplifiedFactorHigh());
  }

  return {};
}

Ptr makeSimplifiedTerm(GQLParser::SimplifiedTermContext *context) {
  if (!context) {
    return {};
  }

  if (auto *concatenation = dynamic_cast<GQLParser::SimplifiedConcatenationLabelContext *>(context)) {
    return makeSimplifiedNaryExpr(
        GQLSimplifiedPathExpr::Kind::Concatenation,
        {makeSimplifiedTerm(concatenation->simplifiedTerm()), makeSimplifiedFactorLow(concatenation->simplifiedFactorLow())});
  }

  if (auto *factor_low = dynamic_cast<GQLParser::SimplifiedFactorLowLabelContext *>(context)) {
    return makeSimplifiedFactorLow(factor_low->simplifiedFactorLow());
  }

  return {};
}

Ptr makeSimplifiedContents(GQLParser::SimplifiedContentsContext *context) {
  if (!context) {
    return {};
  }

  if (auto *path_union = context->simplifiedPathUnion()) {
    PtrList operands;

    for (auto *term : path_union->simplifiedTerm()) {
      appendFlattenedSimplifiedOperands(operands, GQLSimplifiedPathExpr::Kind::Union, makeSimplifiedTerm(term));
    }

    auto expression = make_intrusive<GQLSimplifiedPathExpr>(GQLSimplifiedPathExpr::Kind::Union);
    expression->operands = operands;
    for (const auto &operand : expression->operands) {
      expression->children.push_back(operand);
    }
    return Ptr(expression);
  }

  if (auto *alternation = context->simplifiedMultisetAlternation()) {
    PtrList operands;

    for (auto *term : alternation->simplifiedTerm()) {
      appendFlattenedSimplifiedOperands(operands, GQLSimplifiedPathExpr::Kind::MultisetAlternation, makeSimplifiedTerm(term));
    }

    auto expression = make_intrusive<GQLSimplifiedPathExpr>(GQLSimplifiedPathExpr::Kind::MultisetAlternation);
    expression->operands = operands;
    for (const auto &operand : expression->operands) {
      expression->children.push_back(operand);
    }
    return Ptr(expression);
  }

  return makeSimplifiedTerm(context->simplifiedTerm());
}

Ptr makeSimplifiedPathPattern(GQLParser::SimplifiedPathPatternExpressionContext *context) {
  if (!context) {
    return {};
  }

  auto pattern = make_intrusive<GQLSimplifiedPathPattern>(getSimplifiedDefaultingDirection(context));
  pattern->expression = makeSimplifiedContents(getSimplifiedDefaultingContents(context));
  appendClause(pattern->children, pattern->expression);
  return Ptr(pattern);
}

bool attachPathPrimaryQuantifier(const Ptr &node, Ptr quantifier) {
  if (!node || !quantifier) {
    return false;
  }

  if (auto *edge = node->as<GQLEdgePattern>()) {
    edge->quantifier = std::move(quantifier);
    appendClause(edge->children, edge->quantifier);
    return true;
  }

  if (auto *path = node->as<GQLParenthesizedPathPattern>()) {
    path->quantifier = std::move(quantifier);
    appendClause(path->children, path->quantifier);
    return true;
  }

  if (auto *simplified = node->as<GQLSimplifiedPathPattern>()) {
    simplified->quantifier = std::move(quantifier);
    appendClause(simplified->children, simplified->quantifier);
    return true;
  }

  return false;
}

PathMode getPathMode(GQLParser::PathModeContext *context) {
  if (!context) {
    return PathMode::None;
  }

  if (context->WALK()) {
    return PathMode::Walk;
  }

  if (context->TRAIL()) {
    return PathMode::Trail;
  }

  if (context->SIMPLE()) {
    return PathMode::Simple;
  }

  if (context->ACYCLIC()) {
    return PathMode::Acyclic;
  }

  return PathMode::None;
}

template <typename T>
void fillPathSearchPrefixFields(GQLPathSearchPrefix &prefix, T *context) {
  prefix.path_mode = getPathMode(context ? context->pathMode() : nullptr);
  prefix.has_path_keyword = context && context->pathOrPaths() != nullptr;
  prefix.use_paths_keyword = context && context->pathOrPaths() && context->pathOrPaths()->PATHS();
}

Ptr makePathPatternPrefix(GQLParser::PathModePrefixContext *context) {
  if (!context) {
    return {};
  }

  auto prefix = make_intrusive<GQLPathModePrefix>();
  prefix->path_mode = getPathMode(context->pathMode());
  prefix->has_path_keyword = context->pathOrPaths() != nullptr;
  prefix->use_paths_keyword = context->pathOrPaths() && context->pathOrPaths()->PATHS();
  return Ptr(prefix);
}

Ptr makePathPatternPrefix(GQLParser::PathPatternPrefixContext *context) {
  if (!context) {
    return {};
  }

  if (context->pathModePrefix()) {
    return makePathPatternPrefix(context->pathModePrefix());
  }

  auto *search = context->pathSearchPrefix();
  if (!search) {
    return {};
  }

  if (auto *all = search->allPathSearch()) {
    auto prefix = make_intrusive<GQLPathSearchPrefix>();
    prefix->search_kind = PathSearchKind::All;
    fillPathSearchPrefixFields(*prefix, all);
    return Ptr(prefix);
  }

  if (auto *any = search->anyPathSearch()) {
    auto prefix = make_intrusive<GQLPathSearchPrefix>();
    prefix->search_kind = PathSearchKind::Any;
    prefix->count = any->numberOfPaths() ? makeCountSpec(any->numberOfPaths()->nonNegativeIntegerSpecification()) : Ptr{};
    prefix->count_kind = any->numberOfPaths() ? CountKind::Paths : CountKind::None;
    fillPathSearchPrefixFields(*prefix, any);
    appendClause(prefix->children, prefix->count);
    return Ptr(prefix);
  }

  auto *shortest = search->shortestPathSearch();
  if (!shortest) {
    return {};
  }

  if (auto *all_shortest = shortest->allShortestPathSearch()) {
    auto prefix = make_intrusive<GQLPathSearchPrefix>();
    prefix->search_kind = PathSearchKind::AllShortest;
    fillPathSearchPrefixFields(*prefix, all_shortest);
    return Ptr(prefix);
  }

  if (auto *any_shortest = shortest->anyShortestPathSearch()) {
    auto prefix = make_intrusive<GQLPathSearchPrefix>();
    prefix->search_kind = PathSearchKind::AnyShortest;
    fillPathSearchPrefixFields(*prefix, any_shortest);
    return Ptr(prefix);
  }

  if (auto *counted_shortest = shortest->countedShortestPathSearch()) {
    auto prefix = make_intrusive<GQLPathSearchPrefix>();
    prefix->search_kind = PathSearchKind::CountedShortest;
    prefix->count_kind = CountKind::Paths;
    prefix->count = makeCountSpec(counted_shortest->numberOfPaths()->nonNegativeIntegerSpecification());
    fillPathSearchPrefixFields(*prefix, counted_shortest);
    appendClause(prefix->children, prefix->count);
    return Ptr(prefix);
  }

  if (auto *counted_group = shortest->countedShortestGroupSearch()) {
    auto prefix = make_intrusive<GQLPathSearchPrefix>();
    prefix->search_kind = PathSearchKind::CountedShortestGroup;
    prefix->count_kind = CountKind::Groups;
    prefix->count =
        counted_group->numberOfGroups() ? makeCountSpec(counted_group->numberOfGroups()->nonNegativeIntegerSpecification()) : Ptr{};
    fillPathSearchPrefixFields(*prefix, counted_group);
    prefix->use_groups_keyword = counted_group->GROUPS() != nullptr;
    appendClause(prefix->children, prefix->count);
    return Ptr(prefix);
  }

  return {};
}

Ptr makeKeepClause(GQLParser::KeepClauseContext *context) {
  if (!context) {
    return {};
  }

  auto clause = make_intrusive<GQLKeepClause>();
  clause->path_prefix = makePathPatternPrefix(context->pathPatternPrefix());
  chassert(clause->path_prefix);
  clause->children.push_back(clause->path_prefix);
  return Ptr(clause);
}

GraphMatchMode getGraphMatchMode(GQLParser::MatchModeContext *context) {
  if (!context) {
    return GraphMatchMode::None;
  }

  if (auto *repeatable = context->repeatableElementsMatchMode()) {
    return repeatable->elementBindingsOrElements() && repeatable->elementBindingsOrElements()->BINDINGS()
               ? GraphMatchMode::RepeatableElementBindings
               : GraphMatchMode::RepeatableElements;
  }

  if (auto *different = context->differentEdgesMatchMode()) {
    return different->edgeBindingsOrEdges() && different->edgeBindingsOrEdges()->BINDINGS() ? GraphMatchMode::DifferentEdgeBindings
                                                                                            : GraphMatchMode::DifferentEdges;
  }

  return GraphMatchMode::None;
}

PtrList makeGraphPatternYieldItems(GQLParser::GraphPatternYieldItemListContext *context) {
  PtrList items;

  if (!context) {
    return items;
  }

  for (auto *item : context->graphPatternYieldItem()) {
    auto *binding_reference = item->bindingVariableReference();
    auto *binding_variable = binding_reference ? binding_reference->bindingVariable() : nullptr;
    items.push_back(GQLExpr::identifier(getText(binding_variable)));
  }

  return items;
}

Ptr makeMatchClause(PatternBindingTable &&binding_table, bool optional = false) {
  auto clause = make_intrusive<GQLMatchClause>(optional);
  clause->match_mode = binding_table.match_mode;
  clause->keep_clause = std::move(binding_table.keep_clause);
  clause->path_patterns = std::move(binding_table.path_patterns);
  clause->where = std::move(binding_table.where);
  clause->yield_items = std::move(binding_table.yield_items);

  for (const auto &path : clause->path_patterns) {
    if (path) {
      clause->children.push_back(path);
    }
  }

  if (clause->keep_clause) {
    clause->children.push_back(clause->keep_clause);
  }

  if (clause->where) {
    clause->children.push_back(clause->where);
  }

  for (const auto &item : clause->yield_items) {
    if (item) {
      clause->children.push_back(item);
    }
  }

  return Ptr(clause);
}

Ptr makeGraphPatternBlock(PatternBindingTable &&binding_table, bool parenthesized) {
  auto block = make_intrusive<GQLGraphPatternBlock>();
  block->parenthesized = parenthesized;
  block->match_mode = binding_table.match_mode;
  block->keep_clause = std::move(binding_table.keep_clause);
  block->path_patterns = std::move(binding_table.path_patterns);
  block->where = std::move(binding_table.where);

  for (const auto &path : block->path_patterns) {
    appendClause(block->children, path);
  }

  appendClause(block->children, block->keep_clause);
  appendClause(block->children, block->where);

  return Ptr(block);
}

void populateMatchStatementBlock(GQLMatchStatementBlock &block, GQLParseTreeVisitor &visitor,
                                 GQLParser::MatchStatementBlockContext *context) {
  if (!context) {
    return;
  }

  for (auto *match_statement : context->matchStatement()) {
    auto match_clause = castAny<Ptr>(visitor.visit(match_statement));
    block.matches.push_back(match_clause);
    appendClause(block.children, match_clause);
  }
}

Ptr makeNodeOrEdgePattern(ElementPatternParts &&parts, bool is_edge, EdgeDirection direction) {
  if (is_edge) {
    auto edge = make_intrusive<GQLEdgePattern>(direction);
    edge->variable = std::move(parts.variable);
    edge->label_expression = std::move(parts.label_expression);
    edge->properties = std::move(parts.properties);
    edge->where = std::move(parts.where);

    if (edge->variable) {
      edge->children.push_back(edge->variable);
    }
    if (edge->label_expression) {
      edge->children.push_back(edge->label_expression);
    }
    if (edge->properties) {
      edge->children.push_back(edge->properties);
    }
    if (edge->where) {
      edge->children.push_back(edge->where);
    }

    return Ptr(edge);
  }

  auto node = make_intrusive<GQLNodePattern>();
  node->variable = std::move(parts.variable);
  node->label_expression = std::move(parts.label_expression);
  node->properties = std::move(parts.properties);
  node->where = std::move(parts.where);

  if (node->variable) {
    node->children.push_back(node->variable);
  }
  if (node->label_expression) {
    node->children.push_back(node->label_expression);
  }
  if (node->properties) {
    node->children.push_back(node->properties);
  }
  if (node->where) {
    node->children.push_back(node->where);
  }

  return Ptr(node);
}

std::any GQLParseTreeVisitor::visitMatchStatement(GQLParser::MatchStatementContext *context) {
  if (context->simpleMatchStatement()) {
    return castAny<Ptr>(visit(context->simpleMatchStatement()));
  }

  return castAny<Ptr>(visit(context->optionalMatchStatement()));
}

std::any GQLParseTreeVisitor::visitSimpleMatchStatement(GQLParser::SimpleMatchStatementContext *context) {
  auto binding_table = castAny<PatternBindingTable>(visit(context->graphPatternBindingTable()));
  return makeMatchClause(std::move(binding_table));
}

std::any GQLParseTreeVisitor::visitOptionalMatchStatement(GQLParser::OptionalMatchStatementContext *context) {
  auto *operand = context->optionalOperand();
  if (operand->simpleMatchStatement()) {
    auto clause = castAny<Ptr>(visit(operand->simpleMatchStatement()));
    auto *match_clause = clause ? clause->as<GQLMatchClause>() : nullptr;
    chassert(match_clause);

    if (match_clause) match_clause->optional = true;

    return clause;
  }

  if (auto *block_context = operand->matchStatementBlock()) {
    auto clause = make_intrusive<GQLMatchClause>(true);
    auto block = make_intrusive<GQLMatchStatementBlock>();
    block->parenthesized = operand->LEFT_PAREN() != nullptr;
    populateMatchStatementBlock(*block, *this, block_context);
    clause->optional_operand_block = Ptr(block);
    appendClause(clause->children, clause->optional_operand_block);
    return Ptr(clause);
  }

  chassert(false);
  throwUnsupported("complex optional match operand", context);
}

std::any GQLParseTreeVisitor::visitFilterStatement(GQLParser::FilterStatementContext *context) {
  if (context->whereClause()) {
    return Ptr(
        make_intrusive<GQLWhereClause>(GQLWhereClause::Type::Filter, castAny<Ptr>(visit(context->whereClause()->searchCondition()))));
  }

  return Ptr(make_intrusive<GQLWhereClause>(GQLWhereClause::Type::Filter, castAny<Ptr>(visit(context->searchCondition()))));
}

std::any GQLParseTreeVisitor::visitWhereClause(GQLParser::WhereClauseContext *context) {
  return Ptr(make_intrusive<GQLWhereClause>(castAny<Ptr>(visit(context->searchCondition()))));
}

std::any GQLParseTreeVisitor::visitGraphPatternBindingTable(GQLParser::GraphPatternBindingTableContext *context) {
  auto result = castAny<PatternBindingTable>(visit(context->graphPattern()));
  if (context->graphPatternYieldClause()) {
    result.yield_items = makeGraphPatternYieldItems(context->graphPatternYieldClause()->graphPatternYieldItemList());
  }
  return result;
}

std::any GQLParseTreeVisitor::visitGraphPattern(GQLParser::GraphPatternContext *context) {
  PatternBindingTable result;
  result.match_mode = getGraphMatchMode(context->matchMode());
  result.keep_clause = makeKeepClause(context->keepClause());
  result.path_patterns = castAny<PtrList>(visit(context->pathPatternList()));

  if (context->graphPatternWhereClause()) {
    result.where = castAny<Ptr>(visit(context->graphPatternWhereClause()));
  }

  return result;
}

std::any GQLParseTreeVisitor::visitGraphPatternWhereClause(GQLParser::GraphPatternWhereClauseContext *context) {
  return Ptr(make_intrusive<GQLWhereClause>(castAny<Ptr>(visit(context->searchCondition()))));
}

std::any GQLParseTreeVisitor::visitPathPatternList(GQLParser::PathPatternListContext *context) {
  PtrList patterns;

  for (auto *pattern : context->pathPattern()) {
    patterns.push_back(castAny<Ptr>(visit(pattern)));
  }

  return patterns;
}

std::any GQLParseTreeVisitor::visitPathPattern(GQLParser::PathPatternContext *context) {
  auto pattern = make_intrusive<GQLPathPattern>();

  if (context->pathVariableDeclaration()) {
    pattern->variable = GQLExpr::identifier(getText(context->pathVariableDeclaration()->pathVariable()->bindingVariable()));
    appendClause(pattern->children, pattern->variable);
  }

  if (context->pathPatternPrefix()) {
    pattern->prefix = makePathPatternPrefix(context->pathPatternPrefix());
    appendClause(pattern->children, pattern->prefix);
  }

  pattern->expression = castAny<Ptr>(visit(context->pathPatternExpression()));
  appendClause(pattern->children, pattern->expression);

  return Ptr(pattern);
}

std::any GQLParseTreeVisitor::visitPpePathTerm(GQLParser::PpePathTermContext *context) { return castAny<Ptr>(visit(context->pathTerm())); }

std::any GQLParseTreeVisitor::visitPpeMultisetAlternation(GQLParser::PpeMultisetAlternationContext *context) {
  return makePathPatternAlternation(GQLPathPatternAlternation::Kind::MultisetAlternation, context->pathTerm(), *this);
}

std::any GQLParseTreeVisitor::visitPpePatternUnion(GQLParser::PpePatternUnionContext *context) {
  return makePathPatternAlternation(GQLPathPatternAlternation::Kind::Union, context->pathTerm(), *this);
}

std::any GQLParseTreeVisitor::visitPathTerm(GQLParser::PathTermContext *context) {
  PtrList factors;

  for (auto *factor : context->pathFactor()) {
    factors.push_back(castAny<Ptr>(visit(factor)));
  }

  return Ptr(make_intrusive<GQLPathTerm>(std::move(factors)));
}

std::any GQLParseTreeVisitor::visitPfPathPrimary(GQLParser::PfPathPrimaryContext *context) {
  return castAny<Ptr>(visit(context->pathPrimary()));
}

std::any GQLParseTreeVisitor::visitPfQuantifiedPathPrimary(GQLParser::PfQuantifiedPathPrimaryContext *context) {
  auto node = castAny<Ptr>(visit(context->pathPrimary()));
  if (attachPathPrimaryQuantifier(node, makeQuantifier(context->graphPatternQuantifier()))) {
    return node;
  }

  auto wrapper = make_intrusive<GQLQuantifiedPathPrimary>();
  wrapper->operand = node;
  wrapper->quantifier = makeQuantifier(context->graphPatternQuantifier());
  appendClause(wrapper->children, wrapper->operand);
  appendClause(wrapper->children, wrapper->quantifier);
  return Ptr(wrapper);
}

std::any GQLParseTreeVisitor::visitPfQuestionedPathPrimary(GQLParser::PfQuestionedPathPrimaryContext *context) {
  auto node = castAny<Ptr>(visit(context->pathPrimary()));
  if (attachPathPrimaryQuantifier(node, makeQuestionQuantifier())) {
    return node;
  }

  auto wrapper = make_intrusive<GQLQuantifiedPathPrimary>();
  wrapper->operand = node;
  wrapper->quantifier = makeQuestionQuantifier();
  appendClause(wrapper->children, wrapper->operand);
  appendClause(wrapper->children, wrapper->quantifier);
  return Ptr(wrapper);
}

std::any GQLParseTreeVisitor::visitPpElementPattern(GQLParser::PpElementPatternContext *context) {
  return castAny<Ptr>(visit(context->elementPattern()));
}

std::any GQLParseTreeVisitor::visitPpParenthesizedPathPatternExpression(GQLParser::PpParenthesizedPathPatternExpressionContext *context) {
  auto *pattern_context = context->parenthesizedPathPatternExpression();
  auto pattern = make_intrusive<GQLParenthesizedPathPattern>();

  if (pattern_context->subpathVariableDeclaration()) {
    pattern->subpath_variable = GQLExpr::identifier(getText(pattern_context->subpathVariableDeclaration()->subpathVariable()));
    appendClause(pattern->children, pattern->subpath_variable);
  }

  if (pattern_context->pathModePrefix()) {
    pattern->prefix = makePathPatternPrefix(pattern_context->pathModePrefix());
    appendClause(pattern->children, pattern->prefix);
  }

  pattern->expression = castAny<Ptr>(visit(pattern_context->pathPatternExpression()));
  appendClause(pattern->children, pattern->expression);

  if (pattern_context->parenthesizedPathPatternWhereClause()) {
    pattern->where =
        Ptr(make_intrusive<GQLWhereClause>(castAny<Ptr>(visit(pattern_context->parenthesizedPathPatternWhereClause()->searchCondition()))));
    appendClause(pattern->children, pattern->where);
  }

  return Ptr(pattern);
}

std::any GQLParseTreeVisitor::visitPpSimplifiedPathPatternExpression(GQLParser::PpSimplifiedPathPatternExpressionContext *context) {
  return makeSimplifiedPathPattern(context->simplifiedPathPatternExpression());
}

std::any GQLParseTreeVisitor::visitElementPattern(GQLParser::ElementPatternContext *context) {
  if (context->nodePattern()) {
    return castAny<Ptr>(visit(context->nodePattern()));
  }

  return castAny<Ptr>(visit(context->edgePattern()));
}

std::any GQLParseTreeVisitor::visitNodePattern(GQLParser::NodePatternContext *context) {
  return makeNodeOrEdgePattern(castAny<ElementPatternParts>(visit(context->elementPatternFiller())), false);
}

std::any GQLParseTreeVisitor::visitElementPatternFiller(GQLParser::ElementPatternFillerContext *context) {
  ElementPatternParts parts;

  if (context->elementVariableDeclaration()) {
    parts.variable = GQLExpr::identifier(getText(context->elementVariableDeclaration()->elementVariable()->bindingVariable()));
  }

  if (context->isLabelExpression()) {
    parts.label_expression = castAny<Ptr>(visit(context->isLabelExpression()->labelExpression()));
  }

  if (context->elementPatternPredicate()) {
    if (context->elementPatternPredicate()->elementPropertySpecification()) {
      parts.properties = castAny<Ptr>(visit(context->elementPatternPredicate()->elementPropertySpecification()));
    } else if (context->elementPatternPredicate()->elementPatternWhereClause()) {
      parts.where = castAny<Ptr>(visit(context->elementPatternPredicate()->elementPatternWhereClause()));
    }
  }

  return parts;
}

std::any GQLParseTreeVisitor::visitElementPatternWhereClause(GQLParser::ElementPatternWhereClauseContext *context) {
  return Ptr(make_intrusive<GQLWhereClause>(castAny<Ptr>(visit(context->searchCondition()))));
}

std::any GQLParseTreeVisitor::visitElementPropertySpecification(GQLParser::ElementPropertySpecificationContext *context) {
  return Ptr(make_intrusive<GQLPropertyMap>(castAny<PtrList>(visit(context->propertyKeyValuePairList()))));
}

std::any GQLParseTreeVisitor::visitPropertyKeyValuePairList(GQLParser::PropertyKeyValuePairListContext *context) {
  PtrList items;

  for (auto *item : context->propertyKeyValuePair()) {
    items.push_back(castAny<Ptr>(visit(item)));
  }

  return items;
}

std::any GQLParseTreeVisitor::visitPropertyKeyValuePair(GQLParser::PropertyKeyValuePairContext *context) {
  return Ptr(
      make_intrusive<GQLPropertyItem>(getText(context->propertyName()->identifier()), castAny<Ptr>(visit(context->valueExpression()))));
}

std::any GQLParseTreeVisitor::visitEdgePattern(GQLParser::EdgePatternContext *context) {
  if (context->fullEdgePattern()) {
    return castAny<Ptr>(visit(context->fullEdgePattern()));
  }

  return castAny<Ptr>(visit(context->abbreviatedEdgePattern()));
}

std::any GQLParseTreeVisitor::visitFullEdgePattern(GQLParser::FullEdgePatternContext *context) {
  if (context->fullEdgePointingLeft()) {
    return castAny<Ptr>(visit(context->fullEdgePointingLeft()));
  }
  if (context->fullEdgeUndirected()) {
    return castAny<Ptr>(visit(context->fullEdgeUndirected()));
  }
  if (context->fullEdgePointingRight()) {
    return castAny<Ptr>(visit(context->fullEdgePointingRight()));
  }
  if (context->fullEdgeLeftOrUndirected()) {
    return castAny<Ptr>(visit(context->fullEdgeLeftOrUndirected()));
  }
  if (context->fullEdgeUndirectedOrRight()) {
    return castAny<Ptr>(visit(context->fullEdgeUndirectedOrRight()));
  }
  if (context->fullEdgeLeftOrRight()) {
    return castAny<Ptr>(visit(context->fullEdgeLeftOrRight()));
  }

  return castAny<Ptr>(visit(context->fullEdgeAnyDirection()));
}

std::any GQLParseTreeVisitor::visitFullEdgePointingLeft(GQLParser::FullEdgePointingLeftContext *context) {
  return makeNodeOrEdgePattern(castAny<ElementPatternParts>(visit(context->elementPatternFiller())), true, EdgeDirection::Left);
}

std::any GQLParseTreeVisitor::visitFullEdgeUndirected(GQLParser::FullEdgeUndirectedContext *context) {
  return makeNodeOrEdgePattern(castAny<ElementPatternParts>(visit(context->elementPatternFiller())), true, EdgeDirection::Undirected);
}

std::any GQLParseTreeVisitor::visitFullEdgePointingRight(GQLParser::FullEdgePointingRightContext *context) {
  return makeNodeOrEdgePattern(castAny<ElementPatternParts>(visit(context->elementPatternFiller())), true, EdgeDirection::Right);
}

std::any GQLParseTreeVisitor::visitFullEdgeLeftOrUndirected(GQLParser::FullEdgeLeftOrUndirectedContext *context) {
  return makeNodeOrEdgePattern(castAny<ElementPatternParts>(visit(context->elementPatternFiller())), true, EdgeDirection::LeftOrUndirected);
}

std::any GQLParseTreeVisitor::visitFullEdgeUndirectedOrRight(GQLParser::FullEdgeUndirectedOrRightContext *context) {
  return makeNodeOrEdgePattern(castAny<ElementPatternParts>(visit(context->elementPatternFiller())), true,
                               EdgeDirection::UndirectedOrRight);
}

std::any GQLParseTreeVisitor::visitFullEdgeLeftOrRight(GQLParser::FullEdgeLeftOrRightContext *context) {
  return makeNodeOrEdgePattern(castAny<ElementPatternParts>(visit(context->elementPatternFiller())), true, EdgeDirection::LeftOrRight);
}

std::any GQLParseTreeVisitor::visitFullEdgeAnyDirection(GQLParser::FullEdgeAnyDirectionContext *context) {
  return makeNodeOrEdgePattern(castAny<ElementPatternParts>(visit(context->elementPatternFiller())), true, EdgeDirection::Any);
}

std::any GQLParseTreeVisitor::visitAbbreviatedEdgePattern(GQLParser::AbbreviatedEdgePatternContext *context) {
  EdgeDirection direction = EdgeDirection::Any;

  if (context->LEFT_ARROW()) {
    direction = EdgeDirection::Left;
  } else if (context->RIGHT_ARROW()) {
    direction = EdgeDirection::Right;
  } else if (context->TILDE()) {
    direction = EdgeDirection::Undirected;
  } else if (context->LEFT_ARROW_TILDE()) {
    direction = EdgeDirection::LeftOrUndirected;
  } else if (context->TILDE_RIGHT_ARROW()) {
    direction = EdgeDirection::UndirectedOrRight;
  } else if (context->LEFT_MINUS_RIGHT()) {
    direction = EdgeDirection::LeftOrRight;
  }

  return makeNodeOrEdgePattern({}, true, direction);
}

std::any GQLParseTreeVisitor::visitLabelExpressionNegation(GQLParser::LabelExpressionNegationContext *context) {
  return GQLLabelExpression::unary("!", castAny<Ptr>(visit(context->labelExpression())));
}

std::any GQLParseTreeVisitor::visitLabelExpressionDisjunction(GQLParser::LabelExpressionDisjunctionContext *context) {
  return GQLLabelExpression::binary(GQLLabelExpression::Kind::Disjunction, castAny<Ptr>(visit(context->labelExpression(0))),
                                    castAny<Ptr>(visit(context->labelExpression(1))));
}

std::any GQLParseTreeVisitor::visitLabelExpressionConjunction(GQLParser::LabelExpressionConjunctionContext *context) {
  return GQLLabelExpression::binary(GQLLabelExpression::Kind::Conjunction, castAny<Ptr>(visit(context->labelExpression(0))),
                                    castAny<Ptr>(visit(context->labelExpression(1))));
}

std::any GQLParseTreeVisitor::visitLabelExpressionName(GQLParser::LabelExpressionNameContext *context) {
  return GQLLabelExpression::name(getText(context->labelName()->identifier()));
}

std::any GQLParseTreeVisitor::visitLabelExpressionWildcard(GQLParser::LabelExpressionWildcardContext *) {
  return GQLLabelExpression::wildcard();
}

std::any GQLParseTreeVisitor::visitLabelExpressionParenthesized(GQLParser::LabelExpressionParenthesizedContext *context) {
  return castAny<Ptr>(visit(context->labelExpression()));
}

}  // namespace OPENGQL

}  // namespace DB
