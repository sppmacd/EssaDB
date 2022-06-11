#include "AST.hpp"

#include "Database.hpp"
#include "db/core/DbError.hpp"
#include "db/core/Row.hpp"
#include "db/core/Value.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <unordered_set>

namespace Db::Core::AST {

DbErrorOr<Value> Identifier::evaluate(EvaluationContext& context, Row const& row) const {
    auto column = context.table.get_column(m_id);
    if (!column)
        return DbError { "No such column: " + m_id, start() };
    return row.value(column->second);
}

DbErrorOr<bool> BinaryOperator::is_true(EvaluationContext& context, Row const& row) const {
    // TODO: Implement proper comparison
    switch (m_operation) {
    case Operation::Equal:
        return TRY(TRY(m_lhs->evaluate(context, row)).to_string()) == TRY(TRY(m_rhs->evaluate(context, row)).to_string());
    case Operation::NotEqual:
        return TRY(TRY(m_lhs->evaluate(context, row)).to_string()) != TRY(TRY(m_rhs->evaluate(context, row)).to_string());
    case Operation::Greater:
        return TRY(TRY(m_lhs->evaluate(context, row)).to_string()) > TRY(TRY(m_rhs->evaluate(context, row)).to_string());
    case Operation::GreaterEqual:
        return TRY(TRY(m_lhs->evaluate(context, row)).to_string()) >= TRY(TRY(m_rhs->evaluate(context, row)).to_string());
    case Operation::Less:
        return TRY(TRY(m_lhs->evaluate(context, row)).to_string()) < TRY(TRY(m_rhs->evaluate(context, row)).to_string());
    case Operation::LessEqual:
        return TRY(TRY(m_lhs->evaluate(context, row)).to_string()) <= TRY(TRY(m_rhs->evaluate(context, row)).to_string());
    case Operation::And:
        return TRY(TRY(m_lhs->evaluate(context, row)).to_bool()) && TRY(TRY(m_rhs->evaluate(context, row)).to_bool());
    case Operation::Or:
        return TRY(TRY(m_lhs->evaluate(context, row)).to_bool()) || TRY(TRY(m_rhs->evaluate(context, row)).to_bool());
    case Operation::Not:
        return !TRY(TRY(m_lhs->evaluate(context, row)).to_bool());
    case Operation::Like: {
        std::string str = TRY(TRY(m_lhs->evaluate(context, row)).to_string());
        std::string to_compare = TRY(TRY(m_rhs->evaluate(context, row)).to_string());

        if (to_compare.front() == '*' && to_compare.back() == '*') {
            std::string comparison_substr = to_compare.substr(1, to_compare.size() - 2);

            if (str.size() - 1 < to_compare.size())
                return false;

            return str.find(comparison_substr) != std::string::npos;
        }
        else if (to_compare.front() == '*') {
            auto it1 = str.end(), it2 = to_compare.end();

            if (str.size() < to_compare.size())
                return false;

            while (it1 != str.begin()) {
                if (*it2 == '*')
                    break;

                if (*it1 != *it2 && *it2 != '?')
                    return false;
                it1--;
                it2--;
            }
        }
        else if (to_compare.back() == '*') {
            auto it1 = str.begin(), it2 = to_compare.begin();

            if (str.size() < to_compare.size())
                return false;

            while (it1 != str.end()) {
                if (*it2 == '*')
                    break;

                if (*it1 != *it2 && *it2 != '?')
                    return false;
                it1++;
                it2++;
            }
        }
        else {
            auto it1 = str.begin(), it2 = to_compare.begin();
            if (str.size() != to_compare.size())
                return false;

            while (it1 != str.end()) {
                if (*it1 != *it2 && *it2 != '?')
                    return false;
                it1++;
                it2++;
            }
        }

        return true;
    }
    case Operation::Invalid:
        break;
    }
    __builtin_unreachable();
}

DbErrorOr<Value> BetweenExpression::evaluate(EvaluationContext& context, Row const& row) const {
    // TODO: Implement this for strings etc
    auto value = TRY(TRY(m_lhs->evaluate(context, row)).to_int());
    auto min = TRY(TRY(m_min->evaluate(context, row)).to_int());
    auto max = TRY(TRY(m_max->evaluate(context, row)).to_int());
    return Value::create_bool(value >= min && value <= max);
}

DbErrorOr<Value> Select::execute(Database& db) const {
    // Comments specify SQL Conceptional Evaluation:
    // https://docs.microsoft.com/en-us/sql/t-sql/queries/select-transact-sql#logical-processing-order-of-the-select-statement
    // FROM
    auto table = TRY(db.table(m_from));

    EvaluationContext context { .table = *table };

    auto should_include_row = [&](Row const& row) -> DbErrorOr<bool> {
        if (!m_where)
            return true;
        return TRY(m_where->evaluate(context, row)).to_bool();
    };

    // TODO: ON
    // TODO: JOIN

    std::vector<Row> rows;
    for (auto const& row : table->rows()) {
        // WHERE
        if (!TRY(should_include_row(row)))
            continue;

        // TODO: GROUP BY
        // TODO: HAVING

        // SELECT
        std::vector<Value> values;
        if (m_columns.select_all()) {
            for (auto const& column : table->columns()) {
                auto table_column = table->get_column(column.name());
                if (!table_column)
                    return DbError { "Internal error: invalid column requested for *: '" + column.name() + "'", start() + 1 };
                values.push_back(row.value(table_column->second));
            }
        }
        else {
            for (auto const& column : m_columns.columns()) {
                values.push_back(TRY(column.column->evaluate(context, row)));
            }
        }
        rows.push_back(Row { values });
    }

    // DISTINCT
    if(m_distinct){
        std::vector<Row> occurences;

        for(const auto& row : rows){
            bool distinct = true;
            for(const auto& to_compare : occurences){
                if(row == to_compare){
                    distinct = false;
                    break;
                }
            }

            if(distinct)
                occurences.push_back(row);
        }
        
        rows = std::move(occurences);
    }

    // ORDER BY
    if (m_order_by) {
        for (const auto& column : m_order_by->columns) {
            auto order_by_column = table->get_column(column.name)->second;
            if (!order_by_column) {
                // TODO: Store source position info in ORDER BY node
                return DbError { "Invalid column to order by: " + column.name, start() };
            }
        }
        std::stable_sort(rows.begin(), rows.end(), [&](Row const& lhs, Row const& rhs) -> bool {
            // TODO: Do sorting properly
            for (const auto& column : m_order_by->columns) {
                auto order_by_column = table->get_column(column.name)->second;

                auto lhs_value = lhs.value(order_by_column).to_string();
                auto rhs_value = rhs.value(order_by_column).to_string();
                if (lhs_value.is_error() || rhs_value.is_error()) {
                    // TODO: Actually handle error
                    return false;
                }

                if (lhs_value.value() != rhs_value.value())
                    return (lhs_value.release_value() < rhs_value.release_value()) == (column.order == OrderBy::Order::Ascending);
            }

            return false;
        });
    }

    if (m_top) {
        if (m_top->unit == Top::Unit::Perc) {
            float mul = static_cast<float>(std::min(m_top->value, (unsigned)100)) / 100;
            rows.resize(rows.size() * mul, rows.back());
        }
        else {
            rows.resize(std::min(m_top->value, (unsigned)rows.size()), rows.back());
        }
    }

    std::vector<std::string> column_names;
    if (m_columns.select_all()) {
        for (auto const& column : table->columns())
            column_names.push_back(column.name());
    }
    else {
        for (auto const& column : m_columns.columns()) {
            if (column.alias)
                column_names.push_back(*column.alias);
            else
                column_names.push_back(column.column->to_string());
        }
    }

    return Value::create_select_result({ column_names, std::move(rows) });
}

DbErrorOr<Value> DeleteFrom::execute(Database& db) const {
    auto table = TRY(db.table(m_from));

    EvaluationContext context { .table = *table };

    auto should_include_row = [&](Row const& row) -> DbErrorOr<bool> {
        if (!m_where)
            return true;
        return TRY(m_where->evaluate(context, row)).to_bool();
    };
    label:;

    for (size_t i = 0; i < table->rows().size(); i++) {
        if (TRY(should_include_row(table->rows()[i]))){
            table->delete_row(i);

            goto label;
        }
    }

    return Value::null();
}

DbErrorOr<Value> CreateTable::execute(Database& db) const {
    auto& table = db.create_table(m_name);
    for (auto const& column : m_columns) {
        TRY(table.add_column(column));
    }
    return { Value::null() };
}

DbErrorOr<Value> DropTable::execute(Database& db) const {
    TRY(db.drop_table(m_name));

    return { Value::null() };
}

DbErrorOr<Value> TruncateTable::execute(Database& db) const {
    auto table = TRY(db.table(m_name));
    table->truncate();

    return { Value::null() };
}

DbErrorOr<Value> AlterTable::execute(Database& db) const {
    auto table = TRY(db.table(m_name));
    
    for(const auto& to_add : m_to_add){
        TRY(table->add_column(to_add));
    }
    
    for(const auto& to_alter : m_to_alter){
        TRY(table->alter_column(to_alter));
    }
    
    for(const auto& to_drop : m_to_drop){
        TRY(table->drop_column(to_drop));
    }

    return { Value::null() };
}

DbErrorOr<Value> InsertInto::execute(Database& db) const {
    if(m_values.size() == 0)
        return { Value::null() };
    auto table = TRY(db.table(m_name));

    RowWithColumnNames::MapType map;
    if (m_columns.size() != m_values.size())
        return DbError { "Values doesn't have corresponding columns", start() };

    EvaluationContext context { .table = *table };
    for (size_t i = 0; i < m_columns.size(); i++) {
        map.insert({ m_columns[i], TRY(m_values[i]->evaluate(context, Row({}))) });
    }

    TRY(table->insert(std::move(map)));
    return { Value::null() };
}

}
