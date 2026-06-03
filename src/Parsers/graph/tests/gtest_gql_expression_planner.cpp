#include <gtest/gtest.h>

#include <Columns/ColumnConst.h>
#include <Common/Exception.h>
#include <Common/tests/gtest_global_context.h>
#include <Common/tests/gtest_global_register.h>
#include <Core/Field.h>
#include <DataTypes/DataTypeNullable.h>
#include <DataTypes/DataTypeNothing.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypeTuple.h>
#include <DataTypes/DataTypesNumber.h>
#include <Functions/IFunction.h>
#include <Interpreters/GQL/ExpressionPlanner.h>
#include <Parsers/graph/AST/GQLExpr.h>

#include <base/types.h>

using namespace DB;

namespace GAST = DB::OPENGQL::AST;

namespace DB::ErrorCodes
{
extern const int NOT_IMPLEMENTED;
}

namespace
{

ContextPtr getPlannerContext()
{
    tryRegisterFunctions();
    return getContext().context;
}

const GAST::GQLExpr & asGQLExpr(const ASTPtr & ast)
{
    return assert_cast<const GAST::GQLExpr &>(*ast);
}

const ActionsDAG::Node & buildNode(const ASTPtr & ast, ActionsDAG & dag)
{
    return GQL::buildExpressionNode(asGQLExpr(ast), dag, getPlannerContext());
}

void expectTypeName(const ActionsDAG::Node & node, const String & type_name)
{
    ASSERT_NE(node.result_type, nullptr);
    EXPECT_EQ(node.result_type->getName(), type_name);
}

Field getConstantField(const ActionsDAG::Node & node)
{
    EXPECT_NE(node.column, nullptr);
    EXPECT_TRUE(node.column->isConst());
    return assert_cast<const ColumnConst &>(*node.column).getField();
}

void expectFunctionName(const ActionsDAG::Node & node, const String & function_name)
{
    EXPECT_EQ(node.type, ActionsDAG::ActionType::FUNCTION);
    ASSERT_NE(node.function_base, nullptr);
    EXPECT_EQ(node.function_base->getName(), function_name);
}

ActionsDAG makeDagWithInputs(const NamesAndTypesList & inputs)
{
    return ActionsDAG(inputs);
}

}

TEST(GQLExpressionPlanner, IdentifierResolvesExistingUInt64InputColumn)
{
    auto dag = makeDagWithInputs({{"n", std::make_shared<DataTypeUInt64>()}});

    const auto & node = buildNode(GAST::GQLExpr::identifier("n"), dag);

    EXPECT_EQ(node.type, ActionsDAG::ActionType::INPUT);
    EXPECT_EQ(node.result_name, "n");
    expectTypeName(node, "UInt64");
}

TEST(GQLExpressionPlanner, PropertyAccessBuildsToTupleElementBeforeComparison)
{
    auto tuple_type = std::make_shared<DataTypeTuple>(
        DataTypes{std::make_shared<DataTypeUInt64>(), std::make_shared<DataTypeString>()},
        Strings{"age", "display_name"});
    auto dag = makeDagWithInputs({{"n", tuple_type}});

    const auto expression = GAST::GQLExpr::binaryOp(
        ">",
        GAST::GQLExpr::property(GAST::GQLExpr::identifier("n"), "age"),
        GAST::GQLExpr::literal("30"));

    const auto & node = buildNode(expression, dag);

    expectFunctionName(node, "greater");
    expectTypeName(node, "UInt8");
    ASSERT_EQ(node.children.size(), 2u);

    const auto & property_node = *node.children.front();
    expectFunctionName(property_node, "tupleElement");
    expectTypeName(property_node, "UInt64");
}

TEST(GQLExpressionPlanner, NotBuildsBooleanExpressionTree)
{
    auto dag = makeDagWithInputs({
        {"a", std::make_shared<DataTypeUInt8>()},
        {"b", std::make_shared<DataTypeUInt8>()},
    });

    const auto expression = GAST::GQLExpr::unaryOp(
        "NOT ",
        GAST::GQLExpr::binaryOp("AND", GAST::GQLExpr::identifier("a"), GAST::GQLExpr::identifier("b")));

    const auto & node = buildNode(expression, dag);

    expectFunctionName(node, "not");
    expectTypeName(node, "UInt8");
    ASSERT_EQ(node.children.size(), 1u);

    const auto & and_node = *node.children.front();
    expectFunctionName(and_node, "and");
    expectTypeName(and_node, "UInt8");
}

TEST(GQLExpressionPlanner, LiteralsBuildTypedConstantColumns)
{
    auto dag = makeDagWithInputs({});

    const auto & string_node = buildNode(GAST::GQLExpr::literal("\"foo\""), dag);
    expectTypeName(string_node, "String");
    EXPECT_EQ(getConstantField(string_node), Field(String("foo")));

    const auto & uint_node = buildNode(GAST::GQLExpr::literal("42"), dag);
    expectTypeName(uint_node, "UInt64");
    EXPECT_EQ(getConstantField(uint_node), Field(UInt64(42)));

    const auto & int_node = buildNode(GAST::GQLExpr::literal("-7"), dag);
    expectTypeName(int_node, "Int64");
    EXPECT_EQ(getConstantField(int_node), Field(Int64(-7)));

    const auto & float_node = buildNode(GAST::GQLExpr::literal("3.5"), dag);
    expectTypeName(float_node, "Float64");
    EXPECT_EQ(getConstantField(float_node), Field(Float64(3.5)));

    const auto & bool_node = buildNode(GAST::GQLExpr::literal("TRUE"), dag);
    expectTypeName(bool_node, "UInt8");
    EXPECT_EQ(getConstantField(bool_node), Field(UInt64(1)));

    const auto & null_node = buildNode(GAST::GQLExpr::literal("NULL"), dag);
    expectTypeName(null_node, "Nullable(Nothing)");
    EXPECT_EQ(getConstantField(null_node), Field(Null()));
}

TEST(GQLExpressionPlanner, SpecialValuesBuildTypedConstantColumns)
{
    auto dag = makeDagWithInputs({});

    const auto & true_node = buildNode(GAST::GQLExpr::specialValue("TRUE"), dag);
    expectTypeName(true_node, "UInt8");
    EXPECT_EQ(getConstantField(true_node), Field(UInt64(1)));

    const auto & false_node = buildNode(GAST::GQLExpr::specialValue("FALSE"), dag);
    expectTypeName(false_node, "UInt8");
    EXPECT_EQ(getConstantField(false_node), Field(UInt64(0)));

    const auto & null_node = buildNode(GAST::GQLExpr::specialValue("NULL"), dag);
    expectTypeName(null_node, "Nullable(Nothing)");
    EXPECT_EQ(getConstantField(null_node), Field(Null()));

    const auto & unknown_node = buildNode(GAST::GQLExpr::specialValue("UNKNOWN"), dag);
    expectTypeName(unknown_node, "Nullable(UInt8)");
    EXPECT_EQ(getConstantField(unknown_node), Field(Null()));
}

TEST(GQLExpressionPlanner, IsNullPredicateBuildsToFunction)
{
    auto dag = makeDagWithInputs({{"n", std::make_shared<DataTypeUInt64>()}});

    const auto expression = GAST::GQLExpr::binaryOp(
        "IS",
        GAST::GQLExpr::identifier("n"),
        GAST::GQLExpr::specialValue("NULL"));

    const auto & node = buildNode(expression, dag);

    expectFunctionName(node, "isNull");
    expectTypeName(node, "UInt8");
    ASSERT_EQ(node.children.size(), 1u);
}

TEST(GQLExpressionPlanner, UnsupportedKindThrowsNotImplemented)
{
    auto dag = makeDagWithInputs({});

    try
    {
        (void)buildNode(GAST::GQLExpr::functionCall("labels", {}), dag);
        FAIL() << "Expected unsupported GQL expression kind to throw";
    }
    catch (const Exception & e)
    {
        EXPECT_EQ(e.code(), ErrorCodes::NOT_IMPLEMENTED);
        EXPECT_NE(String(e.message()).find("FunctionCall"), String::npos);
    }
}
