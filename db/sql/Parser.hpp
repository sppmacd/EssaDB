#pragma once

#include <db/core/AST.hpp>
#include <db/core/Function.hpp>

#include "Lexer.hpp"

namespace Db::Sql {

class Parser {
public:
    // NOTE: This stores a reference.
    explicit Parser(std::vector<Token> const& tokens)
        : m_tokens(std::move(tokens)) { }

    Core::DbErrorOr<std::unique_ptr<Core::AST::Statement>> parse_statement();

private:
    Core::DbErrorOr<std::unique_ptr<Core::AST::Select>> parse_select();
    Core::DbErrorOr<std::unique_ptr<Core::AST::CreateTable>> parse_create_table();
    Core::DbErrorOr<std::unique_ptr<Core::AST::Expression>> parse_expression(int min_precedence = 0);
    struct BetweenRange : public Core::AST::Expression {
        std::unique_ptr<Core::AST::Expression> min;
        std::unique_ptr<Core::AST::Expression> max;

        BetweenRange(std::unique_ptr<Core::AST::Expression> min, std::unique_ptr<Core::AST::Expression> max)
            : Expression(min->start())
            , min(std::move(min))
            , max(std::move(max)) { }

        virtual Core::DbErrorOr<Core::Value> evaluate(Core::AST::EvaluationContext&, Core::Row const&) const override { return Core::Value(); }
        virtual std::string to_string() const override { return "BetweenRange(min,max)"; }
    };
    Core::DbErrorOr<std::unique_ptr<Parser::BetweenRange>> parse_between_range();                                                              // (BETWEEN) x AND y
    Core::DbErrorOr<std::unique_ptr<Core::AST::Expression>> parse_operand(std::unique_ptr<Core::AST::Expression> lhs, int min_precedence = 0); // parses operator + rhs
    Core::DbErrorOr<std::unique_ptr<Core::AST::Function>> parse_function(std::string name);
    Core::DbErrorOr<std::unique_ptr<Core::AST::Identifier>> parse_identifier();

    std::vector<Token> const& m_tokens;
    size_t m_offset = 0;
};

}
