#include <Parsers/graph/GQLParsingUtil.h>
#include "config.h"

#if USE_GQL_GRAMMAR

#include <Parsers/graph/ASTGraphQuery.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTLiteral.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTExpressionList.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#pragma clang diagnostic ignored "-Wdocumentation-html"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Winconsistent-missing-destructor-override"
#pragma clang diagnostic ignored "-Wshadow-field"
#pragma clang diagnostic ignored "-Wshadow-field-in-constructor"
#pragma clang diagnostic ignored "-Wsuggest-destructor-override"
#include <GQLLexer.h>
#include <GQLParser.h>
#include <GQLBaseVisitor.h>
#pragma clang diagnostic pop

#include <Common/Exception.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int SYNTAX_ERROR;
    extern const int NOT_IMPLEMENTED;
}

namespace
{

using namespace gql_grammar;

class GQLErrorListener : public antlr4::BaseErrorListener
{
public:
    bool has_error = false;
    String error_message;
    size_t error_pos = 0;

    void syntaxError(
        antlr4::Recognizer *,
        antlr4::Token * offending_symbol,
        size_t,
        size_t,
        const std::string & msg,
        std::exception_ptr) override
    {
        if (!has_error)
        {
            has_error = true;
            error_message = msg;
            if (offending_symbol)
                error_pos = offending_symbol->getStartIndex();
        }
    }
};


/// Builds ClickHouse Graph AST from ANTLR parse tree using standard Visitor pattern.
class GQLASTBuilder : public GQLBaseVisitor
{
public:
    static ASTPtr anyToAST(std::any & val)
    {
        if (!val.has_value())
            return nullptr;
        try
        {
            return std::any_cast<ASTPtr>(val);
        }
        catch (const std::bad_any_cast &)
        {
            return nullptr;
        }
    }

    // ==================== MATCH statement ====================

    std::any visitSimpleMatchStatement(GQLParser::SimpleMatchStatementContext * ctx) override
    {
        auto query = make_intrusive<ASTGraphQuery>();

        if (auto * binding_table = ctx->graphPatternBindingTable())
        {
            if (auto * gp = binding_table->graphPattern())
            {
                auto pattern_any = visitGraphPattern(gp);
                if (auto pattern = anyToAST(pattern_any))
                {
                    query->match_pattern = pattern->as<ASTGraphPattern>();
                    query->children.push_back(pattern);
                }
            }
        }

        return ASTPtr(query);
    }

    // ==================== Graph Pattern ====================

    std::any visitGraphPattern(GQLParser::GraphPatternContext * ctx) override
    {
        auto pattern = make_intrusive<ASTGraphPattern>();

        if (auto * path_list = ctx->pathPatternList())
        {
            for (auto * pp : path_list->pathPattern())
            {
                auto path_any = visitPathPattern(pp);
                if (auto path = anyToAST(path_any))
                    pattern->children.push_back(path);
            }
        }

        if (auto * where_ctx = ctx->graphPatternWhereClause())
        {
            if (auto * sc = where_ctx->searchCondition())
            {
                auto cond_any = visit(sc);
                if (auto cond = anyToAST(cond_any))
                    pattern_where_condition_ = cond;
            }
        }

        return ASTPtr(pattern);
    }

    ASTPtr getPatternWhereCondition() { return pattern_where_condition_; }

    // ==================== Path Pattern ====================

    std::any visitPathPattern(GQLParser::PathPatternContext * ctx) override
    {
        auto path = make_intrusive<ASTPathPattern>();

        if (auto * var_decl = ctx->pathVariableDeclaration())
        {
            if (auto * pv = var_decl->pathVariable())
                path->path_variable = pv->getText();
        }

        if (auto * prefix = ctx->pathPatternPrefix())
        {
            if (auto * mode_prefix = prefix->pathModePrefix())
            {
                if (auto * mode = mode_prefix->pathMode())
                    path->path_mode = parsePathMode(mode);
            }
            if (auto * search = prefix->pathSearchPrefix())
                parsePathSearch(search, path.get());
        }

        if (auto * ppe = ctx->pathPatternExpression())
        {
            collectPathElements(ppe, path.get());
        }

        return ASTPtr(path);
    }

    // ==================== Path Pattern Expression (alternatives) ====================

    std::any visitPpePathTerm(GQLParser::PpePathTermContext * ctx) override
    {
        if (auto * pt = ctx->pathTerm())
            return visitPathTerm(pt);
        return {};
    }

    std::any visitPathTerm(GQLParser::PathTermContext * ctx) override
    {
        auto list = make_intrusive<ASTExpressionList>();
        for (auto * pf : ctx->pathFactor())
        {
            auto elem_any = visit(pf);
            if (auto elem = anyToAST(elem_any))
                list->children.push_back(elem);
        }
        return ASTPtr(list);
    }

    // ==================== Path Factor (quantifiers) ====================

    std::any visitPfPathPrimary(GQLParser::PfPathPrimaryContext * ctx) override
    {
        if (auto * pp = ctx->pathPrimary())
            return visit(pp);
        return {};
    }

    std::any visitPfQuantifiedPathPrimary(GQLParser::PfQuantifiedPathPrimaryContext * ctx) override
    {
        auto elem_any = visit(ctx->pathPrimary());
        auto elem = anyToAST(elem_any);
        if (!elem)
            return {};

        if (auto * quant_ctx = ctx->graphPatternQuantifier())
        {
            auto quant = buildQuantifier(quant_ctx);
            if (auto * edge = elem->as<ASTEdgePattern>())
            {
                edge->quantifier = quant->as<ASTPathQuantifier>();
                edge->children.push_back(quant);
            }
            else if (auto * sub_path = elem->as<ASTPathPattern>())
            {
                sub_path->children.push_back(quant);
            }
        }

        return ASTPtr(elem);
    }

    std::any visitPfQuestionedPathPrimary(GQLParser::PfQuestionedPathPrimaryContext * ctx) override
    {
        auto elem_any = visit(ctx->pathPrimary());
        auto elem = anyToAST(elem_any);
        if (!elem)
            return {};

        auto quant = make_intrusive<ASTPathQuantifier>();
        quant->min_hops = 0;
        quant->max_hops = 1;

        if (auto * edge = elem->as<ASTEdgePattern>())
        {
            edge->quantifier = quant.get();
            edge->children.push_back(quant);
        }

        return ASTPtr(elem);
    }

    // ==================== Path Primary ====================

    std::any visitPpElementPattern(GQLParser::PpElementPatternContext * ctx) override
    {
        if (auto * ep = ctx->elementPattern())
            return visit(ep);
        return {};
    }

    std::any visitPpParenthesizedPathPatternExpression(GQLParser::PpParenthesizedPathPatternExpressionContext * ctx) override
    {
        if (auto * pppe = ctx->parenthesizedPathPatternExpression())
            return visitParenthesizedPathPatternExpression(pppe);
        return {};
    }

    std::any visitParenthesizedPathPatternExpression(GQLParser::ParenthesizedPathPatternExpressionContext * ctx) override
    {
        auto sub_path = make_intrusive<ASTPathPattern>();

        if (auto * sub_var = ctx->subpathVariableDeclaration())
        {
            if (auto * sv = sub_var->subpathVariable())
                sub_path->subpath_variable = sv->getText();
        }

        if (auto * mode_prefix = ctx->pathModePrefix())
        {
            if (auto * mode = mode_prefix->pathMode())
                sub_path->path_mode = parsePathMode(mode);
        }

        if (auto * ppe = ctx->pathPatternExpression())
            collectPathElements(ppe, sub_path.get());

        if (auto * where_ctx = ctx->parenthesizedPathPatternWhereClause())
        {
            if (auto * sc = where_ctx->searchCondition())
            {
                auto cond_any = visit(sc);
                if (auto cond = anyToAST(cond_any))
                {
                    sub_path->where_predicate = cond;
                    sub_path->children.push_back(cond);
                }
            }
        }

        return ASTPtr(sub_path);
    }

    // ==================== Element Pattern ====================

    std::any visitElementPattern(GQLParser::ElementPatternContext * ctx) override
    {
        if (auto * np = ctx->nodePattern())
            return visitNodePattern(np);
        if (auto * ep = ctx->edgePattern())
            return visitEdgePattern(ep);
        return {};
    }

    // ==================== Node Pattern ====================

    std::any visitNodePattern(GQLParser::NodePatternContext * ctx) override
    {
        auto node = make_intrusive<ASTNodePattern>();

        if (auto * filler = ctx->elementPatternFiller())
            fillElement(filler, node->variable, node.get());

        return ASTPtr(node);
    }

    // ==================== Edge Pattern ====================

    std::any visitEdgePattern(GQLParser::EdgePatternContext * ctx) override
    {
        auto edge = make_intrusive<ASTEdgePattern>();

        if (auto * full_edge = ctx->fullEdgePattern())
        {
            GQLParser::ElementPatternFillerContext * filler = nullptr;

            if (auto * right = full_edge->fullEdgePointingRight())
            {
                edge->direction = GraphEdgeDirection::RIGHT;
                filler = right->elementPatternFiller();
            }
            else if (auto * left = full_edge->fullEdgePointingLeft())
            {
                edge->direction = GraphEdgeDirection::LEFT;
                filler = left->elementPatternFiller();
            }
            else if (auto * undirected = full_edge->fullEdgeUndirected())
            {
                edge->direction = GraphEdgeDirection::UNDIRECTED;
                filler = undirected->elementPatternFiller();
            }
            else if (auto * any_dir = full_edge->fullEdgeAnyDirection())
            {
                edge->direction = GraphEdgeDirection::ANY;
                filler = any_dir->elementPatternFiller();
            }
            else if (auto * left_or_und = full_edge->fullEdgeLeftOrUndirected())
            {
                edge->direction = GraphEdgeDirection::LEFT_OR_UNDIRECTED;
                filler = left_or_und->elementPatternFiller();
            }
            else if (auto * und_or_right = full_edge->fullEdgeUndirectedOrRight())
            {
                edge->direction = GraphEdgeDirection::UNDIRECTED_OR_RIGHT;
                filler = und_or_right->elementPatternFiller();
            }
            else if (auto * left_or_right = full_edge->fullEdgeLeftOrRight())
            {
                edge->direction = GraphEdgeDirection::LEFT_OR_RIGHT;
                filler = left_or_right->elementPatternFiller();
            }

            if (filler)
                fillElement(filler, edge->variable, edge.get());
        }
        else if (auto * abbr = ctx->abbreviatedEdgePattern())
        {
            if (abbr->RIGHT_ARROW())
                edge->direction = GraphEdgeDirection::RIGHT;
            else if (abbr->LEFT_ARROW())
                edge->direction = GraphEdgeDirection::LEFT;
            else if (abbr->TILDE())
                edge->direction = GraphEdgeDirection::UNDIRECTED;
            else if (abbr->LEFT_MINUS_RIGHT())
                edge->direction = GraphEdgeDirection::ANY;
            else if (abbr->LEFT_ARROW_TILDE())
                edge->direction = GraphEdgeDirection::LEFT_OR_UNDIRECTED;
            else if (abbr->TILDE_RIGHT_ARROW())
                edge->direction = GraphEdgeDirection::UNDIRECTED_OR_RIGHT;
            else
                edge->direction = GraphEdgeDirection::ANY;
        }

        return ASTPtr(edge);
    }

    // ==================== Label Expression ====================

    std::any visitLabelExpressionName(GQLParser::LabelExpressionNameContext * ctx) override
    {
        auto label = make_intrusive<ASTLabelExpression>();
        label->op = GraphLabelOp::NAME;
        if (auto * ln = ctx->labelName())
            label->label_name = ln->getText();
        return ASTPtr(label);
    }

    std::any visitLabelExpressionWildcard(GQLParser::LabelExpressionWildcardContext *) override
    {
        auto label = make_intrusive<ASTLabelExpression>();
        label->op = GraphLabelOp::WILDCARD;
        return ASTPtr(label);
    }

    std::any visitLabelExpressionNegation(GQLParser::LabelExpressionNegationContext * ctx) override
    {
        auto label = make_intrusive<ASTLabelExpression>();
        label->op = GraphLabelOp::NEGATION;

        if (auto * inner = ctx->labelExpression())
        {
            auto inner_any = visit(inner);
            if (auto inner_ast = anyToAST(inner_any))
                label->children.push_back(inner_ast);
        }

        return ASTPtr(label);
    }

    std::any visitLabelExpressionConjunction(GQLParser::LabelExpressionConjunctionContext * ctx) override
    {
        auto label = make_intrusive<ASTLabelExpression>();
        label->op = GraphLabelOp::CONJUNCTION;

        auto exprs = ctx->labelExpression();
        for (auto * expr : exprs)
        {
            auto expr_any = visit(expr);
            if (auto expr_ast = anyToAST(expr_any))
                label->children.push_back(expr_ast);
        }

        return ASTPtr(label);
    }

    std::any visitLabelExpressionDisjunction(GQLParser::LabelExpressionDisjunctionContext * ctx) override
    {
        auto label = make_intrusive<ASTLabelExpression>();
        label->op = GraphLabelOp::DISJUNCTION;

        auto exprs = ctx->labelExpression();
        for (auto * expr : exprs)
        {
            auto expr_any = visit(expr);
            if (auto expr_ast = anyToAST(expr_any))
                label->children.push_back(expr_ast);
        }

        return ASTPtr(label);
    }

    std::any visitLabelExpressionParenthesized(GQLParser::LabelExpressionParenthesizedContext * ctx) override
    {
        if (auto * inner = ctx->labelExpression())
            return visit(inner);
        return {};
    }

    // ==================== Quantifier ====================

    std::any visitGraphPatternQuantifier(GQLParser::GraphPatternQuantifierContext * ctx) override
    {
        return ASTPtr(buildQuantifier(ctx));
    }

    // ==================== WHERE / Search Condition ====================

    std::any visitSearchCondition(GQLParser::SearchConditionContext * ctx) override
    {
        if (auto * bve = ctx->booleanValueExpression())
            return visit(bve);
        return {};
    }

    std::any visitBooleanValueExpression(GQLParser::BooleanValueExpressionContext * ctx) override
    {
        if (auto * ve = ctx->valueExpression())
            return visit(ve);
        return {};
    }

    // ==================== Value Expression alternatives ====================

    std::any visitPrimaryExprAlt(GQLParser::PrimaryExprAltContext * ctx) override
    {
        if (auto * vep = ctx->valueExpressionPrimary())
            return visit(vep);
        return {};
    }

    std::any visitComparisonExprAlt(GQLParser::ComparisonExprAltContext * ctx) override
    {
        auto exprs = ctx->valueExpression();
        if (exprs.size() < 2)
            return {};

        auto lhs_any = visit(exprs[0]);
        auto rhs_any = visit(exprs[1]);
        auto lhs = anyToAST(lhs_any);
        auto rhs = anyToAST(rhs_any);
        if (!lhs || !rhs)
            return {};

        String func_name = "equals";
        if (auto * op = ctx->compOp())
        {
            if (op->EQUALS_OPERATOR()) func_name = "equals";
            else if (op->NOT_EQUALS_OPERATOR()) func_name = "notEquals";
            else if (op->LEFT_ANGLE_BRACKET()) func_name = "less";
            else if (op->RIGHT_ANGLE_BRACKET()) func_name = "greater";
            else if (op->LESS_THAN_OR_EQUALS_OPERATOR()) func_name = "lessOrEquals";
            else if (op->GREATER_THAN_OR_EQUALS_OPERATOR()) func_name = "greaterOrEquals";
        }

        auto args = make_intrusive<ASTExpressionList>();
        args->children.push_back(lhs);
        args->children.push_back(rhs);

        auto func = make_intrusive<ASTFunction>();
        func->name = func_name;
        func->arguments = args;
        func->children.push_back(args);

        return ASTPtr(func);
    }

    std::any visitConjunctiveExprAlt(GQLParser::ConjunctiveExprAltContext * ctx) override
    {
        return visitBinaryOp(ctx->valueExpression(), "and");
    }

    std::any visitDisjunctiveExprAlt(GQLParser::DisjunctiveExprAltContext * ctx) override
    {
        String func_name = "or";
        if (ctx->XOR())
            func_name = "xor";
        return visitBinaryOp(ctx->valueExpression(), func_name);
    }

    std::any visitNotExprAlt(GQLParser::NotExprAltContext * ctx) override
    {
        auto operand_any = visit(ctx->valueExpression());
        auto operand = anyToAST(operand_any);
        if (!operand)
            return {};

        auto args = make_intrusive<ASTExpressionList>();
        args->children.push_back(operand);

        auto func = make_intrusive<ASTFunction>();
        func->name = "not";
        func->arguments = args;
        func->children.push_back(args);

        return ASTPtr(func);
    }

    std::any visitAddSubtractExprAlt(GQLParser::AddSubtractExprAltContext * ctx) override
    {
        String func_name = ctx->PLUS_SIGN() ? "plus" : "minus";
        return visitBinaryOp(ctx->valueExpression(), func_name);
    }

    std::any visitMultDivExprAlt(GQLParser::MultDivExprAltContext * ctx) override
    {
        String func_name = ctx->ASTERISK() ? "multiply" : "divide";
        return visitBinaryOp(ctx->valueExpression(), func_name);
    }

    std::any visitConcatenationExprAlt(GQLParser::ConcatenationExprAltContext * ctx) override
    {
        return visitBinaryOp(ctx->valueExpression(), "concat");
    }

    std::any visitSignedExprAlt(GQLParser::SignedExprAltContext * ctx) override
    {
        auto operand_any = visit(ctx->valueExpression());
        auto operand = anyToAST(operand_any);
        if (!operand)
            return {};

        if (ctx->MINUS_SIGN())
        {
            auto args = make_intrusive<ASTExpressionList>();
            args->children.push_back(operand);

            auto func = make_intrusive<ASTFunction>();
            func->name = "negate";
            func->arguments = args;
            func->children.push_back(args);

            return ASTPtr(func);
        }

        return ASTPtr(operand);
    }

    std::any visitIsNotExprAlt(GQLParser::IsNotExprAltContext * ctx) override
    {
        auto operand_any = visit(ctx->valueExpression());
        auto operand = anyToAST(operand_any);
        if (!operand)
            return {};

        bool is_true = true;
        if (auto * tv = ctx->truthValue())
        {
            String text = tv->getText();
            std::transform(text.begin(), text.end(), text.begin(), ::tolower);
            is_true = (text == "true");
        }

        String func_name = is_true ? "equals" : "notEquals";
        if (ctx->NOT())
            func_name = is_true ? "notEquals" : "equals";

        auto args = make_intrusive<ASTExpressionList>();
        args->children.push_back(operand);
        args->children.push_back(make_intrusive<ASTLiteral>(Field(static_cast<UInt64>(1))));

        auto func = make_intrusive<ASTFunction>();
        func->name = func_name;
        func->arguments = args;
        func->children.push_back(args);

        return ASTPtr(func);
    }

    // ==================== Value Expression Primary ====================

    std::any visitValueExpressionPrimary(GQLParser::ValueExpressionPrimaryContext * ctx) override
    {
        if (ctx->PERIOD() && ctx->propertyName())
        {
            auto obj_any = visit(ctx->valueExpressionPrimary());
            auto obj = anyToAST(obj_any);
            if (!obj)
                return {};

            String prop = ctx->propertyName()->getText();
            auto args = make_intrusive<ASTExpressionList>();
            args->children.push_back(obj);
            args->children.push_back(make_intrusive<ASTLiteral>(Field(prop)));

            auto func = make_intrusive<ASTFunction>();
            func->name = "tupleElement";
            func->arguments = args;
            func->children.push_back(args);

            return ASTPtr(func);
        }

        if (auto * pve = ctx->parenthesizedValueExpression())
            return visit(pve);
        if (auto * uvs = ctx->unsignedValueSpecification())
            return visit(uvs);
        if (auto * bvr = ctx->bindingVariableReference())
            return visitBindingVariableReference(bvr);
        if (auto * agg = ctx->aggregateFunction())
            return visit(agg);

        return visitChildren(ctx);
    }

    std::any visitParenthesizedValueExpression(GQLParser::ParenthesizedValueExpressionContext * ctx) override
    {
        if (auto * ve = ctx->valueExpression())
            return visit(ve);
        return {};
    }

    std::any visitBindingVariableReference(GQLParser::BindingVariableReferenceContext * ctx) override
    {
        String name = ctx->getText();
        return ASTPtr(make_intrusive<ASTIdentifier>(name));
    }

    std::any visitUnsignedValueSpecification(GQLParser::UnsignedValueSpecificationContext * ctx) override
    {
        if (auto * ul = ctx->unsignedLiteral())
            return visit(ul);
        if (auto * gvs = ctx->generalValueSpecification())
            return visit(gvs);
        return visitChildren(ctx);
    }

    std::any visitNonNegativeIntegerSpecification(GQLParser::NonNegativeIntegerSpecificationContext * ctx) override
    {
        if (auto * ul = ctx->unsignedInteger())
        {
            String text = ul->getText();
            UInt64 val = std::stoull(text);
            return ASTPtr(make_intrusive<ASTLiteral>(Field(val)));
        }
        return visitChildren(ctx);
    }

    std::any visitUnsignedLiteral(GQLParser::UnsignedLiteralContext * ctx) override
    {
        if (auto * num = ctx->unsignedNumericLiteral())
            return visit(num);
        if (auto * gen = ctx->generalLiteral())
            return visit(gen);
        return {};
    }

    std::any visitUnsignedNumericLiteral(GQLParser::UnsignedNumericLiteralContext * ctx) override
    {
        String text = ctx->getText();
        if (ctx->exactNumericLiteral())
        {
            if (text.find('.') != String::npos)
            {
                Float64 val = std::stod(text);
                return ASTPtr(make_intrusive<ASTLiteral>(Field(val)));
            }
            UInt64 val = std::stoull(text);
            return ASTPtr(make_intrusive<ASTLiteral>(Field(val)));
        }
        if (ctx->approximateNumericLiteral())
        {
            Float64 val = std::stod(text);
            return ASTPtr(make_intrusive<ASTLiteral>(Field(val)));
        }
        return {};
    }

    std::any visitGeneralLiteral(GQLParser::GeneralLiteralContext * ctx) override
    {
        if (ctx->BOOLEAN_LITERAL())
        {
            String text = ctx->BOOLEAN_LITERAL()->getText();
            std::transform(text.begin(), text.end(), text.begin(), ::tolower);
            UInt8 val = (text == "true") ? 1 : 0;
            return ASTPtr(make_intrusive<ASTLiteral>(Field(val)));
        }
        if (auto * csl = ctx->characterStringLiteral())
        {
            String text = csl->getText();
            if (text.size() >= 2)
                text = text.substr(1, text.size() - 2);
            return ASTPtr(make_intrusive<ASTLiteral>(Field(text)));
        }
        if (ctx->nullLiteral())
            return ASTPtr(make_intrusive<ASTLiteral>(Field()));
        return visitChildren(ctx);
    }

    // ==================== RETURN ====================

    std::any visitPrimitiveResultStatement(GQLParser::PrimitiveResultStatementContext * ctx) override
    {
        ASTPtr return_ast;
        if (auto * rs = ctx->returnStatement())
        {
            auto ret_any = visitReturnStatement(rs);
            return_ast = anyToAST(ret_any);
        }

        if (auto * obp = ctx->orderByAndPageStatement())
        {
            if (return_ast)
            {
                auto * ret_clause = return_ast->as<ASTGraphReturnClause>();
                if (ret_clause)
                    applyOrderByAndPage(obp, ret_clause);
            }
        }

        return return_ast ? return_ast : ASTPtr{};
    }

    std::any visitReturnStatement(GQLParser::ReturnStatementContext * ctx) override
    {
        auto ret = make_intrusive<ASTGraphReturnClause>();

        if (auto * body = ctx->returnStatementBody())
        {
            if (auto * sq = body->setQuantifier())
            {
                if (sq->DISTINCT())
                    ret->distinct = true;
            }

            if (body->ASTERISK())
            {
                auto item = make_intrusive<ASTGraphReturnItem>();
                item->alias = "*";
                ret->children.push_back(item);
            }
            else if (auto * ril = body->returnItemList())
            {
                for (auto * ri : ril->returnItem())
                {
                    auto item = make_intrusive<ASTGraphReturnItem>();

                    if (auto * ave = ri->aggregatingValueExpression())
                    {
                        auto expr_any = visit(ave);
                        if (auto expr = anyToAST(expr_any))
                            item->children.push_back(expr);
                    }

                    if (auto * alias_ctx = ri->returnItemAlias())
                    {
                        if (auto * id = alias_ctx->identifier())
                            item->alias = id->getText();
                    }

                    ret->children.push_back(item);
                }
            }

            if (auto * gb = body->groupByClause())
            {
                auto group_list = make_intrusive<ASTExpressionList>();
                if (auto * gel = gb->groupingElementList())
                {
                    for (auto * ge : gel->groupingElement())
                    {
                        if (auto * bvr = ge->bindingVariableReference())
                        {
                            auto id_any = visitBindingVariableReference(bvr);
                            if (auto id = anyToAST(id_any))
                                group_list->children.push_back(id);
                        }
                    }
                }
                if (!group_list->children.empty())
                {
                    ret->group_by = group_list;
                    ret->children.push_back(group_list);
                }
            }
        }

        return ASTPtr(ret);
    }

    std::any visitAggregatingValueExpression(GQLParser::AggregatingValueExpressionContext * ctx) override
    {
        if (auto * ve = ctx->valueExpression())
            return visit(ve);
        return {};
    }

    // ==================== ORDER BY / LIMIT / OFFSET ====================

    std::any visitOrderByAndPageStatement(GQLParser::OrderByAndPageStatementContext * ctx) override
    {
        return visitChildren(ctx);
    }

private:
    ASTPtr pattern_where_condition_;

    // ==================== Helpers ====================

    void fillElement(GQLParser::ElementPatternFillerContext * filler, String & variable, IAST * owner)
    {
        if (!filler)
            return;

        if (auto * var_decl = filler->elementVariableDeclaration())
        {
            if (auto * ev = var_decl->elementVariable())
            {
                if (auto * bv = ev->bindingVariable())
                    variable = bv->getText();
            }
        }

        if (auto * isle = filler->isLabelExpression())
        {
            if (auto * le = isle->labelExpression())
            {
                auto label_any = visit(le);
                if (auto label_ast = anyToAST(label_any))
                {
                    auto * label_expr = label_ast->as<ASTLabelExpression>();
                    if (label_expr)
                    {
                        if (auto * node = dynamic_cast<ASTNodePattern *>(owner))
                        {
                            if (label_expr->op == GraphLabelOp::NAME)
                                node->label = label_expr->label_name;
                            node->label_expression = label_expr;
                            node->children.push_back(label_ast);
                        }
                        else if (auto * edge = dynamic_cast<ASTEdgePattern *>(owner))
                        {
                            if (label_expr->op == GraphLabelOp::NAME)
                                edge->label = label_expr->label_name;
                            edge->label_expression = label_expr;
                            edge->children.push_back(label_ast);
                        }
                    }
                }
            }
        }

        if (auto * pred = filler->elementPatternPredicate())
        {
            if (auto * epwc = pred->elementPatternWhereClause())
            {
                if (auto * sc = epwc->searchCondition())
                {
                    auto cond_any = visit(sc);
                    if (auto cond = anyToAST(cond_any))
                    {
                        if (auto * node = dynamic_cast<ASTNodePattern *>(owner))
                        {
                            node->where_predicate = cond;
                            node->children.push_back(cond);
                        }
                        else if (auto * edge = dynamic_cast<ASTEdgePattern *>(owner))
                        {
                            edge->where_predicate = cond;
                            edge->children.push_back(cond);
                        }
                    }
                }
            }
        }
    }


    void collectPathElements(GQLParser::PathPatternExpressionContext * ppe, ASTPathPattern * path)
    {
        if (auto * term_ctx = dynamic_cast<GQLParser::PpePathTermContext *>(ppe))
        {
            if (auto * pt = term_ctx->pathTerm())
            {
                for (auto * pf : pt->pathFactor())
                {
                    auto elem_any = visit(pf);
                    if (auto elem = anyToAST(elem_any))
                        path->children.push_back(elem);
                }
            }
        }
        else if (auto * union_ctx = dynamic_cast<GQLParser::PpePatternUnionContext *>(ppe))
        {
            for (auto * child_pt : union_ctx->pathTerm())
            {
                for (auto * pf : child_pt->pathFactor())
                {
                    auto elem_any = visit(pf);
                    if (auto elem = anyToAST(elem_any))
                        path->children.push_back(elem);
                }
            }
        }
        else if (auto * multi_ctx = dynamic_cast<GQLParser::PpeMultisetAlternationContext *>(ppe))
        {
            for (auto * child_pt : multi_ctx->pathTerm())
            {
                for (auto * pf : child_pt->pathFactor())
                {
                    auto elem_any = visit(pf);
                    if (auto elem = anyToAST(elem_any))
                        path->children.push_back(elem);
                }
            }
        }
    }


    static GraphPathMode parsePathMode(GQLParser::PathModeContext * ctx)
    {
        if (!ctx) return GraphPathMode::DEFAULT;
        String text = ctx->getText();
        std::transform(text.begin(), text.end(), text.begin(), ::toupper);
        if (text == "WALK") return GraphPathMode::WALK;
        if (text == "TRAIL") return GraphPathMode::TRAIL;
        if (text == "SIMPLE") return GraphPathMode::SIMPLE;
        if (text == "ACYCLIC") return GraphPathMode::ACYCLIC;
        return GraphPathMode::DEFAULT;
    }


    static void parsePathSearch(GQLParser::PathSearchPrefixContext * ctx, ASTPathPattern * path)
    {
        if (auto * all = ctx->allPathSearch())
        {
            path->search_prefix = GraphPathSearch::ALL;
            if (auto * mode = all->pathMode())
                path->path_mode = parsePathMode(mode);
        }
        else if (auto * any = ctx->anyPathSearch())
        {
            path->search_prefix = GraphPathSearch::ANY;
            if (auto * nop = any->numberOfPaths())
                path->search_count = std::stoull(nop->getText());
            if (auto * mode = any->pathMode())
                path->path_mode = parsePathMode(mode);
        }
        else if (auto * shortest = ctx->shortestPathSearch())
        {
            if (auto * all_s = shortest->allShortestPathSearch())
            {
                path->search_prefix = GraphPathSearch::ALL_SHORTEST;
                if (auto * mode = all_s->pathMode())
                    path->path_mode = parsePathMode(mode);
            }
            else if (auto * any_s = shortest->anyShortestPathSearch())
            {
                path->search_prefix = GraphPathSearch::ANY_SHORTEST;
                if (auto * mode = any_s->pathMode())
                    path->path_mode = parsePathMode(mode);
            }
            else if (auto * counted = shortest->countedShortestPathSearch())
            {
                path->search_prefix = GraphPathSearch::COUNTED_SHORTEST;
                if (auto * nop = counted->numberOfPaths())
                    path->search_count = std::stoull(nop->getText());
                if (auto * mode = counted->pathMode())
                    path->path_mode = parsePathMode(mode);
            }
            else if (auto * group = shortest->countedShortestGroupSearch())
            {
                path->search_prefix = GraphPathSearch::COUNTED_SHORTEST_GROUP;
                if (auto * nog = group->numberOfGroups())
                    path->search_count = std::stoull(nog->getText());
                if (auto * mode = group->pathMode())
                    path->path_mode = parsePathMode(mode);
            }
        }
    }


    ASTPtr buildQuantifier(GQLParser::GraphPatternQuantifierContext * ctx)
    {
        auto quant = make_intrusive<ASTPathQuantifier>();

        if (ctx->ASTERISK())
        {
            quant->min_hops = 0;
            quant->max_hops = ASTPathQuantifier::UNLIMITED;
        }
        else if (ctx->PLUS_SIGN())
        {
            quant->min_hops = 1;
            quant->max_hops = ASTPathQuantifier::UNLIMITED;
        }
        else if (auto * fixed = ctx->fixedQuantifier())
        {
            UInt64 n = std::stoull(fixed->getText().substr(1, fixed->getText().size() - 2));
            quant->min_hops = n;
            quant->max_hops = n;
        }
        else if (auto * gen = ctx->generalQuantifier())
        {
            if (auto * lb = gen->lowerBound())
                quant->min_hops = std::stoull(lb->getText());
            else
                quant->min_hops = 0;

            if (auto * ub = gen->upperBound())
                quant->max_hops = std::stoull(ub->getText());
            else
                quant->max_hops = ASTPathQuantifier::UNLIMITED;
        }

        return quant;
    }


    std::any visitBinaryOp(const std::vector<GQLParser::ValueExpressionContext *> & exprs, const String & func_name)
    {
        if (exprs.size() < 2)
            return {};

        auto lhs_any = visit(exprs[0]);
        auto rhs_any = visit(exprs[1]);
        auto lhs = anyToAST(lhs_any);
        auto rhs = anyToAST(rhs_any);
        if (!lhs || !rhs)
            return {};

        auto args = make_intrusive<ASTExpressionList>();
        args->children.push_back(lhs);
        args->children.push_back(rhs);

        auto func = make_intrusive<ASTFunction>();
        func->name = func_name;
        func->arguments = args;
        func->children.push_back(args);

        return ASTPtr(func);
    }


    void applyOrderByAndPage(GQLParser::OrderByAndPageStatementContext * ctx, ASTGraphReturnClause * ret)
    {
        if (auto * obc = ctx->orderByClause())
        {
            auto order_list = make_intrusive<ASTExpressionList>();
            if (auto * ssl = obc->sortSpecificationList())
            {
                for (auto * ss : ssl->sortSpecification())
                {
                    if (auto * sk = ss->sortKey())
                    {
                        auto key_any = visit(sk->aggregatingValueExpression());
                        if (auto key = anyToAST(key_any))
                            order_list->children.push_back(key);
                    }
                }
            }
            if (!order_list->children.empty())
            {
                ret->order_by = order_list;
                ret->children.push_back(order_list);
            }
        }

        if (auto * oc = ctx->offsetClause())
        {
            if (auto * nnis = oc->nonNegativeIntegerSpecification())
            {
                auto val_any = visit(nnis);
                if (auto val = anyToAST(val_any))
                {
                    ret->offset = val;
                    ret->children.push_back(val);
                }
            }
        }

        if (auto * lc = ctx->limitClause())
        {
            if (auto * nnis = lc->nonNegativeIntegerSpecification())
            {
                auto val_any = visit(nnis);
                if (auto val = anyToAST(val_any))
                {
                    ret->limit = val;
                    ret->children.push_back(val);
                }
            }
        }
    }
};

}


GQLParsingUtil::Result GQLParsingUtil::parseMatchQuery(const String & gql_text)
{
    Result result;

    GQLErrorListener error_listener;
    antlr4::ANTLRInputStream input_stream(gql_text);

    GQLLexer lexer(&input_stream);
    lexer.removeErrorListeners();
    lexer.addErrorListener(&error_listener);

    antlr4::CommonTokenStream token_stream(&lexer);

    GQLParser parser(&token_stream);
    parser.removeErrorListeners();
    parser.addErrorListener(&error_listener);

    auto * tree = parser.simpleMatchStatement();

    if (error_listener.has_error)
    {
        result.error_message = error_listener.error_message;
        result.error_pos = error_listener.error_pos;
        return result;
    }

    GQLASTBuilder builder;
    auto query_any = builder.visit(tree);
    auto query_ast = GQLASTBuilder::anyToAST(query_any);

    if (!query_ast)
    {
        result.error_message = "Failed to build Graph AST from parse tree";
        return result;
    }

    if (auto where = builder.getPatternWhereCondition())
    {
        auto * graph_query = query_ast->as<ASTGraphQuery>();
        if (graph_query)
        {
            graph_query->where_condition = where;
            graph_query->children.push_back(where);
        }
    }

    result.ast = query_ast;
    return result;
}


GQLParsingUtil::Result GQLParsingUtil::parseGQLStatement(const String & gql_text)
{
    Result result;

    GQLErrorListener error_listener;
    antlr4::ANTLRInputStream input_stream(gql_text);

    GQLLexer lexer(&input_stream);
    lexer.removeErrorListeners();
    lexer.addErrorListener(&error_listener);

    antlr4::CommonTokenStream token_stream(&lexer);

    GQLParser parser(&token_stream);
    parser.removeErrorListeners();
    parser.addErrorListener(&error_listener);

    auto * tree = parser.gqlProgram();

    if (error_listener.has_error)
    {
        result.error_message = error_listener.error_message;
        result.error_pos = error_listener.error_pos;
        return result;
    }

    GQLASTBuilder builder;
    auto query_any = builder.visit(tree);
    result.ast = GQLASTBuilder::anyToAST(query_any);

    if (!result.ast)
        result.error_message = "Failed to build Graph AST from parse tree";

    return result;
}

}

#else

namespace DB
{

namespace ErrorCodes
{
    extern const int NOT_IMPLEMENTED;
}

GQLParsingUtil::Result GQLParsingUtil::parseMatchQuery(const String &)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL grammar support is disabled. Rebuild with ENABLE_GQL_GRAMMAR=ON");
}

GQLParsingUtil::Result GQLParsingUtil::parseGQLStatement(const String &)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "GQL grammar support is disabled. Rebuild with ENABLE_GQL_GRAMMAR=ON");
}

}

#endif
