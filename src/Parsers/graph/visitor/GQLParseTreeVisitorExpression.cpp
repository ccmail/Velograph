#include <Parsers/graph/visitor/GQLVisitorCommon.h>

namespace DB {

namespace OPENGQL {

Ptr makeListConstructor(GQLParser::ListValueConstructorByEnumerationContext *context, GQLParseTreeVisitor &visitor) {
  auto list = make_intrusive<GQLListConstructor>();

  auto *elements = context ? context->listElementList() : nullptr;
  if (!elements) {
    return Ptr(list);
  }

  for (auto *element : elements->listElement()) {
    auto item = castAny<Ptr>(visitor.visit(element->valueExpression()));
    list->items.push_back(item);
    appendClause(list->children, item);
  }

  return Ptr(list);
}

Ptr makeRecordConstructor(GQLParser::RecordConstructorContext *context, GQLParseTreeVisitor &visitor) {
  auto record = make_intrusive<GQLRecordConstructor>();
  record->explicit_record_keyword = context && context->RECORD() != nullptr;

  auto *field_list = context && context->fieldsSpecification() ? context->fieldsSpecification()->fieldList() : nullptr;
  if (!field_list) {
    return Ptr(record);
  }

  for (auto *field : field_list->field()) {
    auto item = Ptr(
        make_intrusive<GQLPropertyItem>(getText(field->fieldName()->identifier()), castAny<Ptr>(visitor.visit(field->valueExpression()))));
    record->fields.push_back(item);
    appendClause(record->children, item);
  }

  return Ptr(record);
}

Ptr makeAggregateFunction(GQLParser::AggregateFunctionContext *context, GQLParseTreeVisitor &visitor) {
  if (!context) {
    return {};
  }

  if (context->COUNT() && context->ASTERISK()) {
    PtrList arguments;
    arguments.push_back(GQLExpr::literal("*"));
    return GQLExpr::functionCall("COUNT", std::move(arguments));
  }

  if (auto *general = context->generalSetFunction()) {
    Ptr argument = castAny<Ptr>(visitor.visit(general->valueExpression()));
    PtrList arguments;
    arguments.push_back(argument);
    auto set_quantifier = GQLExpr::SetQuantifier::None;

    if (auto *quantifier = general->setQuantifier()) {
      set_quantifier = quantifier->DISTINCT() ? GQLExpr::SetQuantifier::Distinct : GQLExpr::SetQuantifier::All;
    }

    return GQLExpr::functionCall(getText(general->generalSetFunctionType()), std::move(arguments), set_quantifier);
  }

  if (auto *binary = context->binarySetFunction()) {
    PtrList arguments;
    auto set_quantifier = GQLExpr::SetQuantifier::None;

    if (auto *dependent = binary->dependentValueExpression()) {
      if (auto *quantifier = dependent->setQuantifier()) {
        set_quantifier = quantifier->DISTINCT() ? GQLExpr::SetQuantifier::Distinct : GQLExpr::SetQuantifier::All;
      }

      arguments.push_back(castAny<Ptr>(visitor.visit(dependent->numericValueExpression())));
    }

    if (auto *independent = binary->independentValueExpression()) {
      arguments.push_back(castAny<Ptr>(visitor.visit(independent)));
    }

    return GQLExpr::functionCall(getText(binary->binarySetFunctionType()), std::move(arguments), set_quantifier);
  }

  throwUnsupported("aggregate function", context);
}

Ptr makeNumericValueFunction(GQLParser::NumericValueFunctionContext *numeric, GQLParseTreeVisitor &visitor) {
  if (auto *length = numeric->lengthExpression()) {
    if (auto *char_length = length->charLengthExpression()) {
      PtrList arguments;
      arguments.push_back(castAny<Ptr>(visitor.visit(char_length->characterStringValueExpression())));
      return GQLExpr::functionCall(char_length->CHAR_LENGTH() ? "CHAR_LENGTH" : "CHARACTER_LENGTH", std::move(arguments));
    }

    if (auto *byte_length = length->byteLengthExpression()) {
      PtrList arguments;
      arguments.push_back(castAny<Ptr>(visitor.visit(byte_length->byteStringValueExpression())));
      return GQLExpr::functionCall(byte_length->BYTE_LENGTH() ? "BYTE_LENGTH" : "OCTET_LENGTH", std::move(arguments));
    }

    if (auto *path_length = length->pathLengthExpression()) {
      PtrList arguments;
      arguments.push_back(castAny<Ptr>(visitor.visit(path_length->pathValueExpression())));
      return GQLExpr::functionCall("PATH_LENGTH", std::move(arguments));
    }
  }

  if (auto *cardinality = numeric->cardinalityExpression()) {
    PtrList arguments;
    if (cardinality->cardinalityExpressionArgument()) {
      arguments.push_back(castAny<Ptr>(visitor.visit(cardinality->cardinalityExpressionArgument())));
      return GQLExpr::functionCall("CARDINALITY", std::move(arguments));
    }

    if (cardinality->listValueExpression()) {
      arguments.push_back(castAny<Ptr>(visitor.visit(cardinality->listValueExpression())));
      return GQLExpr::functionCall("SIZE", std::move(arguments));
    }
  }

  if (auto *absolute = numeric->absoluteValueExpression()) {
    PtrList arguments;
    arguments.push_back(castAny<Ptr>(visitor.visit(absolute->valueExpression())));
    return GQLExpr::functionCall("ABS", std::move(arguments));
  }

  if (auto *floor_fn = numeric->floorFunction()) {
    PtrList arguments;
    arguments.push_back(castAny<Ptr>(visitor.visit(floor_fn->numericValueExpression())));
    return GQLExpr::functionCall("FLOOR", std::move(arguments));
  }

  if (auto *ceiling_fn = numeric->ceilingFunction()) {
    PtrList arguments;
    arguments.push_back(castAny<Ptr>(visitor.visit(ceiling_fn->numericValueExpression())));
    return GQLExpr::functionCall(ceiling_fn->CEIL() ? "CEIL" : "CEILING", std::move(arguments));
  }

  if (auto *modulus = numeric->modulusExpression()) {
    PtrList arguments;
    arguments.push_back(castAny<Ptr>(visitor.visit(modulus->numericValueExpressionDividend())));
    arguments.push_back(castAny<Ptr>(visitor.visit(modulus->numericValueExpressionDivisor())));
    return GQLExpr::functionCall("MOD", std::move(arguments));
  }

  if (auto *power = numeric->powerFunction()) {
    PtrList arguments;
    arguments.push_back(castAny<Ptr>(visitor.visit(power->numericValueExpressionBase())));
    arguments.push_back(castAny<Ptr>(visitor.visit(power->numericValueExpressionExponent())));
    return GQLExpr::functionCall("POWER", std::move(arguments));
  }

  if (auto *sqrt_fn = numeric->squareRoot()) {
    PtrList arguments;
    arguments.push_back(castAny<Ptr>(visitor.visit(sqrt_fn->numericValueExpression())));
    return GQLExpr::functionCall("SQRT", std::move(arguments));
  }

  if (auto *trig = numeric->trigonometricFunction()) {
    PtrList arguments;
    arguments.push_back(castAny<Ptr>(visitor.visit(trig->numericValueExpression())));
    return GQLExpr::functionCall(getText(trig->trigonometricFunctionName()), std::move(arguments));
  }

  if (auto *gen_log = numeric->generalLogarithmFunction()) {
    PtrList arguments;
    arguments.push_back(castAny<Ptr>(visitor.visit(gen_log->generalLogarithmBase())));
    arguments.push_back(castAny<Ptr>(visitor.visit(gen_log->generalLogarithmArgument())));
    return GQLExpr::functionCall("LOG", std::move(arguments));
  }

  if (auto *common_log = numeric->commonLogarithm()) {
    PtrList arguments;
    arguments.push_back(castAny<Ptr>(visitor.visit(common_log->numericValueExpression())));
    return GQLExpr::functionCall("LOG10", std::move(arguments));
  }

  if (auto *nat_log = numeric->naturalLogarithm()) {
    PtrList arguments;
    arguments.push_back(castAny<Ptr>(visitor.visit(nat_log->numericValueExpression())));
    return GQLExpr::functionCall("LN", std::move(arguments));
  }

  if (auto *exp_fn = numeric->exponentialFunction()) {
    PtrList arguments;
    arguments.push_back(castAny<Ptr>(visitor.visit(exp_fn->numericValueExpression())));
    return GQLExpr::functionCall("EXP", std::move(arguments));
  }

  throwUnsupported("numeric value function", numeric);
}

Ptr makeCharacterOrByteStringFunction(GQLParser::CharacterOrByteStringFunctionContext *context, GQLParseTreeVisitor &visitor) {
  if (auto *sub = context->subCharacterOrByteString()) {
    PtrList arguments;
    arguments.push_back(castAny<Ptr>(visitor.visit(sub->valueExpression())));
    arguments.push_back(castAny<Ptr>(visitor.visit(sub->stringLength())));
    return GQLExpr::functionCall(sub->LEFT() ? "LEFT" : "RIGHT", std::move(arguments));
  }

  if (auto *fold = context->foldCharacterString()) {
    PtrList arguments;
    arguments.push_back(castAny<Ptr>(visitor.visit(fold->valueExpression())));
    return GQLExpr::functionCall(fold->UPPER() ? "UPPER" : "LOWER", std::move(arguments));
  }

  if (auto *multi_trim = context->trimMultiCharacterCharacterString()) {
    PtrList arguments;
    for (auto *expr : multi_trim->valueExpression()) arguments.push_back(castAny<Ptr>(visitor.visit(expr)));

    String name;
    if (multi_trim->BTRIM())
      name = "BTRIM";
    else if (multi_trim->LTRIM())
      name = "LTRIM";
    else
      name = "RTRIM";
    return GQLExpr::functionCall(name, std::move(arguments));
  }

  if (auto *normalize = context->normalizeCharacterString()) {
    PtrList arguments;
    arguments.push_back(castAny<Ptr>(visitor.visit(normalize->valueExpression())));
    if (normalize->normalForm()) arguments.push_back(GQLExpr::literal(getText(normalize->normalForm())));
    return GQLExpr::functionCall("NORMALIZE", std::move(arguments));
  }

  if (auto *single_trim = context->trimSingleCharacterOrByteString()) {
    auto *operands = single_trim->trimOperands();
    auto source = castAny<Ptr>(visitor.visit(operands->trimCharacterOrByteStringSource()));

    GQLExpr::TrimSpec spec = GQLExpr::TrimSpec::None;
    if (auto *ts = operands->trimSpecification()) {
      if (ts->LEADING())
        spec = GQLExpr::TrimSpec::Leading;
      else if (ts->TRAILING())
        spec = GQLExpr::TrimSpec::Trailing;
      else
        spec = GQLExpr::TrimSpec::Both;
    }

    Ptr trim_char;
    if (auto *tc = operands->trimCharacterOrByteString()) trim_char = castAny<Ptr>(visitor.visit(tc->valueExpression()));

    return GQLExpr::trimString(std::move(source), spec, std::move(trim_char));
  }

  throwUnsupported("character or byte string function", context);
}

Ptr makeDatetimeValueFunction(GQLParser::DatetimeValueFunctionContext *context, GQLParseTreeVisitor &visitor) {
  if (auto *date_fn = context->dateFunction()) {
    if (date_fn->CURRENT_DATE()) return GQLExpr::bareKeywordFunction("CURRENT_DATE");
    PtrList args;
    if (auto *params = date_fn->dateFunctionParameters()) {
      if (auto *rc = params->recordConstructor())
        args.push_back(castAny<Ptr>(visitor.visit(rc)));
      else
        args.push_back(GQLExpr::literal(getText(params)));
    }
    return GQLExpr::functionCall("DATE", std::move(args));
  }

  if (auto *time_fn = context->timeFunction()) {
    if (time_fn->CURRENT_TIME()) return GQLExpr::bareKeywordFunction("CURRENT_TIME");
    PtrList args;
    if (auto *params = time_fn->timeFunctionParameters()) {
      if (auto *rc = params->recordConstructor())
        args.push_back(castAny<Ptr>(visitor.visit(rc)));
      else
        args.push_back(GQLExpr::literal(getText(params)));
    }
    return GQLExpr::functionCall("ZONED_TIME", std::move(args));
  }

  if (auto *datetime_fn = context->datetimeFunction()) {
    if (datetime_fn->CURRENT_TIMESTAMP()) return GQLExpr::bareKeywordFunction("CURRENT_TIMESTAMP");
    PtrList args;
    if (auto *params = datetime_fn->datetimeFunctionParameters()) {
      if (auto *rc = params->recordConstructor())
        args.push_back(castAny<Ptr>(visitor.visit(rc)));
      else
        args.push_back(GQLExpr::literal(getText(params)));
    }
    return GQLExpr::functionCall("ZONED_DATETIME", std::move(args));
  }

  if (auto *localtime_fn = context->localtimeFunction()) {
    if (!localtime_fn->LEFT_PAREN()) return GQLExpr::bareKeywordFunction("LOCAL_TIME");
    PtrList args;
    if (auto *params = localtime_fn->timeFunctionParameters()) {
      if (auto *rc = params->recordConstructor())
        args.push_back(castAny<Ptr>(visitor.visit(rc)));
      else
        args.push_back(GQLExpr::literal(getText(params)));
    }
    return GQLExpr::functionCall("LOCAL_TIME", std::move(args));
  }

  if (auto *localdatetime_fn = context->localdatetimeFunction()) {
    if (localdatetime_fn->LOCAL_TIMESTAMP()) return GQLExpr::bareKeywordFunction("LOCAL_TIMESTAMP");
    PtrList args;
    if (auto *params = localdatetime_fn->datetimeFunctionParameters()) {
      if (auto *rc = params->recordConstructor())
        args.push_back(castAny<Ptr>(visitor.visit(rc)));
      else
        args.push_back(GQLExpr::literal(getText(params)));
    }
    return GQLExpr::functionCall("LOCAL_DATETIME", std::move(args));
  }

  throwUnsupported("datetime value function", context);
}

Ptr makeDurationValueFunction(GQLParser::DurationValueFunctionContext *context, GQLParseTreeVisitor &visitor) {
  if (auto *duration_fn = context->durationFunction()) {
    PtrList args;
    auto *params = duration_fn->durationFunctionParameters();
    if (auto *rc = params->recordConstructor())
      args.push_back(castAny<Ptr>(visitor.visit(rc)));
    else
      args.push_back(GQLExpr::literal(getText(params)));
    return GQLExpr::functionCall("DURATION", std::move(args));
  }

  if (auto *absolute = context->absoluteValueExpression()) {
    PtrList args;
    args.push_back(castAny<Ptr>(visitor.visit(absolute->valueExpression())));
    return GQLExpr::functionCall("ABS", std::move(args));
  }

  throwUnsupported("duration value function", context);
}

Ptr makeListValueFunction(GQLParser::ListValueFunctionContext *context, GQLParseTreeVisitor &visitor) {
  if (auto *elements_fn = context->elementsFunction()) {
    PtrList args;
    args.push_back(castAny<Ptr>(visitor.visit(elements_fn->pathValueExpression())));
    return GQLExpr::functionCall("ELEMENTS", std::move(args));
  }

  if (auto *trim_list = context->trimListFunction()) {
    PtrList args;
    args.push_back(castAny<Ptr>(visitor.visit(trim_list->listValueExpression())));
    args.push_back(castAny<Ptr>(visitor.visit(trim_list->numericValueExpression())));
    return GQLExpr::functionCall("TRIM", std::move(args));
  }

  throwUnsupported("list value function", context);
}

Ptr makeValueFunction(GQLParser::ValueFunctionContext *context, GQLParseTreeVisitor &visitor) {
  if (!context) return {};

  if (auto *numeric = context->numericValueFunction()) return castAny<Ptr>(visitor.visit(numeric));

  if (auto *char_fn = context->characterOrByteStringFunction()) return castAny<Ptr>(visitor.visit(char_fn));

  if (auto *datetime = context->datetimeValueFunction()) return castAny<Ptr>(visitor.visit(datetime));

  if (auto *duration = context->durationValueFunction()) return castAny<Ptr>(visitor.visit(duration));

  if (auto *list_fn = context->listValueFunction()) return castAny<Ptr>(visitor.visit(list_fn));

  if (auto *dt_sub = context->datetimeSubtraction()) {
    auto *params = dt_sub->datetimeSubtractionParameters();
    auto left = castAny<Ptr>(visitor.visit(params->datetimeValueExpression1()->datetimeValueExpression()));
    auto right = castAny<Ptr>(visitor.visit(params->datetimeValueExpression2()->datetimeValueExpression()));

    GQLExpr::TemporalQualifier qualifier = GQLExpr::TemporalQualifier::None;
    if (auto *tq = dt_sub->temporalDurationQualifier()) {
      qualifier = tq->YEAR() ? GQLExpr::TemporalQualifier::YearToMonth : GQLExpr::TemporalQualifier::DayToSecond;
    }
    return GQLExpr::durationBetween(std::move(left), std::move(right), qualifier);
  }

  throwUnsupported("value function", context);
}

Ptr makeResultExpr(GQLParser::ResultContext *context, GQLParseTreeVisitor &visitor) {
  if (!context) return {};

  if (context->nullLiteral()) return GQLExpr::specialValue("NULL");

  if (auto *result_expr = context->resultExpression()) return castAny<Ptr>(visitor.visit(result_expr));

  throwUnsupported("result expression", context);
}

Ptr makeCaseExpression(GQLParser::CaseExpressionContext *context, GQLParseTreeVisitor &visitor);
Ptr makeCastSpecification(GQLParser::CastSpecificationContext *context, GQLParseTreeVisitor &visitor);

Ptr makeValueQueryExpr(GQLParser::ValueQueryExpressionContext *context, GQLParseTreeVisitor &visitor) {
  auto subquery = castAny<Ptr>(visitor.visit(context->nestedQuerySpecification()));
  return GQLExpr::valueQuery(std::move(subquery));
}

Ptr makeLetValueExpr(GQLParser::LetValueExpressionContext *context, GQLParseTreeVisitor &visitor) {
  PtrList bindings;
  for (auto *def : context->letVariableDefinitionList()->letVariableDefinition()) {
    Ptr assignment;
    if (auto *value_definition = def->valueVariableDefinition()) {
      Ptr value;
      Ptr type;
      if (auto *initializer = value_definition->optTypedValueInitializer()) {
        type = makeValueType(initializer->typed(), initializer->valueType());
        if (initializer->valueInitializer()) value = castAny<Ptr>(visitor.visit(initializer->valueInitializer()));
      }
      assignment =
          Ptr(make_intrusive<GQLAssignmentItem>(getText(value_definition->bindingVariable()), std::move(value), true, std::move(type)));
    } else {
      assignment =
          Ptr(make_intrusive<GQLAssignmentItem>(getText(def->bindingVariable()), castAny<Ptr>(visitor.visit(def->valueExpression()))));
    }
    bindings.push_back(std::move(assignment));
  }
  auto body = castAny<Ptr>(visitor.visit(context->valueExpression()));
  return GQLExpr::letExpr(std::move(bindings), std::move(body));
}

Ptr makePathValueConstructorExpr(GQLParser::PathValueConstructorContext *context, GQLParseTreeVisitor &visitor) {
  auto *by_enum = context->pathValueConstructorByEnumeration();
  auto *elem_list = by_enum->pathElementList();
  PtrList elements;
  elements.push_back(castAny<Ptr>(visitor.visit(elem_list->pathElementListStart()->nodeReferenceValueExpression())));
  for (auto *step : elem_list->pathElementListStep()) {
    elements.push_back(castAny<Ptr>(visitor.visit(step->edgeReferenceValueExpression())));
    elements.push_back(castAny<Ptr>(visitor.visit(step->nodeReferenceValueExpression())));
  }
  return GQLExpr::pathConstructor(std::move(elements));
}

template <typename Context>
Ptr tryMakeStructuredValuePrimary(Context *context, GQLParseTreeVisitor &visitor) {
  if (auto *value_query = context->valueQueryExpression()) return castAny<Ptr>(visitor.visit(value_query));
  if (auto *let_expr = context->letValueExpression()) return castAny<Ptr>(visitor.visit(let_expr));
  if (auto *path_ctor = context->pathValueConstructor()) return castAny<Ptr>(visitor.visit(path_ctor));
  return {};
}

Ptr makeNpvepSpecialCaseExpr(GQLParser::NonParenthesizedValueExpressionPrimarySpecialCaseContext *special, GQLParseTreeVisitor &visitor) {
  if (special->unsignedValueSpecification()) return castAny<Ptr>(visitor.visit(special->unsignedValueSpecification()));
  if (special->aggregateFunction()) return castAny<Ptr>(visitor.visit(special->aggregateFunction()));
  if (special->caseExpression()) return castAny<Ptr>(visitor.visit(special->caseExpression()));
  if (special->castSpecification()) return castAny<Ptr>(visitor.visit(special->castSpecification()));
  if (special->element_idFunction()) {
    PtrList args;
    args.push_back(GQLExpr::identifier(getElementVariableName(special->element_idFunction()->elementVariableReference())));
    return GQLExpr::functionCall("ELEMENT_ID", std::move(args));
  }
  if (special->propertyName()) {
    auto base = castAny<Ptr>(visitor.visit(special->valueExpressionPrimary()));
    return GQLExpr::property(std::move(base), getText(special->propertyName()->identifier()));
  }
  if (auto structured = tryMakeStructuredValuePrimary(special, visitor)) return structured;
  throwUnsupported("non-parenthesized value expression primary special case", special);
}

Ptr makeNpvepExpr(GQLParser::NonParenthesizedValueExpressionPrimaryContext *npvep, GQLParseTreeVisitor &visitor) {
  if (auto *bvr = npvep->bindingVariableReference()) return GQLExpr::identifier(getText(bvr->bindingVariable()));
  if (auto *special = npvep->nonParenthesizedValueExpressionPrimarySpecialCase()) return makeNpvepSpecialCaseExpr(special, visitor);
  throwUnsupported("non-parenthesized value expression primary", npvep);
}

Ptr makeObjectExpressionPrimary(GQLParser::ObjectExpressionPrimaryContext *context, GQLParseTreeVisitor &visitor) {
  if (context->VARIABLE()) return GQLExpr::variableExpression(castAny<Ptr>(visitor.visit(context->valueExpressionPrimary())));
  if (auto *paren = context->parenthesizedValueExpression()) return castAny<Ptr>(visitor.visit(paren));
  if (auto *special = context->nonParenthesizedValueExpressionPrimarySpecialCase()) return makeNpvepSpecialCaseExpr(special, visitor);
  throwUnsupported("object expression primary", context);
}

Ptr makeWhenOperandExpr(GQLParser::WhenOperandContext *wo, GQLParseTreeVisitor &visitor, Ptr case_operand = {}) {
  if (auto *npvep = wo->nonParenthesizedValueExpressionPrimary()) return makeNpvepExpr(npvep, visitor);

  auto cloneLeft = [&]() -> Ptr {
    if (!case_operand) throwUnsupported("simple CASE operand", wo);
    return case_operand->clone();
  };

  if (wo->compOp()) {
    auto right = castAny<Ptr>(visitor.visit(wo->valueExpression()));
    return GQLExpr::binaryOp(getText(wo->compOp()), cloneLeft(), std::move(right));
  }

  if (auto *null_part = wo->nullPredicatePart2()) {
    const String op = null_part->NOT() ? "IS NOT" : "IS";
    return GQLExpr::binaryOp(op, cloneLeft(), GQLExpr::specialValue("NULL"));
  }

  if (auto *vt_part = wo->valueTypePredicatePart2()) {
    const String op = vt_part->NOT() ? "IS NOT TYPED" : "IS TYPED";
    return GQLExpr::binaryOp(op, cloneLeft(), makeValueType(nullptr, vt_part->valueType()));
  }

  if (auto *dir_part = wo->directedPredicatePart2()) {
    const String op = dir_part->NOT() ? "IS NOT" : "IS";
    return GQLExpr::binaryOp(op, cloneLeft(), GQLExpr::literal("DIRECTED"));
  }

  if (auto *label_part = wo->labeledPredicatePart2()) {
    auto *labeled_or_colon = label_part->isLabeledOrColon();
    String op = ":";
    if (labeled_or_colon->IS()) op = labeled_or_colon->NOT() ? "IS NOT LABELED" : "IS LABELED";
    return GQLExpr::binaryOp(op, cloneLeft(), castAny<Ptr>(visitor.visit(label_part->labelExpression())));
  }

  if (auto *src_part = wo->sourcePredicatePart2()) {
    const String op = src_part->NOT() ? "IS NOT SOURCE OF" : "IS SOURCE OF";
    return GQLExpr::binaryOp(op, cloneLeft(),
                             GQLExpr::identifier(getElementVariableName(src_part->edgeReference()->elementVariableReference())));
  }

  if (auto *dst_part = wo->destinationPredicatePart2()) {
    const String op = dst_part->NOT() ? "IS NOT DESTINATION OF" : "IS DESTINATION OF";
    return GQLExpr::binaryOp(op, cloneLeft(),
                             GQLExpr::identifier(getElementVariableName(dst_part->edgeReference()->elementVariableReference())));
  }

  if (auto *np = wo->normalizedPredicatePart2()) {
    bool negated = np->NOT() != nullptr;
    auto form = GQLExpr::NormalForm::None;
    if (auto *nf = np->normalForm()) {
      if (nf->NFC())
        form = GQLExpr::NormalForm::NFC;
      else if (nf->NFD())
        form = GQLExpr::NormalForm::NFD;
      else if (nf->NFKC())
        form = GQLExpr::NormalForm::NFKC;
      else if (nf->NFKD())
        form = GQLExpr::NormalForm::NFKD;
    }
    String op = negated ? "IS NOT" : "IS";
    String right_text;
    if (form != GQLExpr::NormalForm::None) {
      static const char *form_names[] = {"", "NFC", "NFD", "NFKC", "NFKD"};
      right_text = form_names[static_cast<UInt8>(form)];
      right_text += " NORMALIZED";
    } else {
      right_text = "NORMALIZED";
    }
    return GQLExpr::binaryOp(op, cloneLeft(), GQLExpr::literal(right_text));
  }

  throwUnsupported("when operand", wo);
}

Ptr makeCaseExpression(GQLParser::CaseExpressionContext *context, GQLParseTreeVisitor &visitor) {
  if (!context) return {};

  if (auto *abbreviation = context->caseAbbreviation()) {
    PtrList arguments;
    for (auto *value_expr : abbreviation->valueExpression()) arguments.push_back(castAny<Ptr>(visitor.visit(value_expr)));

    String name = abbreviation->NULLIF() ? "NULLIF" : "COALESCE";
    return GQLExpr::functionCall(name, std::move(arguments));
  }

  auto *spec = context->caseSpecification();
  if (!spec) throwUnsupported("case expression", context);

  if (auto *searched = spec->searchedCase()) {
    auto expr = make_intrusive<GQLCaseExpr>(GQLCaseExpr::Form::Searched);

    for (auto *clause : searched->searchedWhenClause()) {
      auto when_expr = castAny<Ptr>(visitor.visit(clause->searchCondition()));
      auto then_expr = makeResultExpr(clause->result(), visitor);
      expr->when_operands.push_back(when_expr);
      expr->then_results.push_back(then_expr);
      if (when_expr) expr->children.push_back(when_expr);
      if (then_expr) expr->children.push_back(then_expr);
    }

    if (auto *else_clause = searched->elseClause()) {
      expr->else_result = makeResultExpr(else_clause->result(), visitor);
      if (expr->else_result) expr->children.push_back(expr->else_result);
    }

    return Ptr(expr);
  }

  if (auto *simple = spec->simpleCase()) {
    auto expr = make_intrusive<GQLCaseExpr>(GQLCaseExpr::Form::Simple);

    if (auto *operand = simple->caseOperand()) {
      if (auto *elem_ref = operand->elementVariableReference())
        expr->operand = GQLExpr::identifier(getElementVariableName(elem_ref));
      else if (auto *npvep = operand->nonParenthesizedValueExpressionPrimary())
        expr->operand = makeNpvepExpr(npvep, visitor);
      if (expr->operand) expr->children.push_back(expr->operand);
    }

    for (auto *clause : simple->simpleWhenClause()) {
      Ptr when_expr;
      if (auto *when_list = clause->whenOperandList()) {
        auto operands = when_list->whenOperand();
        if (operands.size() == 1) {
          when_expr = makeWhenOperandExpr(operands.front(), visitor, expr->operand);
        } else {
          PtrList items;
          for (auto *wo : operands) items.push_back(makeWhenOperandExpr(wo, visitor, expr->operand));
          when_expr = GQLExpr::exprList(std::move(items));
        }
      }

      auto then_expr = makeResultExpr(clause->result(), visitor);
      expr->when_operands.push_back(when_expr);
      expr->then_results.push_back(then_expr);
      if (when_expr) expr->children.push_back(when_expr);
      if (then_expr) expr->children.push_back(then_expr);
    }

    if (auto *else_clause = simple->elseClause()) {
      expr->else_result = makeResultExpr(else_clause->result(), visitor);
      if (expr->else_result) expr->children.push_back(expr->else_result);
    }

    return Ptr(expr);
  }

  throwUnsupported("case specification", context);
}

Ptr makeCastSpecification(GQLParser::CastSpecificationContext *context, GQLParseTreeVisitor &visitor) {
  if (!context) return {};

  Ptr operand;
  if (auto *cast_operand = context->castOperand()) {
    if (cast_operand->nullLiteral())
      operand = GQLExpr::specialValue("NULL");
    else if (cast_operand->valueExpression())
      operand = castAny<Ptr>(visitor.visit(cast_operand->valueExpression()));
  }

  auto *target = context->castTarget();
  return GQLExpr::castExpr(std::move(operand), makeValueType(nullptr, target ? target->valueType() : nullptr));
}

std::any GQLParseTreeVisitor::visitSearchCondition(GQLParser::SearchConditionContext *context) {
  return visit(context->booleanValueExpression());
}

std::any GQLParseTreeVisitor::visitAggregatingValueExpression(GQLParser::AggregatingValueExpressionContext *context) {
  return castAny<Ptr>(visit(context->valueExpression()));
}

std::any GQLParseTreeVisitor::visitConjunctiveExprAlt(GQLParser::ConjunctiveExprAltContext *context) {
  return GQLExpr::binaryOp("AND", castAny<Ptr>(visit(context->valueExpression(0))), castAny<Ptr>(visit(context->valueExpression(1))));
}

std::any GQLParseTreeVisitor::visitMultDivExprAlt(GQLParser::MultDivExprAltContext *context) {
  return GQLExpr::binaryOp(context->operator_ ? context->operator_->getText() : String("/"),
                           castAny<Ptr>(visit(context->valueExpression(0))), castAny<Ptr>(visit(context->valueExpression(1))));
}

std::any GQLParseTreeVisitor::visitSignedExprAlt(GQLParser::SignedExprAltContext *context) {
  if (context->PLUS_SIGN()) {
    return castAny<Ptr>(visit(context->valueExpression()));
  }

  return GQLExpr::unaryOp("-", castAny<Ptr>(visit(context->valueExpression())));
}

std::any GQLParseTreeVisitor::visitIsNotExprAlt(GQLParser::IsNotExprAltContext *context) {
  const String op = context->NOT() ? "IS NOT" : "IS";
  return GQLExpr::binaryOp(op, castAny<Ptr>(visit(context->valueExpression())), GQLExpr::specialValue(getText(context->truthValue())));
}

std::any GQLParseTreeVisitor::visitNotExprAlt(GQLParser::NotExprAltContext *context) {
  return GQLExpr::unaryOp("NOT ", castAny<Ptr>(visit(context->valueExpression())));
}

std::any GQLParseTreeVisitor::visitValueFunctionExprAlt(GQLParser::ValueFunctionExprAltContext *context) {
  return visit(context->valueFunction());
}

std::any GQLParseTreeVisitor::visitConcatenationExprAlt(GQLParser::ConcatenationExprAltContext *context) {
  return GQLExpr::binaryOp("||", castAny<Ptr>(visit(context->valueExpression(0))), castAny<Ptr>(visit(context->valueExpression(1))));
}

std::any GQLParseTreeVisitor::visitDisjunctiveExprAlt(GQLParser::DisjunctiveExprAltContext *context) {
  return GQLExpr::binaryOp(context->OR() ? "OR" : "XOR", castAny<Ptr>(visit(context->valueExpression(0))),
                           castAny<Ptr>(visit(context->valueExpression(1))));
}

std::any GQLParseTreeVisitor::visitComparisonExprAlt(GQLParser::ComparisonExprAltContext *context) {
  return GQLExpr::binaryOp(getText(context->compOp()), castAny<Ptr>(visit(context->valueExpression(0))),
                           castAny<Ptr>(visit(context->valueExpression(1))));
}

std::any GQLParseTreeVisitor::visitPrimaryExprAlt(GQLParser::PrimaryExprAltContext *context) {
  return castAny<Ptr>(visit(context->valueExpressionPrimary()));
}

std::any GQLParseTreeVisitor::visitAddSubtractExprAlt(GQLParser::AddSubtractExprAltContext *context) {
  return GQLExpr::binaryOp(context->PLUS_SIGN() ? "+" : "-", castAny<Ptr>(visit(context->valueExpression(0))),
                           castAny<Ptr>(visit(context->valueExpression(1))));
}

std::any GQLParseTreeVisitor::visitPredicateExprAlt(GQLParser::PredicateExprAltContext *context) {
  auto *predicate = context->predicate();

  if (auto *null_predicate = predicate->nullPredicate()) {
    const String op = null_predicate->nullPredicatePart2()->NOT() ? "IS NOT" : "IS";
    return GQLExpr::binaryOp(op, castAny<Ptr>(visit(null_predicate->valueExpressionPrimary())), GQLExpr::specialValue("NULL"));
  }

  if (auto *value_type_predicate = predicate->valueTypePredicate()) {
    const String op = value_type_predicate->valueTypePredicatePart2()->NOT() ? "IS NOT TYPED" : "IS TYPED";
    return GQLExpr::binaryOp(op, castAny<Ptr>(visit(value_type_predicate->valueExpressionPrimary())),
                             makeValueType(nullptr, value_type_predicate->valueTypePredicatePart2()->valueType()));
  }

  if (auto *directed_predicate = predicate->directedPredicate()) {
    const String op = directed_predicate->directedPredicatePart2()->NOT() ? "IS NOT" : "IS";
    return GQLExpr::binaryOp(op, GQLExpr::identifier(getElementVariableName(directed_predicate->elementVariableReference())),
                             GQLExpr::literal("DIRECTED"));
  }

  if (auto *labeled_predicate = predicate->labeledPredicate()) {
    auto *label_part = labeled_predicate->labeledPredicatePart2()->isLabeledOrColon();
    String op = ":";

    if (label_part->IS()) op = label_part->NOT() ? "IS NOT LABELED" : "IS LABELED";

    return GQLExpr::binaryOp(op, GQLExpr::identifier(getElementVariableName(labeled_predicate->elementVariableReference())),
                             castAny<Ptr>(visit(labeled_predicate->labeledPredicatePart2()->labelExpression())));
  }

  if (auto *source_destination_predicate = predicate->sourceDestinationPredicate()) {
    auto left = GQLExpr::identifier(getElementVariableName(source_destination_predicate->nodeReference()->elementVariableReference()));

    if (auto *source = source_destination_predicate->sourcePredicatePart2()) {
      const String op = source->NOT() ? "IS NOT SOURCE OF" : "IS SOURCE OF";
      auto right = GQLExpr::identifier(getElementVariableName(source->edgeReference()->elementVariableReference()));
      return GQLExpr::binaryOp(op, left, right);
    }

    auto *destination = source_destination_predicate->destinationPredicatePart2();
    const String op = destination->NOT() ? "IS NOT DESTINATION OF" : "IS DESTINATION OF";
    auto right = GQLExpr::identifier(getElementVariableName(destination->edgeReference()->elementVariableReference()));
    return GQLExpr::binaryOp(op, left, right);
  }

  if (auto *all_different_predicate = predicate->all_differentPredicate()) {
    PtrList arguments;

    for (auto *reference : all_different_predicate->elementVariableReference())
      arguments.push_back(GQLExpr::identifier(getElementVariableName(reference)));

    return GQLExpr::functionCall("ALL_DIFFERENT", std::move(arguments));
  }

  if (auto *same_predicate = predicate->samePredicate()) {
    PtrList arguments;

    for (auto *reference : same_predicate->elementVariableReference())
      arguments.push_back(GQLExpr::identifier(getElementVariableName(reference)));

    return GQLExpr::functionCall("SAME", std::move(arguments));
  }

  if (auto *property_exists_predicate = predicate->property_existsPredicate()) {
    PtrList arguments;
    arguments.push_back(GQLExpr::identifier(getElementVariableName(property_exists_predicate->elementVariableReference())));
    arguments.push_back(GQLExpr::literal(getText(property_exists_predicate->propertyName()->identifier())));
    return GQLExpr::functionCall("PROPERTY_EXISTS", std::move(arguments));
  }

  if (auto *exists_predicate = predicate->existsPredicate()) {
    Ptr operand;

    if (exists_predicate->graphPattern()) {
      auto binding_table = castAny<PatternBindingTable>(visit(exists_predicate->graphPattern()));
      operand = makeGraphPatternBlock(std::move(binding_table), exists_predicate->LEFT_PAREN() != nullptr);
    } else if (exists_predicate->matchStatementBlock()) {
      auto block = make_intrusive<GQLMatchStatementBlock>();
      block->parenthesized = exists_predicate->LEFT_PAREN() != nullptr;
      populateMatchStatementBlock(*block, *this, exists_predicate->matchStatementBlock());
      operand = Ptr(block);
    } else if (exists_predicate->nestedQuerySpecification()) {
      operand = castAny<Ptr>(visit(exists_predicate->nestedQuerySpecification()));
    }

    return GQLExpr::unaryOp("EXISTS ", std::move(operand));
  }

  throwUnsupported("predicate", context);
}

std::any GQLParseTreeVisitor::visitNormalizedPredicateExprAlt(GQLParser::NormalizedPredicateExprAltContext *context) {
  auto operand = castAny<Ptr>(visit(context->valueExpression()));
  auto *part2 = context->normalizedPredicatePart2();
  bool negated = part2->NOT() != nullptr;
  auto form = GQLExpr::NormalForm::None;

  if (auto *nf = part2->normalForm()) {
    if (nf->NFC())
      form = GQLExpr::NormalForm::NFC;
    else if (nf->NFD())
      form = GQLExpr::NormalForm::NFD;
    else if (nf->NFKC())
      form = GQLExpr::NormalForm::NFKC;
    else if (nf->NFKD())
      form = GQLExpr::NormalForm::NFKD;
  }

  return GQLExpr::normalizedPredicate(std::move(operand), negated, form);
}

std::any GQLParseTreeVisitor::visitNumericValueExpression(GQLParser::NumericValueExpressionContext *context) {
  if (context->valueExpressionPrimary()) return visit(context->valueExpressionPrimary());

  if (context->numericValueFunction()) return visit(context->numericValueFunction());

  if (context->sign) {
    auto operand = castAny<Ptr>(visit(context->numericValueExpression(0)));
    if (context->sign->getType() == GQLParser::MINUS_SIGN) return std::any(GQLExpr::unaryOp("-", std::move(operand)));
    return std::any(std::move(operand));
  }

  if (context->operator_) {
    auto left = castAny<Ptr>(visit(context->numericValueExpression(0)));
    auto right = castAny<Ptr>(visit(context->numericValueExpression(1)));
    return std::any(GQLExpr::binaryOp(context->operator_->getText(), std::move(left), std::move(right)));
  }

  throwUnsupported("numeric value expression", context);
}

std::any GQLParseTreeVisitor::visitPropertyGraphExprAlt(GQLParser::PropertyGraphExprAltContext *context) {
  bool property = context->PROPERTY() != nullptr;
  auto graph = makeGraphExpression(context->graphExpression(), *this);
  return GQLExpr::graphExpression(std::move(graph), property);
}

std::any GQLParseTreeVisitor::visitBindingTableExprAlt(GQLParser::BindingTableExprAltContext *context) {
  bool binding = context->BINDING() != nullptr;
  auto table = makeBindingTableExpression(context->bindingTableExpression(), *this);
  return GQLExpr::bindingTableExpression(std::move(table), binding);
}

std::any GQLParseTreeVisitor::visitParenthesizedValueExpression(GQLParser::ParenthesizedValueExpressionContext *context) {
  return visit(context->valueExpression());
}

std::any GQLParseTreeVisitor::visitNonParenthesizedValueExpressionPrimary(
    GQLParser::NonParenthesizedValueExpressionPrimaryContext *context) {
  return std::any(makeNpvepExpr(context, *this));
}

std::any GQLParseTreeVisitor::visitNonParenthesizedValueExpressionPrimarySpecialCase(
    GQLParser::NonParenthesizedValueExpressionPrimarySpecialCaseContext *context) {
  return std::any(makeNpvepSpecialCaseExpr(context, *this));
}

std::any GQLParseTreeVisitor::visitObjectExpressionPrimary(GQLParser::ObjectExpressionPrimaryContext *context) {
  return std::any(makeObjectExpressionPrimary(context, *this));
}

std::any GQLParseTreeVisitor::visitCharacterStringValueExpression(GQLParser::CharacterStringValueExpressionContext *context) {
  return visit(context->valueExpression());
}

std::any GQLParseTreeVisitor::visitByteStringValueExpression(GQLParser::ByteStringValueExpressionContext *context) {
  return visit(context->valueExpression());
}

std::any GQLParseTreeVisitor::visitPathValueExpression(GQLParser::PathValueExpressionContext *context) {
  return visit(context->valueExpression());
}

std::any GQLParseTreeVisitor::visitListValueExpression(GQLParser::ListValueExpressionContext *context) {
  return visit(context->valueExpression());
}

std::any GQLParseTreeVisitor::visitDatetimeValueExpression(GQLParser::DatetimeValueExpressionContext *context) {
  return visit(context->valueExpression());
}

std::any GQLParseTreeVisitor::visitDurationValueExpression(GQLParser::DurationValueExpressionContext *context) {
  return visit(context->valueExpression());
}

std::any GQLParseTreeVisitor::visitNodeReferenceValueExpression(GQLParser::NodeReferenceValueExpressionContext *context) {
  return visit(context->valueExpressionPrimary());
}

std::any GQLParseTreeVisitor::visitEdgeReferenceValueExpression(GQLParser::EdgeReferenceValueExpressionContext *context) {
  return visit(context->valueExpressionPrimary());
}

std::any GQLParseTreeVisitor::visitValueQueryExpression(GQLParser::ValueQueryExpressionContext *context) {
  return std::any(makeValueQueryExpr(context, *this));
}

std::any GQLParseTreeVisitor::visitLetValueExpression(GQLParser::LetValueExpressionContext *context) {
  return std::any(makeLetValueExpr(context, *this));
}

std::any GQLParseTreeVisitor::visitPathValueConstructor(GQLParser::PathValueConstructorContext *context) {
  return std::any(makePathValueConstructorExpr(context, *this));
}

std::any GQLParseTreeVisitor::visitListValueConstructorByEnumeration(GQLParser::ListValueConstructorByEnumerationContext *context) {
  return std::any(makeListConstructor(context, *this));
}

std::any GQLParseTreeVisitor::visitRecordConstructor(GQLParser::RecordConstructorContext *context) {
  return std::any(makeRecordConstructor(context, *this));
}

std::any GQLParseTreeVisitor::visitListLiteral(GQLParser::ListLiteralContext *context) {
  return visit(context->listValueConstructorByEnumeration());
}

std::any GQLParseTreeVisitor::visitRecordLiteral(GQLParser::RecordLiteralContext *context) { return visit(context->recordConstructor()); }

std::any GQLParseTreeVisitor::visitAggregateFunction(GQLParser::AggregateFunctionContext *context) {
  return std::any(makeAggregateFunction(context, *this));
}

std::any GQLParseTreeVisitor::visitValueFunction(GQLParser::ValueFunctionContext *context) {
  return std::any(makeValueFunction(context, *this));
}

std::any GQLParseTreeVisitor::visitNumericValueFunction(GQLParser::NumericValueFunctionContext *context) {
  return std::any(makeNumericValueFunction(context, *this));
}

std::any GQLParseTreeVisitor::visitCharacterOrByteStringFunction(GQLParser::CharacterOrByteStringFunctionContext *context) {
  return std::any(makeCharacterOrByteStringFunction(context, *this));
}

std::any GQLParseTreeVisitor::visitDatetimeValueFunction(GQLParser::DatetimeValueFunctionContext *context) {
  return std::any(makeDatetimeValueFunction(context, *this));
}

std::any GQLParseTreeVisitor::visitDurationValueFunction(GQLParser::DurationValueFunctionContext *context) {
  return std::any(makeDurationValueFunction(context, *this));
}

std::any GQLParseTreeVisitor::visitListValueFunction(GQLParser::ListValueFunctionContext *context) {
  return std::any(makeListValueFunction(context, *this));
}

std::any GQLParseTreeVisitor::visitCaseExpression(GQLParser::CaseExpressionContext *context) {
  return std::any(makeCaseExpression(context, *this));
}

std::any GQLParseTreeVisitor::visitCastSpecification(GQLParser::CastSpecificationContext *context) {
  return std::any(makeCastSpecification(context, *this));
}

std::any GQLParseTreeVisitor::visitGeneralValueSpecification(GQLParser::GeneralValueSpecificationContext *context) {
  if (context->dynamicParameterSpecification()) return visit(context->dynamicParameterSpecification());
  if (context->SESSION_USER()) return GQLExpr::specialValue("SESSION_USER");
  throwUnsupported("general value specification", context);
}

std::any GQLParseTreeVisitor::visitDynamicParameterSpecification(GQLParser::DynamicParameterSpecificationContext *context) {
  return GQLExpr::dynamicParameter(getText(context));
}

std::any GQLParseTreeVisitor::visitGeneralLiteral(GQLParser::GeneralLiteralContext *context) {
  if (context->BOOLEAN_LITERAL()) return GQLExpr::specialValue(context->BOOLEAN_LITERAL()->getText());
  if (context->characterStringLiteral()) return GQLExpr::constant(getText(context));
  if (context->BYTE_STRING_LITERAL()) return GQLExpr::literal(getText(context));
  if (context->nullLiteral()) return visit(context->nullLiteral());
  if (context->temporalLiteral()) return visit(context->temporalLiteral());
  if (context->durationLiteral()) return visit(context->durationLiteral());
  if (context->listLiteral()) return visit(context->listLiteral());
  if (context->recordLiteral()) return visit(context->recordLiteral());
  throwUnsupported("general literal", context);
}

std::any GQLParseTreeVisitor::visitNullLiteral(GQLParser::NullLiteralContext *) { return GQLExpr::specialValue("NULL"); }

std::any GQLParseTreeVisitor::visitTemporalLiteral(GQLParser::TemporalLiteralContext *context) {
  if (context->dateLiteral()) return visit(context->dateLiteral());
  if (context->timeLiteral()) return visit(context->timeLiteral());
  if (context->datetimeLiteral()) return visit(context->datetimeLiteral());
  throwUnsupported("temporal literal", context);
}

std::any GQLParseTreeVisitor::visitDateLiteral(GQLParser::DateLiteralContext *context) {
  return GQLExpr::temporalLiteral("DATE", GQLExpr::literal(getText(context->dateString())));
}

std::any GQLParseTreeVisitor::visitTimeLiteral(GQLParser::TimeLiteralContext *context) {
  return GQLExpr::temporalLiteral("TIME", GQLExpr::literal(getText(context->timeString())));
}

std::any GQLParseTreeVisitor::visitDatetimeLiteral(GQLParser::DatetimeLiteralContext *context) {
  String keyword = context->TIMESTAMP() ? "TIMESTAMP" : "DATETIME";
  return GQLExpr::temporalLiteral(keyword, GQLExpr::literal(getText(context->datetimeString())));
}

std::any GQLParseTreeVisitor::visitDurationLiteral(GQLParser::DurationLiteralContext *context) {
  return GQLExpr::durationLiteral(GQLExpr::literal(getText(context->durationString())));
}

std::any GQLParseTreeVisitor::visitValueInitializer(GQLParser::ValueInitializerContext *context) {
  return visit(context->valueExpression());
}

std::any GQLParseTreeVisitor::visitBooleanValueExpression(GQLParser::BooleanValueExpressionContext *context) {
  return visit(context->valueExpression());
}

std::any GQLParseTreeVisitor::visitResultExpression(GQLParser::ResultExpressionContext *context) {
  return visit(context->valueExpression());
}

std::any GQLParseTreeVisitor::visitCardinalityExpressionArgument(GQLParser::CardinalityExpressionArgumentContext *context) {
  return visit(context->valueExpression());
}

std::any GQLParseTreeVisitor::visitTrimCharacterOrByteStringSource(GQLParser::TrimCharacterOrByteStringSourceContext *context) {
  return visit(context->valueExpression());
}

std::any GQLParseTreeVisitor::visitStringLength(GQLParser::StringLengthContext *context) {
  return visit(context->numericValueExpression());
}

std::any GQLParseTreeVisitor::visitIndependentValueExpression(GQLParser::IndependentValueExpressionContext *context) {
  return visit(context->numericValueExpression());
}

std::any GQLParseTreeVisitor::visitForItemSource(GQLParser::ForItemSourceContext *context) { return visit(context->valueExpression()); }

std::any GQLParseTreeVisitor::visitNumericValueExpressionDividend(GQLParser::NumericValueExpressionDividendContext *context) {
  return visit(context->numericValueExpression());
}

std::any GQLParseTreeVisitor::visitNumericValueExpressionDivisor(GQLParser::NumericValueExpressionDivisorContext *context) {
  return visit(context->numericValueExpression());
}

std::any GQLParseTreeVisitor::visitNumericValueExpressionBase(GQLParser::NumericValueExpressionBaseContext *context) {
  return visit(context->numericValueExpression());
}

std::any GQLParseTreeVisitor::visitNumericValueExpressionExponent(GQLParser::NumericValueExpressionExponentContext *context) {
  return visit(context->numericValueExpression());
}

std::any GQLParseTreeVisitor::visitGeneralLogarithmBase(GQLParser::GeneralLogarithmBaseContext *context) {
  return visit(context->numericValueExpression());
}

std::any GQLParseTreeVisitor::visitGeneralLogarithmArgument(GQLParser::GeneralLogarithmArgumentContext *context) {
  return visit(context->numericValueExpression());
}

std::any GQLParseTreeVisitor::visitValueExpressionPrimary(GQLParser::ValueExpressionPrimaryContext *context) {
  if (context->bindingVariableReference()) {
    return GQLExpr::identifier(getText(context->bindingVariableReference()->bindingVariable()));
  }

  if (context->aggregateFunction()) {
    return visit(context->aggregateFunction());
  }

  if (context->unsignedValueSpecification()) {
    return castAny<Ptr>(visit(context->unsignedValueSpecification()));
  }

  if (context->valueExpressionPrimary() && context->propertyName()) {
    return GQLExpr::property(castAny<Ptr>(visit(context->valueExpressionPrimary())), getText(context->propertyName()->identifier()));
  }

  if (context->parenthesizedValueExpression()) {
    return visit(context->parenthesizedValueExpression());
  }

  if (auto *element_id_function = context->element_idFunction()) {
    PtrList arguments;
    arguments.push_back(GQLExpr::identifier(getElementVariableName(element_id_function->elementVariableReference())));
    return GQLExpr::functionCall("ELEMENT_ID", std::move(arguments));
  }

  if (auto *case_expr = context->caseExpression()) {
    return visit(case_expr);
  }

  if (auto *cast_spec = context->castSpecification()) {
    return visit(cast_spec);
  }

  if (auto structured = tryMakeStructuredValuePrimary(context, *this)) return structured;

  throwUnsupported("value expression primary", context);
}

std::any GQLParseTreeVisitor::visitUnsignedValueSpecification(GQLParser::UnsignedValueSpecificationContext *context) {
  if (context->unsignedLiteral()) return visit(context->unsignedLiteral());
  if (context->generalValueSpecification()) return visit(context->generalValueSpecification());
  throwUnsupported("unsigned value specification", context);
}

std::any GQLParseTreeVisitor::visitUnsignedLiteral(GQLParser::UnsignedLiteralContext *context) {
  if (context->generalLiteral()) return visit(context->generalLiteral());
  return GQLExpr::constant(getText(context));
}

}  // namespace OPENGQL

}  // namespace DB
