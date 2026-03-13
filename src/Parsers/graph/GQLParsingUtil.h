#pragma once

#include <Parsers/IAST.h>

namespace DB
{

class GQLParsingUtil
{
public:
    struct Result
    {
        ASTPtr ast;
        String error_message;
        size_t error_pos = 0;
    };

    static Result parseMatchQuery(const String & gql_text);

    static Result parseGQLStatement(const String & gql_text);
};

}
