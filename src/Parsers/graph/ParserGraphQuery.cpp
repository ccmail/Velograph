#include <Parsers/graph/ParserGraphQuery.h>
#include <Parsers/graph/GQLParsingUtil.h>
#include <Parsers/graph/ASTGraphQuery.h>
#include <Parsers/CommonParsers.h>
#include <Common/Exception.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int SYNTAX_ERROR;
}

bool ParserGraphQuery::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    auto saved_pos = pos;

    if (!ParserKeyword(Keyword::MATCH).ignore(pos, expected))
        return false;

    String gql_text = "MATCH ";
    auto current = pos;
    while (current.isValid())
    {
        gql_text += String(current->begin, current->end);
        gql_text += " ";
        ++current;
    }

    auto result = GQLParsingUtil::parseMatchQuery(gql_text);

    if (!result.ast)
    {
        pos = saved_pos;
        return false;
    }

    node = result.ast;

    while (pos.isValid())
        ++pos;

    return true;
}

}
