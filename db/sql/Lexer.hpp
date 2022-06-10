#pragma once

#include <string>
#include <vector>

namespace Db::Sql {

struct Token {
    enum class Type {
        KeywordSelect,
        KeywordFrom,
        KeywordTop,
        KeywordCreate,
        KeywordTable,
        KeywordAlias,
        KeywordOrder,
        KeywordBy,
        OrderByParam,
        Identifier,
        Arg,
        Asterisk,
        Comma,
        ParenOpen,
        ParenClose,
        SquaredParenOpen,
        SquaredParenClose,
        Semicolon,
        Eof,
        Garbage
    };

    Type type {};
    std::string value {};
};

class Lexer {
public:
    Lexer(std::istream& in)
        : m_in(in) { }

    std::vector<Token> lex();

private:
    std::istream& m_in;
};

}
