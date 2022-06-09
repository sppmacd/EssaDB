#pragma once

#include <string>
#include <vector>

namespace Db::Sql {

struct Token {
    enum class Type {
        KeywordSelect,
        KeywordFrom,
        KeywordAfterSelect,
        Identifier,
        Arg,
        Asterisk,
        Comma,
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

    bool m_select_syntax = 0;
};

}
