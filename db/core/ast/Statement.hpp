#pragma once

#include <db/core/Column.hpp>
#include <db/core/IndexedRelation.hpp>
#include <db/core/ValueOrResultSet.hpp>
#include <db/core/ast/ASTNode.hpp>
#include <db/core/ast/Expression.hpp>
#include <map>

namespace Db::Core {
class Database;
}

namespace Db::Core::AST {

struct ParsedColumn {
    Core::Column column;
    std::variant<std::monostate, Core::PrimaryKey, Core::ForeignKey> key;
};

class Expression;
class Check;

class Statement : public ASTNode {
public:
    explicit Statement(ssize_t start)
        : ASTNode(start) { }

    virtual ~Statement() = default;
    virtual DbErrorOr<ValueOrResultSet> execute(Database&) const = 0;
};

class StatementList : public ASTNode {
public:
    explicit StatementList(ssize_t start, std::vector<std::unique_ptr<Statement>> statements)
        : ASTNode(start)
        , m_statements(std::move(statements)) { }

    DbErrorOr<ValueOrResultSet> execute(Database&) const;

private:
    std::vector<std::unique_ptr<Statement>> m_statements;
};

class DeleteFrom : public Statement {
public:
    DeleteFrom(ssize_t start, std::string from, std::unique_ptr<Expression> where = {})
        : Statement(start)
        , m_from(std::move(from))
        , m_where(std::move(where)) { }

    virtual DbErrorOr<ValueOrResultSet> execute(Database&) const override;

private:
    std::string m_from;
    std::unique_ptr<Expression> m_where;
};

class Update : public Statement {
public:
    struct UpdatePair {
        std::string column;
        std::unique_ptr<Expression> expr;
    };

    Update(ssize_t start, std::string table, std::vector<UpdatePair> to_update)
        : Statement(start)
        , m_table(table)
        , m_to_update(std::move(to_update)) { }

    virtual DbErrorOr<ValueOrResultSet> execute(Database&) const override;

private:
    std::string m_table;
    std::vector<UpdatePair> m_to_update;
};

class Import : public Statement {
public:
    enum class Mode {
        Csv
    };

    Import(ssize_t start, Mode mode, std::string filename, std::string table)
        : Statement(start)
        , m_mode(mode)
        , m_filename(std::move(filename))
        , m_table(std::move(table)) { }

    virtual DbErrorOr<ValueOrResultSet> execute(Database&) const override;

private:
    Mode m_mode;
    std::string m_filename;
    std::string m_table;
};

class CreateTable : public Statement {
public:
    CreateTable(ssize_t start, std::string name, std::vector<ParsedColumn> columns, std::shared_ptr<AST::Check> check)
        : Statement(start)
        , m_name(std::move(name))
        , m_columns(std::move(columns))
        , m_check(std::move(check)) { }

    virtual DbErrorOr<ValueOrResultSet> execute(Database&) const override;

private:
    std::string m_name;
    std::vector<ParsedColumn> m_columns;
    std::shared_ptr<AST::Check> m_check;
    std::map<std::string, std::shared_ptr<AST::Expression>> m_check_constraints;
};

class DropTable : public Statement {
public:
    DropTable(ssize_t start, std::string name)
        : Statement(start)
        , m_name(std::move(name)) { }

    virtual DbErrorOr<ValueOrResultSet> execute(Database&) const override;

private:
    std::string m_name;
};

class TruncateTable : public Statement {
public:
    TruncateTable(ssize_t start, std::string name)
        : Statement(start)
        , m_name(std::move(name)) { }

    virtual DbErrorOr<ValueOrResultSet> execute(Database&) const override;

private:
    std::string m_name;
};

class AlterTable : public Statement {
public:
    AlterTable(ssize_t start, std::string name, std::vector<ParsedColumn> to_add, std::vector<ParsedColumn> to_alter, std::vector<std::string> to_drop,
        std::shared_ptr<Expression> check_to_add, std::shared_ptr<Expression> check_to_alter, bool check_to_drop,
        std::vector<std::pair<std::string, std::shared_ptr<Expression>>> constraint_to_add, std::vector<std::pair<std::string, std::shared_ptr<Expression>>> constraint_to_alter, std::vector<std::string> constraint_to_drop)
        : Statement(start)
        , m_name(std::move(name))
        , m_to_add(std::move(to_add))
        , m_to_alter(std::move(to_alter))
        , m_to_drop(std::move(to_drop))
        , m_check_to_add(std::move(check_to_add))
        , m_check_to_alter(std::move(check_to_alter))
        , m_check_to_drop(check_to_drop)
        , m_constraint_to_add(std::move(constraint_to_add))
        , m_constraint_to_alter(std::move(constraint_to_alter))
        , m_constraint_to_drop(std::move(constraint_to_drop)) { }

    virtual DbErrorOr<ValueOrResultSet> execute(Database&) const override;

private:
    std::string m_name;
    std::vector<ParsedColumn> m_to_add;
    std::vector<ParsedColumn> m_to_alter;
    std::vector<std::string> m_to_drop;
    std::shared_ptr<Expression> m_check_to_add;
    std::shared_ptr<Expression> m_check_to_alter;
    bool m_check_to_drop;
    std::vector<std::pair<std::string, std::shared_ptr<Expression>>> m_constraint_to_add;
    std::vector<std::pair<std::string, std::shared_ptr<Expression>>> m_constraint_to_alter;
    std::vector<std::string> m_constraint_to_drop;
};

}
