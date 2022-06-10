#pragma once

#include <db/core/AST.hpp>
#include <db/core/Function.hpp>

#include "Lexer.hpp"

namespace Db::Sql {

class Parser {
public:
    Parser(std::vector<Token> tokens)
        : m_tokens(std::move(tokens)) { }

    Core::DbErrorOr<std::unique_ptr<Core::AST::Statement>> parse_statement();

private:
    Core::DbErrorOr<std::unique_ptr<Core::AST::Select>> parse_select();
    Core::DbErrorOr<std::unique_ptr<Core::AST::CreateTable>> parse_create_table();
    enum class AllowOperators {
        Yes,
        No
    };
    Core::DbErrorOr<std::unique_ptr<Core::AST::Expression>> parse_expression(AllowOperators);
    Core::DbErrorOr<std::unique_ptr<Core::AST::Expression>> parse_operand(std::unique_ptr<Core::AST::Expression> lhs); // parses operator + rhs
    Core::DbErrorOr<std::unique_ptr<Core::AST::Function>> parse_function(std::string name);
    Core::DbErrorOr<std::unique_ptr<Core::AST::Identifier>> parse_identifier();

    std::vector<Token> m_tokens;
    size_t m_offset = 0;
};

}
