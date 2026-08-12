#include <Parsers/graph/AST/GQLExpr.h>

#include <Common/StringUtils.h>
#include <IO/ReadBufferFromString.h>
#include <IO/ReadHelpers.h>

#include <Poco/String.h>

namespace DB::OPENGQL::AST {
namespace {

GQLExpr::BinaryOperator classifyBinaryOperator(const String& raw_operator) {
  const String op = Poco::toUpper(raw_operator);

  if (op == "=") return GQLExpr::BinaryOperator::Equals;
  if (op == "<>" || op == "!=") return GQLExpr::BinaryOperator::NotEquals;
  if (op == ">") return GQLExpr::BinaryOperator::Greater;
  if (op == ">=") return GQLExpr::BinaryOperator::GreaterOrEquals;
  if (op == "<") return GQLExpr::BinaryOperator::Less;
  if (op == "<=") return GQLExpr::BinaryOperator::LessOrEquals;
  if (op == "+") return GQLExpr::BinaryOperator::Plus;
  if (op == "-") return GQLExpr::BinaryOperator::Minus;
  if (op == "*") return GQLExpr::BinaryOperator::Multiply;
  if (op == "/") return GQLExpr::BinaryOperator::Divide;
  if (op == "AND") return GQLExpr::BinaryOperator::And;
  if (op == "OR") return GQLExpr::BinaryOperator::Or;

  return GQLExpr::BinaryOperator::Unknown;
}

GQLExpr::SpecialValue classifySpecialValue(const String& raw_value) {
  const String value = Poco::toUpper(raw_value);

  if (value == "TRUE") return GQLExpr::SpecialValue::True;
  if (value == "FALSE") return GQLExpr::SpecialValue::False;
  if (value == "NULL") return GQLExpr::SpecialValue::Null;
  if (value == "SESSION_USER") return GQLExpr::SpecialValue::SessionUser;

  return GQLExpr::SpecialValue::Unknown;
}

}  // namespace

Ptr GQLExpr::constant(const String& raw) {
  String text = raw;
  trim(text);
  const String upper = Poco::toUpper(text);

  if (upper == "TRUE" || upper == "FALSE" || upper == "NULL") return specialValue(text);

  if (text.size() >= 2 && ((text.front() == '\'' && text.back() == '\'') || (text.front() == '"' && text.back() == '"'))) {
    ReadBufferFromString in(text);
    String value;
    if (text.starts_with('"'))
      readDoubleQuotedStringWithSQLStyle(value, in);
    else
      readQuotedStringWithSQLStyle(value, in);
    assertEOF(in);
    return literal(raw, Field(std::move(value)));
  }

  if (text.find_first_of(".eE") != String::npos) return literal(raw, Field(parseFromString<Float64>(text)));
  if (text.starts_with('-')) return literal(raw, Field(parseFromString<Int64>(text)));
  return literal(raw, Field(parseFromString<UInt64>(text)));
}

Ptr GQLExpr::binaryOp(const String& op, Ptr left, Ptr right) {
  auto expression = make_intrusive<GQLExpr>(Kind::BinaryOp, op);
  expression->binary_operator = classifyBinaryOperator(op);
  expression->children.push_back(std::move(left));
  expression->children.push_back(std::move(right));
  return Ptr(expression);
}

Ptr GQLExpr::specialValue(const String& text) {
  auto expression = make_intrusive<GQLExpr>(Kind::SpecialValue, text);
  expression->special_value = classifySpecialValue(text);
  return Ptr(expression);
}

}  // namespace DB::OPENGQL::AST
