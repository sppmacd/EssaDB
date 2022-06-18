#pragma once

#include "Tuple.hpp"
#include "SelectResult.hpp"
#include "Table.hpp"
#include "Value.hpp"
#include "db/core/Column.hpp"
#include "db/core/DbError.hpp"

#include <iostream>
#include <memory>
#include <optional>
#include <pthread.h>
#include <set>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <vector>
#include <map>

namespace Db::Core {
class Database;
}

namespace Db::Core::AST {

struct EvaluationContext {
    Table const& table;
};

class ASTNode {
public:
    explicit ASTNode(ssize_t start)
        : m_start(start) { }

    auto start() const { return m_start; }

private:
    size_t m_start;
};

class Expression : public ASTNode {
public:
    explicit Expression(ssize_t start)
        : ASTNode(start) { }

    virtual ~Expression() = default;
    virtual DbErrorOr<Value> evaluate(EvaluationContext&, Tuple const&) const = 0;
    virtual std::string to_string() const = 0;
};

class Literal : public Expression {
public:
    explicit Literal(ssize_t start, Value val)
        : Expression(start)
        , m_value(std::move(val)) { }

    virtual DbErrorOr<Value> evaluate(EvaluationContext&, Tuple const&) const override { return m_value; }
    virtual std::string to_string() const override { return m_value.to_string().release_value_but_fixme_should_propagate_errors(); }

private:
    Value m_value;
};

class Identifier : public Expression {
public:
    explicit Identifier(ssize_t start, std::string id)
        : Expression(start)
        , m_id(std::move(id)) { }

    virtual DbErrorOr<Value> evaluate(EvaluationContext&, Tuple const&) const override;
    virtual std::string to_string() const override { return m_id; }

private:
    std::string m_id;
};

class BinaryOperator : public Expression {
public:
    enum class Operation {
        Equal,        // =
        NotEqual,     // !=
        Greater,      // >
        GreaterEqual, // >=
        Less,         // <
        LessEqual,    // <=
        Like,
        And,
        Or,
        Not, // FIXME: This is not binary op

        Invalid
    };

    BinaryOperator(std::unique_ptr<Expression> lhs, Operation op, std::unique_ptr<Expression> rhs = nullptr)
        : Expression(lhs->start())
        , m_lhs(std::move(lhs))
        , m_operation(op)
        , m_rhs(std::move(rhs)) { }

    virtual DbErrorOr<Value> evaluate(EvaluationContext& context, Tuple const& row) const override {
        return Value::create_bool(TRY(is_true(context, row)));
    }
    virtual std::string to_string() const override { return "BinaryOperator(" + m_lhs->to_string() + "," + m_rhs->to_string() + ")"; }

private:
    DbErrorOr<bool> is_true(EvaluationContext&, Tuple const&) const;

    std::unique_ptr<Expression> m_lhs;
    Operation m_operation {};
    std::unique_ptr<Expression> m_rhs;
};

class ArithmeticOperator : public Expression {
public:
    enum class Operation {
        Add,
        Sub,
        Mul,
        Div,

        Invalid
    };

    ArithmeticOperator(std::unique_ptr<Expression> lhs, Operation op, std::unique_ptr<Expression> rhs = nullptr)
        : Expression(lhs->start())
        , m_lhs(std::move(lhs))
        , m_operation(op)
        , m_rhs(std::move(rhs)) { }

    virtual DbErrorOr<Value> evaluate(EvaluationContext& context, Tuple const& row) const override;

    virtual std::string to_string() const override { return "BinaryOperator(" + m_lhs->to_string() + "," + m_rhs->to_string() + ")"; }

private:
    DbErrorOr<bool> is_true(EvaluationContext&, Tuple const&) const;

    std::unique_ptr<Expression> m_lhs;
    Operation m_operation {};
    std::unique_ptr<Expression> m_rhs;
};

class BetweenExpression : public Expression {
public:
    BetweenExpression(std::unique_ptr<Expression> lhs, std::unique_ptr<Expression> min, std::unique_ptr<Expression> max)
        : Expression(lhs->start())
        , m_lhs(std::move(lhs))
        , m_min(std::move(min))
        , m_max(std::move(max)) {
        assert(m_lhs);
        assert(m_min);
        assert(m_max);
    }

    virtual DbErrorOr<Value> evaluate(EvaluationContext& context, Tuple const& row) const override;
    virtual std::string to_string() const override {
        return "BetweenExpression(" + m_lhs->to_string() + "," + m_min->to_string() + "," + m_max->to_string() + ")";
    }

private:
    std::unique_ptr<Expression> m_lhs;
    std::unique_ptr<Expression> m_min;
    std::unique_ptr<Expression> m_max;
};

class InExpression : public Expression {
public:
    InExpression(std::unique_ptr<Expression> lhs, std::vector<std::unique_ptr<Core::AST::Expression>> args)
        : Expression(lhs->start())
        , m_lhs(std::move(lhs))
        , m_args(std::move(args)) {
        assert(m_lhs);
    }

    virtual DbErrorOr<Value> evaluate(EvaluationContext& context, Tuple const& row) const override;
    virtual std::string to_string() const override {
        std::string result = "InExpression(";
        for(auto i = m_args.begin(); i < m_args.end(); i++){
            result += (*i)->to_string();
            if(i != m_args.end() - 1)
                result += ", ";
        }

        result += ")";

        return result;
    }

private:
    std::unique_ptr<Expression> m_lhs;
    std::vector<std::unique_ptr<Core::AST::Expression>> m_args;
};

class CaseExpression : public Expression{
public:
    struct CasePair{
        std::unique_ptr<Expression> expr;
        std::unique_ptr<Expression> value;
    };

    CaseExpression(std::vector<CasePair> cases, std::unique_ptr<Core::AST::Expression> else_value = {})
        : Expression(cases.front().expr->start())
        , m_cases(std::move(cases))
        , m_else_value(std::move(else_value)){}

    virtual DbErrorOr<Value> evaluate(EvaluationContext& context, Tuple const& row) const override;
    virtual std::string to_string() const override {
        std::string result = "CaseExpression: \n";
        for(const auto& c : m_cases){
            result += "if expression then " + c.value->to_string() + "\n";
        }

        if(m_else_value)
            result += "else " + m_else_value->to_string();

        return result;
    }

private:
    std::vector<CasePair> m_cases;

    std::unique_ptr<Core::AST::Expression> m_else_value;
};

class SelectColumns {
public:
    SelectColumns() = default;

    struct Column {
        std::unique_ptr<Expression> column;
        std::optional<std::string> alias = {};
    };

    explicit SelectColumns(std::vector<Column> columns)
        : m_columns(std::move(columns)) { }

    bool select_all() const { return m_columns.empty(); }
    std::vector<Column> const& columns() const { return m_columns; }
    std::vector<std::optional<std::string>> const& aliases() const { return m_aliases; }

private:
    std::vector<Column> m_columns {};
    std::vector<std::optional<std::string>> m_aliases {};
};

struct OrderBy {
    enum class Order {
        Ascending,
        Descending
    };

    struct OrderBySet {
        std::string name;
        Order order = Order::Ascending;
    };

    std::vector<OrderBySet> columns;
};

struct GroupBy{
    std::vector<std::string> columns;

    bool is_valid(std::string const& rhs)const{
        bool result = 0;
        for(const auto& lhs : columns){
            result |= (lhs == rhs);
        }

        return result;
    }
};

struct Top {
    enum class Unit {
        Val,
        Perc
    };
    Unit unit = Unit::Perc;
    unsigned value = 100;
};

class Statement : public ASTNode {
public:
    explicit Statement(ssize_t start)
        : ASTNode(start) { }

    virtual ~Statement() = default;
    virtual DbErrorOr<Value> execute(Database&) const = 0;
};

class Select : public Statement {
public:
    Select(ssize_t start, SelectColumns columns, std::string from, std::unique_ptr<Expression> where = {}, std::optional<OrderBy> order_by = {}, std::optional<Top> top = {}, std::optional<GroupBy> group_by = {}, std::unique_ptr<Expression> having = {}, bool distinct = false, std::optional<std::string> select_into = {})
        : Statement(start)
        , m_columns(std::move(columns))
        , m_from(std::move(from))
        , m_where(std::move(where))
        , m_order_by(std::move(order_by))
        , m_top(std::move(top))
        , m_group_by(std::move(group_by))
        , m_having(std::move(having))
        , m_distinct(distinct)
        , m_select_into(std::move(select_into)) { }

    virtual DbErrorOr<Value> execute(Database&) const override;

private:
    SelectColumns m_columns;
    std::string m_from;
    std::unique_ptr<Expression> m_where;
    std::optional<OrderBy> m_order_by;
    std::optional<Top> m_top;
    std::optional<GroupBy> m_group_by;
    std::unique_ptr<Expression> m_having;
    bool m_distinct;
    std::optional<std::string> m_select_into;
};

class Union : public Statement {
public:
    Union(std::unique_ptr<Select> lhs, std::unique_ptr<Select> rhs, bool distinct)
        : Statement(lhs->start())
        , m_lhs(std::move(lhs))
        , m_rhs(std::move(rhs))
        , m_distinct(distinct) {}

    virtual DbErrorOr<Value> execute(Database&) const override;

private:
    std::unique_ptr<Select> m_lhs;
    std::unique_ptr<Select> m_rhs;
    bool m_distinct;
};

class DeleteFrom : public Statement {
public:
    DeleteFrom(ssize_t start, std::string from, std::unique_ptr<Expression> where = {})
        : Statement(start)
        , m_from(std::move(from))
        , m_where(std::move(where)) { }

    virtual DbErrorOr<Value> execute(Database&) const override;

private:
    std::string m_from;
    std::unique_ptr<Expression> m_where;
};

class Update : public Statement{
public:
    struct UpdatePair{
        std::string column;
        std::unique_ptr<Expression> expr;
    };

    Update(ssize_t start, std::string table, std::vector<UpdatePair> to_update)
    : Statement(start)
    , m_table(table)
    , m_to_update(std::move(to_update)) { }

    virtual DbErrorOr<Value> execute(Database&) const override;

private:
    std::string m_table;
    std::vector<UpdatePair> m_to_update;
};

class CreateTable : public Statement {
public:
    CreateTable(ssize_t start, std::string name, std::vector<Column> columns)
        : Statement(start)
        , m_name(std::move(name))
        , m_columns(std::move(columns)) { }

    virtual DbErrorOr<Value> execute(Database&) const override;

private:
    std::string m_name;
    std::vector<Column> m_columns;
};

class DropTable : public Statement {
public:
    DropTable(ssize_t start, std::string name)
        : Statement(start)
        , m_name(std::move(name)){ }

    virtual DbErrorOr<Value> execute(Database&) const override;

private:
    std::string m_name;
};

class TruncateTable : public Statement {
public:
    TruncateTable(ssize_t start, std::string name)
        : Statement(start)
        , m_name(std::move(name)){ }

    virtual DbErrorOr<Value> execute(Database&) const override;

private:
    std::string m_name;
};

class AlterTable : public Statement {
public:
    AlterTable(ssize_t start, std::string name, std::vector<Column> to_add, std::vector<Column> to_alter, std::vector<Column> to_drop)
        : Statement(start)
        , m_name(std::move(name))
        , m_to_add(std::move(to_add))
        , m_to_alter(std::move(to_alter))
        , m_to_drop(std::move(to_drop)){}

    virtual DbErrorOr<Value> execute(Database&) const override;

private:
    std::string m_name;
    std::vector<Column> m_to_add;
    std::vector<Column> m_to_alter;
    std::vector<Column> m_to_drop;
};

class InsertInto : public Statement {
public:
    InsertInto(ssize_t start, std::string name, std::vector<std::string> columns, std::vector<std::unique_ptr<Core::AST::Expression>> values)
        : Statement(start)
        , m_name(std::move(name))
        , m_columns(std::move(columns))
        , m_values(std::move(values)) { }

    InsertInto(ssize_t start, std::string name, std::vector<std::string> columns, Core::DbErrorOr<std::unique_ptr<Core::AST::Select>> select)
    : Statement(start)
    , m_name(std::move(name))
    , m_columns(std::move(columns))
    , m_select(std::move(select)) {}

    virtual DbErrorOr<Value> execute(Database&) const override;

private:
    std::string m_name;
    std::vector<std::string> m_columns;
    std::vector<std::unique_ptr<Core::AST::Expression>> m_values;
    std::optional<Core::DbErrorOr<std::unique_ptr<Core::AST::Select>>> m_select;
};

}
