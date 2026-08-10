#pragma once

#include <Core/Field.h>
#include <Parsers/ASTWithAlias.h>
#include <Parsers/graph/AST/Utils.h>

#include <optional>

namespace DB::OPENGQL::AST {

class GQLExpr final : public DB::ASTWithAlias {
 public:
  enum class SetQuantifier : UInt8 {
    None,
    Distinct,
    All,
  };

  enum class TrimSpec : UInt8 {
    None,
    Leading,
    Trailing,
    Both,
  };

  enum class TemporalQualifier : UInt8 {
    None,
    YearToMonth,
    DayToSecond,
  };

  enum class NormalForm : UInt8 {
    None,
    NFC,
    NFD,
    NFKC,
    NFKD,
  };

  enum class BinaryOperator : UInt8 {
    Unknown,
    Equals,
    NotEquals,
    Greater,
    GreaterOrEquals,
    Less,
    LessOrEquals,
    Plus,
    Minus,
    Multiply,
    Divide,
    And,
    Or,
  };

  enum class SpecialValue : UInt8 {
    Unknown,
    True,
    False,
    Null,
    SessionUser,
  };

  enum class Kind : UInt8 {
    Identifier,
    Literal,
    Property,
    UnaryOp,
    BinaryOp,
    FunctionCall,
    Cast,
    DurationBetween,
    TrimString,
    ExprList,
    ValueQuery,
    LetExpr,
    PathConstructor,
    DynamicParameter,
    SpecialValue,
    TemporalLiteral,
    DurationLiteral,
    VariableExpression,
    GraphExpression,
    BindingTableExpression,
  };

  explicit GQLExpr(Kind kind_, String text_ = {}) : kind(kind_), text(std::move(text_)) {}

  static Ptr identifier(const String& name) { return Ptr(make_intrusive<GQLExpr>(Kind::Identifier, name)); }

  static Ptr literal(const String& text) { return Ptr(make_intrusive<GQLExpr>(Kind::Literal, text)); }

  static Ptr literal(const String& text, Field value) {
    auto expression = make_intrusive<GQLExpr>(Kind::Literal, text);
    expression->literal_value = std::move(value);
    return Ptr(expression);
  }

  static Ptr constant(const String& text);

  static Ptr property(Ptr base, const String& property_name) {
    auto expression = make_intrusive<GQLExpr>(Kind::Property, property_name);
    expression->children.push_back(std::move(base));
    return Ptr(expression);
  }

  static Ptr unaryOp(const String& op, Ptr operand) {
    auto expression = make_intrusive<GQLExpr>(Kind::UnaryOp, op);
    expression->children.push_back(std::move(operand));
    return Ptr(expression);
  }

  static Ptr binaryOp(const String& op, Ptr left, Ptr right);

  static Ptr functionCall(const String& name, PtrList arguments, SetQuantifier set_quantifier_ = SetQuantifier::None) {
    auto expression = make_intrusive<GQLExpr>(Kind::FunctionCall, name);
    expression->set_quantifier = set_quantifier_;
    expression->children = std::move(arguments);
    return Ptr(expression);
  }

  static Ptr bareKeywordFunction(const String& name) {
    auto expression = make_intrusive<GQLExpr>(Kind::FunctionCall, name);
    expression->bare_keyword = true;
    return Ptr(expression);
  }

  static Ptr castExpr(Ptr operand, const String& target_type) {
    auto expression = make_intrusive<GQLExpr>(Kind::Cast, target_type);
    expression->children.push_back(std::move(operand));
    return Ptr(expression);
  }

  static Ptr castExpr(Ptr operand, Ptr target_type) {
    auto expression = make_intrusive<GQLExpr>(Kind::Cast);
    if (operand) expression->children.push_back(std::move(operand));
    if (target_type) expression->children.push_back(std::move(target_type));
    return Ptr(expression);
  }

  static Ptr trimString(Ptr source, TrimSpec spec, Ptr trim_char = nullptr) {
    auto expression = make_intrusive<GQLExpr>(Kind::TrimString);
    expression->trim_spec = spec;
    expression->children.push_back(std::move(source));
    if (trim_char) expression->children.push_back(std::move(trim_char));
    return Ptr(expression);
  }

  static Ptr durationBetween(Ptr left, Ptr right, TemporalQualifier qualifier = TemporalQualifier::None) {
    auto expression = make_intrusive<GQLExpr>(Kind::DurationBetween, "DURATION_BETWEEN");
    expression->temporal_qualifier = qualifier;
    expression->children.push_back(std::move(left));
    expression->children.push_back(std::move(right));
    return Ptr(expression);
  }

  static Ptr exprList(PtrList items) {
    auto expression = make_intrusive<GQLExpr>(Kind::ExprList);
    expression->children = std::move(items);
    return Ptr(expression);
  }

  static Ptr valueQuery(Ptr subquery) {
    auto expression = make_intrusive<GQLExpr>(Kind::ValueQuery);
    expression->children.push_back(std::move(subquery));
    return Ptr(expression);
  }

  static Ptr letExpr(PtrList bindings, Ptr body) {
    auto expression = make_intrusive<GQLExpr>(Kind::LetExpr);
    for (auto& b : bindings) expression->children.push_back(std::move(b));
    expression->children.push_back(std::move(body));
    return Ptr(expression);
  }

  static Ptr pathConstructor(PtrList elements) {
    auto expression = make_intrusive<GQLExpr>(Kind::PathConstructor);
    expression->children = std::move(elements);
    return Ptr(expression);
  }

  static Ptr normalizedPredicate(Ptr operand, bool negated, NormalForm form) {
    String op = negated ? "IS NOT" : "IS";
    String right_text;
    if (form != NormalForm::None) {
      static const char* form_names[] = {"", "NFC", "NFD", "NFKC", "NFKD"};
      right_text = form_names[static_cast<UInt8>(form)];
      right_text += " NORMALIZED";
    } else {
      right_text = "NORMALIZED";
    }
    return GQLExpr::binaryOp(op, std::move(operand), GQLExpr::literal(right_text));
  }

  static Ptr dynamicParameter(const String& text) { return Ptr(make_intrusive<GQLExpr>(Kind::DynamicParameter, text)); }

  static Ptr specialValue(const String& text);

  static Ptr temporalLiteral(const String& keyword, Ptr string_value) {
    auto expression = make_intrusive<GQLExpr>(Kind::TemporalLiteral, keyword);
    expression->children.push_back(std::move(string_value));
    return Ptr(expression);
  }

  static Ptr durationLiteral(Ptr string_value) {
    auto expression = make_intrusive<GQLExpr>(Kind::DurationLiteral, "DURATION");
    expression->children.push_back(std::move(string_value));
    return Ptr(expression);
  }

  static Ptr variableExpression(Ptr operand) {
    auto expression = make_intrusive<GQLExpr>(Kind::VariableExpression, "VARIABLE");
    expression->children.push_back(std::move(operand));
    return Ptr(expression);
  }

  static Ptr graphExpression(Ptr graph, bool property_prefix) {
    auto expression = make_intrusive<GQLExpr>(Kind::GraphExpression, property_prefix ? "PROPERTY GRAPH" : "GRAPH");
    expression->children.push_back(std::move(graph));
    return Ptr(expression);
  }

  static Ptr bindingTableExpression(Ptr table, bool binding_prefix) {
    auto expression = make_intrusive<GQLExpr>(Kind::BindingTableExpression, binding_prefix ? "BINDING TABLE" : "TABLE");
    expression->children.push_back(std::move(table));
    return Ptr(expression);
  }

  String getID(char delim) const override { return "GQLExpr" + (delim + text); }

  ASTPtr clone() const override {
    auto result = make_intrusive<GQLExpr>(*this);
    result->children.clear();

    for (const auto& child : children) {
      if (child) result->children.push_back(child->clone());
    }

    return result;
  }

  Kind kind;
  String text;
  std::optional<Field> literal_value;
  BinaryOperator binary_operator = BinaryOperator::Unknown;
  SpecialValue special_value = SpecialValue::Unknown;
  SetQuantifier set_quantifier = SetQuantifier::None;
  TrimSpec trim_spec = TrimSpec::None;
  TemporalQualifier temporal_qualifier = TemporalQualifier::None;
  bool bare_keyword = false;

 protected:
  void formatImplWithoutAlias(WriteBuffer& ostr, const FormatSettings& settings, FormatState& state,
                              FormatStateStacked frame) const override {
    switch (kind) {
      case Kind::Identifier:
      case Kind::Literal:
      case Kind::DynamicParameter:
      case Kind::SpecialValue:
        ostr << text;
        return;

      case Kind::Property: {
        if (!children.empty() && children.front()) children.front()->format(ostr, settings, state, frame);

        ostr << "." << text;
        return;
      }

      case Kind::UnaryOp: {
        ostr << text;

        if (!children.empty() && children.front()) children.front()->format(ostr, settings, state, frame);

        return;
      }

      case Kind::BinaryOp: {
        ostr << "(";

        if (!children.empty() && children[0]) children[0]->format(ostr, settings, state, frame);

        ostr << " " << text << " ";

        if (children.size() > 1 && children[1]) children[1]->format(ostr, settings, state, frame);

        ostr << ")";
        return;
      }

      case Kind::FunctionCall: {
        ostr << text;
        if (!bare_keyword) {
          ostr << "(";
          if (set_quantifier != SetQuantifier::None) {
            ostr << (set_quantifier == SetQuantifier::Distinct ? "DISTINCT" : "ALL");
            if (!children.empty()) ostr << " ";
          }
          detail::formatChildren(ostr, settings, state, frame, children, ", ");
          ostr << ")";
        }
        return;
      }

      case Kind::Cast: {
        ostr << "CAST(";
        if (!children.empty() && children.front()) children.front()->format(ostr, settings, state, frame);
        ostr << " AS ";
        if (children.size() > 1 && children[1])
          children[1]->format(ostr, settings, state, frame);
        else
          ostr << text;
        ostr << ")";
        return;
      }

      case Kind::TrimString: {
        ostr << "TRIM(";
        bool has_prefix = (trim_spec != TrimSpec::None) || children.size() > 1;
        if (has_prefix) {
          if (trim_spec == TrimSpec::Leading)
            ostr << "LEADING";
          else if (trim_spec == TrimSpec::Trailing)
            ostr << "TRAILING";
          else if (trim_spec == TrimSpec::Both)
            ostr << "BOTH";
          if (children.size() > 1 && children[1]) {
            if (trim_spec != TrimSpec::None) ostr << " ";
            children[1]->format(ostr, settings, state, frame);
          }
          ostr << " FROM ";
        }
        if (!children.empty() && children[0]) children[0]->format(ostr, settings, state, frame);
        ostr << ")";
        return;
      }

      case Kind::ExprList: {
        detail::formatChildren(ostr, settings, state, frame, children, ", ");
        return;
      }

      case Kind::ValueQuery: {
        ostr << "VALUE ";
        if (!children.empty() && children.front()) children.front()->format(ostr, settings, state, frame);
        return;
      }

      case Kind::LetExpr: {
        ostr << "LET ";
        for (size_t i = 0; i + 1 < children.size(); ++i) {
          if (i > 0) ostr << ", ";
          if (children[i]) children[i]->format(ostr, settings, state, frame);
        }
        ostr << " IN ";
        if (!children.empty() && children.back()) children.back()->format(ostr, settings, state, frame);
        ostr << " END";
        return;
      }

      case Kind::PathConstructor: {
        ostr << "PATH[";
        for (size_t i = 0; i < children.size(); ++i) {
          if (i > 0) ostr << ", ";
          if (children[i]) children[i]->format(ostr, settings, state, frame);
        }
        ostr << "]";
        return;
      }

      case Kind::DurationBetween: {
        ostr << "DURATION_BETWEEN(";
        if (!children.empty() && children[0]) children[0]->format(ostr, settings, state, frame);
        ostr << ", ";
        if (children.size() > 1 && children[1]) children[1]->format(ostr, settings, state, frame);
        ostr << ")";
        if (temporal_qualifier == TemporalQualifier::YearToMonth)
          ostr << " YEAR TO MONTH";
        else if (temporal_qualifier == TemporalQualifier::DayToSecond)
          ostr << " DAY TO SECOND";
        return;
      }

      case Kind::TemporalLiteral:
      case Kind::DurationLiteral:
      case Kind::VariableExpression:
      case Kind::GraphExpression:
      case Kind::BindingTableExpression: {
        ostr << text << " ";
        if (!children.empty() && children.front()) children.front()->format(ostr, settings, state, frame);
        return;
      }
    }
  }

  void appendColumnNameImpl(WriteBuffer& ostr) const override {
    IAST::FormatSettings settings(true);
    IAST::FormatState state;
    formatImplWithoutAlias(ostr, settings, state, {});
  }
};

}  // namespace DB::OPENGQL::AST
