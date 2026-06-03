#include <Interpreters/GQL/ExpressionPlanner.h>

#include <Common/Exception.h>
#include <Common/StringUtils.h>
#include <Core/Field.h>
#include <DataTypes/DataTypeNullable.h>
#include <DataTypes/DataTypeNothing.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypeFactory.h>
#include <DataTypes/DataTypesNumber.h>
#include <DataTypes/FieldToDataType.h>
#include <Functions/FunctionFactory.h>
#include <Interpreters/GQL/PlanScope.h>
#include <IO/ReadBufferFromString.h>
#include <IO/ReadHelpers.h>
#include <Parsers/ASTExpressionList.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTLiteral.h>
#include <Parsers/graph/AST/GQLCaseExpr.h>
#include <Parsers/graph/AST/GQLListConstructor.h>
#include <Parsers/graph/AST/GQLTypeExpression.h>

#include <Poco/String.h>

namespace DB::ErrorCodes
{
extern const int NOT_IMPLEMENTED;
}

namespace DB::GQL
{
namespace
{

using GQLExpr = OPENGQL::AST::GQLExpr;

String kindToString(GQLExpr::Kind kind)
{
    switch (kind)
    {
        case GQLExpr::Kind::Identifier:
            return "Identifier";
        case GQLExpr::Kind::Literal:
            return "Literal";
        case GQLExpr::Kind::Property:
            return "Property";
        case GQLExpr::Kind::UnaryOp:
            return "UnaryOp";
        case GQLExpr::Kind::BinaryOp:
            return "BinaryOp";
        case GQLExpr::Kind::FunctionCall:
            return "FunctionCall";
        case GQLExpr::Kind::Cast:
            return "Cast";
        case GQLExpr::Kind::DurationBetween:
            return "DurationBetween";
        case GQLExpr::Kind::TrimString:
            return "TrimString";
        case GQLExpr::Kind::ExprList:
            return "ExprList";
        case GQLExpr::Kind::ValueQuery:
            return "ValueQuery";
        case GQLExpr::Kind::LetExpr:
            return "LetExpr";
        case GQLExpr::Kind::PathConstructor:
            return "PathConstructor";
        case GQLExpr::Kind::DynamicParameter:
            return "DynamicParameter";
        case GQLExpr::Kind::SpecialValue:
            return "SpecialValue";
        case GQLExpr::Kind::TemporalLiteral:
            return "TemporalLiteral";
        case GQLExpr::Kind::DurationLiteral:
            return "DurationLiteral";
        case GQLExpr::Kind::VariableExpression:
            return "VariableExpression";
        case GQLExpr::Kind::GraphExpression:
            return "GraphExpression";
        case GQLExpr::Kind::BindingTableExpression:
            return "BindingTableExpression";
    }

    return "Unknown";
}

[[noreturn]] void throwUnsupportedKind(const GQLExpr & expr)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Unsupported GQL expression kind: {}", kindToString(expr.kind));
}

[[noreturn]] void throwUnsupportedShape(const GQLExpr & expr, const String & reason)
{
    throw Exception(
        ErrorCodes::NOT_IMPLEMENTED,
        "Unsupported GQL {} expression: {}",
        kindToString(expr.kind),
        reason);
}

const ActionsDAG::Node & buildGQLExpressionNodeImpl(const GQLExpr & expr, ActionsDAG & dag, ContextPtr context, const PlanScope * scope);
const ActionsDAG::Node & buildExpressionNodeImpl(const IAST & expr, ActionsDAG & dag, ContextPtr context, const PlanScope * scope);

String normalizedOperator(String op)
{
    trim(op);
    return Poco::toUpper(op);
}

const GQLExpr & getChildExpression(const GQLExpr & expr, size_t index)
{
    if (index >= expr.children.size() || !expr.children[index])
        throwUnsupportedShape(expr, fmt::format("missing child {}", index));

    const auto * child = expr.children[index]->as<GQLExpr>();
    if (!child)
        throwUnsupportedShape(expr, fmt::format("child {} is not a GQL expression", index));

    return *child;
}

const ActionsDAG::Node & addConstant(ActionsDAG & dag, DataTypePtr type, Field value, String name)
{
    ColumnWithTypeAndName column;
    column.type = std::move(type);
    column.name = std::move(name);
    column.column = column.type->createColumnConst(1, value);
    return dag.addColumn(std::move(column));
}

String parseStringLiteral(const String & text)
{
    ReadBufferFromString in(text);
    String value;

    if (text.starts_with('"'))
        readDoubleQuotedStringWithSQLStyle(value, in);
    else
        readQuotedStringWithSQLStyle(value, in);

    assertEOF(in);
    return value;
}

const ActionsDAG::Node & buildLiteralNode(const GQLExpr & expr, ActionsDAG & dag)
{
    String text = expr.text;
    trim(text);
    const String upper_text = Poco::toUpper(text);

    if (upper_text == "TRUE")
        return addConstant(dag, std::make_shared<DataTypeUInt8>(), UInt64(1), expr.text);

    if (upper_text == "FALSE")
        return addConstant(dag, std::make_shared<DataTypeUInt8>(), UInt64(0), expr.text);

    if (upper_text == "NULL")
        return addConstant(
            dag,
            std::make_shared<DataTypeNullable>(std::make_shared<DataTypeNothing>()),
            Null(),
            expr.text);

    if (text.size() >= 2 && ((text.front() == '\'' && text.back() == '\'') || (text.front() == '"' && text.back() == '"')))
        return addConstant(dag, std::make_shared<DataTypeString>(), parseStringLiteral(text), expr.text);

    try
    {
        if (text.find_first_of(".eE") != String::npos)
            return addConstant(dag, std::make_shared<DataTypeFloat64>(), parseFromString<Float64>(text), expr.text);

        if (text.starts_with('-'))
            return addConstant(dag, std::make_shared<DataTypeInt64>(), parseFromString<Int64>(text), expr.text);

        return addConstant(dag, std::make_shared<DataTypeUInt64>(), parseFromString<UInt64>(text), expr.text);
    }
    catch (...)
    {
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Unsupported GQL literal: {}", expr.text);
    }
}

const ActionsDAG::Node & buildSpecialValueNode(const GQLExpr & expr, ActionsDAG & dag)
{
    const String text = normalizedOperator(expr.text);

    if (text == "TRUE")
        return addConstant(dag, std::make_shared<DataTypeUInt8>(), UInt64(1), expr.text);

    if (text == "FALSE")
        return addConstant(dag, std::make_shared<DataTypeUInt8>(), UInt64(0), expr.text);

    if (text == "NULL")
        return addConstant(
            dag,
            std::make_shared<DataTypeNullable>(std::make_shared<DataTypeNothing>()),
            Null(),
            expr.text);

    if (text == "UNKNOWN")
        return addConstant(
            dag,
            std::make_shared<DataTypeNullable>(std::make_shared<DataTypeUInt8>()),
            Null(),
            expr.text);

    throwUnsupportedShape(expr, fmt::format("special value '{}' is not supported", expr.text));
}

const ActionsDAG::Node & addFunction(
    ActionsDAG & dag,
    ContextPtr context,
    const String & function_name,
    ActionsDAG::NodeRawConstPtrs children)
{
    auto function = FunctionFactory::instance().get(function_name, context);
    return dag.addFunction(function, std::move(children), {});
}

const ActionsDAG::Node & buildIdentifierNode(const String & name, ActionsDAG & dag, ContextPtr context, const PlanScope * scope)
{
    const auto * binding = scope ? scope->tryGetBinding(name) : nullptr;
    if (scope && !binding)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL identifier '{}' is not available in current plan scope", name);

    if (const auto * node = dag.tryFindInOutputs(name))
        return *node;

    if (binding && binding->expression)
    {
        const auto & value = buildExpressionNodeImpl(*binding->expression, dag, context, scope);
        return dag.addAlias(value, name);
    }

    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL identifier '{}' is not available in ActionsDAG outputs", name);
}

const ActionsDAG::Node & buildPropertyAccessNode(const GQLExpr & expr, ActionsDAG & dag, ContextPtr context, const PlanScope * scope)
{
    const auto & base = buildGQLExpressionNodeImpl(getChildExpression(expr, 0), dag, context, scope);
    const auto & property = addConstant(dag, std::make_shared<DataTypeString>(), expr.text, expr.text);

    /// A future refactor might prefer `getSubcolumn` for struct types.
    return addFunction(dag, context, "tupleElement", {&base, &property});
}

const char * getUnaryFunctionName(const String & op)
{
    if (op == "NOT")
        return "not";

    if (op == "-")
        return "negate";

    return nullptr;
}

const ActionsDAG::Node & buildUnaryOpNode(const GQLExpr & expr, ActionsDAG & dag, ContextPtr context, const PlanScope * scope)
{
    const auto * function_name = getUnaryFunctionName(normalizedOperator(expr.text));
    if (!function_name)
        throwUnsupportedShape(expr, fmt::format("operator '{}' is not supported", expr.text));

    const auto & operand = buildGQLExpressionNodeImpl(getChildExpression(expr, 0), dag, context, scope);
    return addFunction(dag, context, function_name, {&operand});
}

const char * getBinaryFunctionName(const String & op)
{
    if (op == "=")
        return "equals";
    if (op == "<>" || op == "!=")
        return "notEquals";
    if (op == ">")
        return "greater";
    if (op == ">=")
        return "greaterOrEquals";
    if (op == "<")
        return "less";
    if (op == "<=")
        return "lessOrEquals";
    if (op == "+")
        return "plus";
    if (op == "-")
        return "minus";
    if (op == "*")
        return "multiply";
    if (op == "/")
        return "divide";
    if (op == "AND")
        return "and";
    if (op == "OR")
        return "or";

    return nullptr;
}

const char * getScalarFunctionName(const String & name)
{
    if (name == "ABS")
        return "abs";
    if (name == "FLOOR")
        return "floor";
    if (name == "CEIL" || name == "CEILING")
        return "ceil";
    if (name == "SQRT")
        return "sqrt";
    if (name == "EXP")
        return "exp";
    if (name == "LN")
        return "log";
    if (name == "LOG10")
        return "log10";
    if (name == "SIN")
        return "sin";
    if (name == "COS")
        return "cos";
    if (name == "TAN")
        return "tan";
    if (name == "ASIN")
        return "asin";
    if (name == "ACOS")
        return "acos";
    if (name == "ATAN")
        return "atan";
    if (name == "UPPER")
        return "upper";
    if (name == "LOWER")
        return "lower";
    if (name == "CHAR_LENGTH" || name == "CHARACTER_LENGTH" || name == "BYTE_LENGTH" || name == "OCTET_LENGTH")
        return "length";

    return nullptr;
}

bool isNullSpecialValue(const GQLExpr & expr)
{
    return expr.kind == GQLExpr::Kind::SpecialValue && normalizedOperator(expr.text) == "NULL";
}

const ActionsDAG::Node & buildIsPredicateNode(const GQLExpr & expr, ActionsDAG & dag, ContextPtr context, bool negative, const PlanScope * scope)
{
    const auto & left_expr = getChildExpression(expr, 0);
    const auto & right_expr = getChildExpression(expr, 1);

    if (!isNullSpecialValue(right_expr))
        throwUnsupportedShape(expr, fmt::format("operator '{}' only supports NULL right operand", expr.text));

    const auto & left = buildGQLExpressionNodeImpl(left_expr, dag, context, scope);
    return addFunction(dag, context, negative ? "isNotNull" : "isNull", {&left});
}

const ActionsDAG::Node & buildBinaryOpNode(const GQLExpr & expr, ActionsDAG & dag, ContextPtr context, const PlanScope * scope)
{
    const String op = normalizedOperator(expr.text);
    if (op == "IS")
        return buildIsPredicateNode(expr, dag, context, false, scope);
    if (op == "IS NOT")
        return buildIsPredicateNode(expr, dag, context, true, scope);

    const auto * function_name = getBinaryFunctionName(op);
    if (!function_name)
        throwUnsupportedShape(expr, fmt::format("operator '{}' is not supported", expr.text));

    const auto & left = buildGQLExpressionNodeImpl(getChildExpression(expr, 0), dag, context, scope);
    const auto & right = buildGQLExpressionNodeImpl(getChildExpression(expr, 1), dag, context, scope);
    return addFunction(dag, context, function_name, {&left, &right});
}

const ActionsDAG::Node & buildFunctionCallNode(const GQLExpr & expr, ActionsDAG & dag, ContextPtr context, const PlanScope * scope)
{
    if (expr.bare_keyword)
        throwUnsupportedShape(expr, fmt::format("bare keyword function '{}' is not supported", expr.text));
    if (expr.set_quantifier != GQLExpr::SetQuantifier::None)
        throwUnsupportedShape(expr, fmt::format("set quantifier function '{}' is not supported", expr.text));

    const auto * function_name = getScalarFunctionName(normalizedOperator(expr.text));
    if (!function_name)
        throwUnsupportedShape(expr, fmt::format("function '{}' is not supported", expr.text));

    ActionsDAG::NodeRawConstPtrs arguments;
    arguments.reserve(expr.children.size());
    for (size_t i = 0; i < expr.children.size(); ++i)
        arguments.push_back(&buildGQLExpressionNodeImpl(getChildExpression(expr, i), dag, context, scope));

    return addFunction(dag, context, function_name, std::move(arguments));
}

String getDataTypeName(const OPENGQL::AST::GQLTypeExpression & type)
{
    if (type.kind != OPENGQL::AST::GQLTypeExpression::Kind::Name)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Only scalar named GQL types are supported for CAST");
    if (type.not_null)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL CAST target NOT NULL is not supported");
    if (type.prefix != OPENGQL::AST::GQLTypeExpression::Prefix::None)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL CAST target type prefix is not supported");

    const String name = normalizedOperator(type.name);
    if (name == "STRING")
        return "String";
    if (name == "BOOL" || name == "BOOLEAN")
        return "Bool";
    if (name == "INT" || name == "INTEGER" || name == "BIG INTEGER")
        return "Int64";
    if (name == "SMALL INTEGER")
        return "Int32";
    if (name == "INT8" || name == "INT16" || name == "INT32" || name == "INT64")
        return "Int" + name.substr(3);
    if (name == "UINT8" || name == "UINT16" || name == "UINT32" || name == "UINT64")
        return "UInt" + name.substr(4);
    if (name == "FLOAT" || name == "REAL")
        return "Float32";
    if (name == "FLOAT32")
        return "Float32";
    if (name == "FLOAT64" || name == "DOUBLE" || name == "DOUBLE PRECISION")
        return "Float64";

    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Unsupported GQL CAST target type: {}", type.name);
}

const ActionsDAG::Node & castNodeToGQLType(
    const ActionsDAG::Node & operand,
    const OPENGQL::AST::GQLTypeExpression & type,
    ActionsDAG & dag,
    ContextPtr context)
{
    const auto cast_type = DataTypeFactory::instance().get(getDataTypeName(type));
    return dag.addCast(operand, cast_type, {}, context);
}

const ActionsDAG::Node & buildCastNode(const GQLExpr & expr, ActionsDAG & dag, ContextPtr context, const PlanScope * scope)
{
    const auto & operand = buildGQLExpressionNodeImpl(getChildExpression(expr, 0), dag, context, scope);

    if (expr.children.size() < 2 || !expr.children[1])
        throwUnsupportedShape(expr, "missing target type");

    const auto * type = expr.children[1]->as<OPENGQL::AST::GQLTypeExpression>();
    if (!type)
        throwUnsupportedShape(expr, fmt::format("target type child is {}", expr.children[1]->getID(' ')));

    return castNodeToGQLType(operand, *type, dag, context);
}

const ActionsDAG::Node & buildCaseExprNode(const OPENGQL::AST::GQLCaseExpr & expr, ActionsDAG & dag, ContextPtr context, const PlanScope * scope)
{
    if (expr.when_operands.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL CASE without WHEN operands is not supported");
    if (expr.when_operands.size() != expr.then_results.size())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL CASE must have the same number of WHEN and THEN expressions");

    ActionsDAG::NodeRawConstPtrs arguments;
    arguments.reserve(expr.when_operands.size() * 2 + 1);

    const ActionsDAG::Node * operand = nullptr;
    if (expr.form == OPENGQL::AST::GQLCaseExpr::Form::Simple)
    {
        if (!expr.operand)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL simple CASE without operand is not supported");

        operand = &buildExpressionNodeImpl(*expr.operand, dag, context, scope);
    }

    for (size_t i = 0; i < expr.when_operands.size(); ++i)
    {
        if (!expr.when_operands[i] || !expr.then_results[i])
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL CASE WHEN and THEN expressions must be non-null");

        if (expr.form == OPENGQL::AST::GQLCaseExpr::Form::Simple)
        {
            const auto & when = buildExpressionNodeImpl(*expr.when_operands[i], dag, context, scope);
            arguments.push_back(&addFunction(dag, context, "equals", {operand, &when}));
        }
        else
        {
            arguments.push_back(&buildExpressionNodeImpl(*expr.when_operands[i], dag, context, scope));
        }

        arguments.push_back(&buildExpressionNodeImpl(*expr.then_results[i], dag, context, scope));
    }

    if (expr.else_result)
        arguments.push_back(&buildExpressionNodeImpl(*expr.else_result, dag, context, scope));
    else
        arguments.push_back(&addConstant(
            dag,
            std::make_shared<DataTypeNullable>(std::make_shared<DataTypeNothing>()),
            Null(),
            "NULL"));

    return addFunction(dag, context, "multiIf", std::move(arguments));
}

const ActionsDAG::Node & buildListConstructorNode(
    const OPENGQL::AST::GQLListConstructor & list,
    ActionsDAG & dag,
    ContextPtr context,
    const PlanScope * scope)
{
    ActionsDAG::NodeRawConstPtrs arguments;
    arguments.reserve(list.items.size());
    for (const auto & item : list.items)
    {
        if (!item)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL list constructor item must be non-null");

        arguments.push_back(&buildExpressionNodeImpl(*item, dag, context, scope));
    }

    return addFunction(dag, context, "array", std::move(arguments));
}

const ActionsDAG::Node & buildGQLExpressionNodeImpl(const GQLExpr & expr, ActionsDAG & dag, ContextPtr context, const PlanScope * scope)
{
    switch (expr.kind)
    {
        case GQLExpr::Kind::Identifier:
            return buildIdentifierNode(expr.text, dag, context, scope);
        case GQLExpr::Kind::Literal:
            return buildLiteralNode(expr, dag);
        case GQLExpr::Kind::Property:
            return buildPropertyAccessNode(expr, dag, context, scope);
        case GQLExpr::Kind::UnaryOp:
            return buildUnaryOpNode(expr, dag, context, scope);
        case GQLExpr::Kind::BinaryOp:
            return buildBinaryOpNode(expr, dag, context, scope);
        case GQLExpr::Kind::SpecialValue:
            return buildSpecialValueNode(expr, dag);
        case GQLExpr::Kind::FunctionCall:
            return buildFunctionCallNode(expr, dag, context, scope);
        case GQLExpr::Kind::Cast:
            return buildCastNode(expr, dag, context, scope);
        case GQLExpr::Kind::DurationBetween:
        case GQLExpr::Kind::TrimString:
        case GQLExpr::Kind::ExprList:
        case GQLExpr::Kind::ValueQuery:
        case GQLExpr::Kind::LetExpr:
        case GQLExpr::Kind::PathConstructor:
        case GQLExpr::Kind::DynamicParameter:
        case GQLExpr::Kind::TemporalLiteral:
        case GQLExpr::Kind::DurationLiteral:
        case GQLExpr::Kind::VariableExpression:
        case GQLExpr::Kind::GraphExpression:
        case GQLExpr::Kind::BindingTableExpression:
            throwUnsupportedKind(expr);
    }

    throwUnsupportedKind(expr);
}

const ActionsDAG::Node & buildASTLiteralNode(const ASTLiteral & literal, ActionsDAG & dag)
{
    const auto name = literal.getColumnName();
    return addConstant(dag, applyVisitor(FieldToDataType(), literal.value), literal.value, name);
}

const ActionsDAG::Node & buildASTFunctionNode(const ASTFunction & function, ActionsDAG & dag, ContextPtr context, const PlanScope * scope)
{
    if (function.parameters)
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Parametric ClickHouse function AST is not supported in GQL expression planning");
    if (function.isWindowFunction())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Window function AST is not supported in GQL expression planning");
    if (function.isLambdaFunction())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Lambda function AST is not supported in GQL expression planning");

    ActionsDAG::NodeRawConstPtrs arguments;
    if (function.arguments)
    {
        const auto * expression_list = function.arguments->as<ASTExpressionList>();
        if (!expression_list)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "ClickHouse function AST arguments must be ASTExpressionList");

        arguments.reserve(expression_list->children.size());
        for (const auto & argument : expression_list->children)
        {
            if (!argument)
                throw Exception(ErrorCodes::NOT_IMPLEMENTED, "ClickHouse function AST argument must be non-null");

            arguments.push_back(&buildExpressionNodeImpl(*argument, dag, context, scope));
        }
    }

    return addFunction(dag, context, function.name, std::move(arguments));
}

const ActionsDAG::Node & buildASTExpressionListNode(const ASTExpressionList & expression_list, ActionsDAG & dag, ContextPtr context, const PlanScope * scope)
{
    if (expression_list.children.empty())
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Empty ClickHouse expression list AST is not supported in GQL expression planning");

    ActionsDAG::NodeRawConstPtrs arguments;
    arguments.reserve(expression_list.children.size());
    for (const auto & item : expression_list.children)
    {
        if (!item)
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "ClickHouse expression list AST item must be non-null");

        arguments.push_back(&buildExpressionNodeImpl(*item, dag, context, scope));
    }

    return addFunction(dag, context, "tuple", std::move(arguments));
}

const ActionsDAG::Node & buildExpressionNodeImpl(const IAST & expr, ActionsDAG & dag, ContextPtr context, const PlanScope * scope)
{
    if (const auto * gql_expr = expr.as<GQLExpr>())
        return buildGQLExpressionNodeImpl(*gql_expr, dag, context, scope);

    if (const auto * identifier = expr.as<ASTIdentifier>())
        return buildIdentifierNode(identifier->name(), dag, context, scope);

    if (const auto * literal = expr.as<ASTLiteral>())
        return buildASTLiteralNode(*literal, dag);

    if (const auto * function = expr.as<ASTFunction>())
        return buildASTFunctionNode(*function, dag, context, scope);

    if (const auto * expression_list = expr.as<ASTExpressionList>())
        return buildASTExpressionListNode(*expression_list, dag, context, scope);

    if (const auto * case_expr = expr.as<OPENGQL::AST::GQLCaseExpr>())
        return buildCaseExprNode(*case_expr, dag, context, scope);

    if (const auto * list = expr.as<OPENGQL::AST::GQLListConstructor>())
        return buildListConstructorNode(*list, dag, context, scope);

    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Unsupported GQL expression AST: {}", expr.getID(' '));
}

}

const ActionsDAG::Node & buildExpressionNode(const IAST & expr, ActionsDAG & dag, ContextPtr context)
{
    return buildExpressionNodeImpl(expr, dag, context, nullptr);
}

const ActionsDAG::Node & buildExpressionNode(const GQLExpr & expr, ActionsDAG & dag, ContextPtr context)
{
    return buildGQLExpressionNodeImpl(expr, dag, context, nullptr);
}

const ActionsDAG::Node & buildExpressionNode(const IAST & expr, ActionsDAG & dag, ContextPtr context, const PlanScope & scope)
{
    return buildExpressionNodeImpl(expr, dag, context, &scope);
}

const ActionsDAG::Node & buildExpressionNode(const GQLExpr & expr, ActionsDAG & dag, ContextPtr context, const PlanScope & scope)
{
    return buildGQLExpressionNodeImpl(expr, dag, context, &scope);
}

const ActionsDAG::Node & buildExpressionNodeAsType(
    const IAST & expr,
    const OPENGQL::AST::GQLTypeExpression & type,
    ActionsDAG & dag,
    ContextPtr context,
    const PlanScope & scope)
{
    return castNodeToGQLType(buildExpressionNodeImpl(expr, dag, context, &scope), type, dag, context);
}

}
