#include "AST.hpp"

#include "Database.hpp"
#include "DbError.hpp"
#include "Function.hpp"
#include "Tuple.hpp"
#include "Value.hpp"
#include "db/core/RowWithColumnNames.hpp"

#include <cctype>
#include <db/util/Is.hpp>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Db::Core::AST {

DbErrorOr<Value> Identifier::evaluate(EvaluationContext& context, TupleWithSource const& row) const {
    if (context.row_type == EvaluationContext::RowType::FromTable) {
        if (!context.table)
            return DbError { "You need a table to resolve identifiers", start() };
        auto column = context.table->get_column(m_id);
        if (!column)
            return DbError { "No such column: " + m_id, start() };
        return row.tuple.value(column->second);
    }

    return TRY(context.columns.resolve_value(context, row, m_id));
}

// FIXME: char ranges doesn't work in row
static DbErrorOr<bool> wildcard_parser(std::string const& lhs, std::string const& rhs) {
    auto is_string_valid = [](std::string const& lhs, std::string const& rhs) {
        size_t len = 0;

        for (auto left_it = lhs.begin(), right_it = rhs.begin(); left_it < lhs.end() || right_it < rhs.end(); left_it++, right_it++) {
            len++;
            if (*right_it == '?') {
                if (lhs.size() < len)
                    return false;
                continue;
            }
            else if (*right_it == '#') {
                if (!isdigit(*left_it) || lhs.size() < len)
                    return false;
            }
            else if (*right_it == '[') {
                if (lhs.size() < len)
                    return false;

                right_it++;
                bool negate = 0;
                if (*right_it == '!') {
                    negate = 1;
                    right_it++;
                }

                std::vector<char> allowed_chars;

                for (auto it = right_it; *it != ']'; it++) {
                    allowed_chars.push_back(*it);
                    right_it++;
                }
                right_it++;

                if (allowed_chars.size() == 3 && allowed_chars[1] == '-') {
                    bool in_range = (allowed_chars[0] <= *left_it && *left_it <= allowed_chars[2]);
                    if (negate ? in_range : !in_range)
                        return false;
                }
                else {
                    bool exists = 0;
                    for (const auto& c : allowed_chars) {
                        exists |= (c == *left_it);
                    }

                    if (negate ? exists : !exists)
                        return false;
                }
            }
            else if (*right_it != *left_it) {
                return false;
            }
        }

        return true;
    };

    bool result = 0;
    auto left_it = lhs.begin(), right_it = rhs.begin();

    for (left_it = lhs.begin(), right_it = rhs.begin(); left_it < lhs.end(); left_it++) {
        size_t dist_to_next_asterisk = 0, substr_len = 0;
        bool brackets = false;
        if (*right_it == '*')
            right_it++;

        for (auto it = right_it; it < rhs.end() && *it != '*'; it++) {
            dist_to_next_asterisk++;

            if (brackets && *it != ']')
                continue;

            substr_len++;

            if (*it == '[')
                brackets = true;
        }

        size_t lstart = static_cast<size_t>(left_it - lhs.begin());
        size_t rstart = static_cast<size_t>(right_it - rhs.begin());
        try {
            if (is_string_valid(lhs.substr(lstart, substr_len), rhs.substr(rstart, dist_to_next_asterisk))) {
                left_it += substr_len;
                right_it += dist_to_next_asterisk;

                result = 1;
            }
        } catch (...) {
            return false;
        }
    }
    if (right_it != rhs.end() && rhs.back() != '*')
        return false;
    return result;
}

DbErrorOr<bool> BinaryOperator::is_true(EvaluationContext& context, TupleWithSource const& row) const {
    // TODO: Implement proper comparison
    switch (m_operation) {
    case Operation::Equal:
        return TRY(m_lhs->evaluate(context, row)) == TRY(m_rhs->evaluate(context, row));
    case Operation::NotEqual:
        return TRY(m_lhs->evaluate(context, row)) != TRY(m_rhs->evaluate(context, row));
    case Operation::Greater:
        return TRY(m_lhs->evaluate(context, row)) > TRY(m_rhs->evaluate(context, row));
    case Operation::GreaterEqual:
        return TRY(m_lhs->evaluate(context, row)) >= TRY(m_rhs->evaluate(context, row));
    case Operation::Less:
        return TRY(m_lhs->evaluate(context, row)) < TRY(m_rhs->evaluate(context, row));
    case Operation::LessEqual:
        return TRY(m_lhs->evaluate(context, row)) <= TRY(m_rhs->evaluate(context, row));
    case Operation::And:
        return TRY(TRY(m_lhs->evaluate(context, row)).to_bool()) && TRY(TRY(m_rhs->evaluate(context, row)).to_bool());
    case Operation::Or:
        return TRY(TRY(m_lhs->evaluate(context, row)).to_bool()) || TRY(TRY(m_rhs->evaluate(context, row)).to_bool());
    case Operation::Not:
        return TRY(TRY(m_lhs->evaluate(context, row)).to_bool());
    case Operation::Like: {
        return wildcard_parser(TRY(TRY(m_lhs->evaluate(context, row)).to_string()), TRY(TRY(m_rhs->evaluate(context, row)).to_string()));
    }
    case Operation::Invalid:
        break;
    }
    __builtin_unreachable();
}

DbErrorOr<Value> ArithmeticOperator::evaluate(EvaluationContext& context, TupleWithSource const& row) const {
    auto lhs = TRY(m_lhs->evaluate(context, row));
    auto rhs = TRY(m_rhs->evaluate(context, row));

    switch (m_operation) {
    case Operation::Add:
        return lhs + rhs;
    case Operation::Sub:
        return lhs - rhs;
    case Operation::Mul:
        return lhs * rhs;
    case Operation::Div:
        return lhs / rhs;
    case Operation::Invalid:
        break;
    }
    __builtin_unreachable();
}

DbErrorOr<Value> BetweenExpression::evaluate(EvaluationContext& context, TupleWithSource const& row) const {
    // TODO: Implement this for strings etc
    auto value = TRY(TRY(m_lhs->evaluate(context, row)).to_int());
    auto min = TRY(TRY(m_min->evaluate(context, row)).to_int());
    auto max = TRY(TRY(m_max->evaluate(context, row)).to_int());
    return Value::create_bool(value >= min && value <= max);
}

DbErrorOr<Value> InExpression::evaluate(EvaluationContext& context, TupleWithSource const& row) const {
    // TODO: Implement this for strings etc
    auto value = TRY(TRY(m_lhs->evaluate(context, row)).to_string());
    for (const auto& arg : m_args) {
        auto to_compare = TRY(TRY(arg->evaluate(context, row)).to_string());

        if (value == to_compare)
            return Value::create_bool(true);
    }
    return Value::create_bool(false);
}

DbErrorOr<Value> CaseExpression::evaluate(EvaluationContext& context, TupleWithSource const& row) const {
    for (const auto& case_expression : m_cases) {
        if (TRY(TRY(case_expression.expr->evaluate(context, row)).to_bool()))
            return TRY(case_expression.value->evaluate(context, row));
    }

    if (m_else_value)
        return TRY(m_else_value->evaluate(context, row));
    return Value::null();
}

SelectColumns::SelectColumns(std::vector<Column> columns)
    : m_columns(std::move(columns)) {
    size_t index = 0;
    for (auto const& column : m_columns) {
        if (column.alias)
            m_aliases.insert({ *column.alias, ResolvedAlias { .column = *column.column, .index = index } });
        m_aliases.insert({ column.column->to_string(), ResolvedAlias { .column = *column.column, .index = index } });
        index++;
    }
}

SelectColumns::ResolvedAlias const* SelectColumns::resolve_alias(std::string const& alias) const {
    auto it = m_aliases.find(alias);
    if (it == m_aliases.end()) {
        return nullptr;
    }
    return &it->second;
}

DbErrorOr<Value> SelectColumns::resolve_value(EvaluationContext& context, TupleWithSource const& tuple, std::string const& alias) const {
    auto resolved_alias = resolve_alias(alias);
    if (resolved_alias)
        return tuple.tuple.value(resolved_alias->index);

    if (!context.table)
        return DbError { "Identifier '" + alias + "' not defined", 0 };

    auto column = context.table->get_column(alias);
    if (!column)
        return DbError { "Identifier '" + alias + "' not defined in table nor as an alias", 0 };
    if (!tuple.source)
        return DbError { "Cannot use table columns on aggregated rows", 0 };
    return tuple.source->value(column->second);
}

DbErrorOr<Value> ExpressionOrIndex::evaluate(EvaluationContext& context, TupleWithSource const& input) const {
    if (is_expression()) {
        return TRY(expression().evaluate(context, input));
    }
    auto index = this->index();
    if (index >= context.columns.columns().size()) {
        // TODO: Store location info
        return DbError { "Index out of range", 0 };
    }
    return TRY(context.columns.columns()[index].column->evaluate(context, input));
}

DbErrorOr<Value> Select::execute(Database& db) const {
    // Comments specify SQL Conceptional Evaluation:
    // https://docs.microsoft.com/en-us/sql/t-sql/queries/select-transact-sql#logical-processing-order-of-the-select-statement
    // FROM
    // TODO: ON
    // TODO: JOIN

    auto table = m_options.from ? TRY(db.table(*m_options.from)) : nullptr;

    SelectColumns select_all_columns;

    SelectColumns const& columns = *TRY([this, table, &select_all_columns]() -> DbErrorOr<SelectColumns const*> {
        if (m_options.columns.select_all()) {
            if (!table) {
                return DbError { "You need a table to do SELECT *", start() };
            }
            std::vector<SelectColumns::Column> all_columns;
            for (auto const& column : table->columns()) {
                all_columns.push_back(SelectColumns::Column { .column = std::make_unique<Identifier>(start() + 1, column.name()) });
            }
            select_all_columns = SelectColumns { std::move(all_columns) };
            return &select_all_columns;
        }
        return &m_options.columns;
    }());

    EvaluationContext context { .columns = columns, .table = table, .row_type = EvaluationContext::RowType::FromTable };

    auto rows = TRY([&]() -> DbErrorOr<std::vector<TupleWithSource>> {
        if (m_options.from) {
            // SELECT etc.
            return collect_rows(context, *table, table->rows());
        }
        else {
            std::vector<Value> values;
            for (auto const& column : m_options.columns.columns()) {
                values.push_back(TRY(column.column->evaluate(context, {})));
            }
            return std::vector<TupleWithSource> { { .tuple = Tuple { values }, .source = {} } };
        }
    }());

    context.row_type = EvaluationContext::RowType::FromResultSet;

    // DISTINCT
    if (m_options.distinct) {
        std::vector<TupleWithSource> occurences;

        // FIXME: O(n^2)
        for (const auto& row : rows) {
            bool distinct = true;
            for (const auto& to_compare : occurences) {
                if (TRY(row == to_compare)) {
                    distinct = false;
                    break;
                }
            }

            if (distinct)
                occurences.push_back(row);
        }

        rows = std::move(occurences);
    }

    // ORDER BY
    if (m_options.order_by) {
        auto generate_tuple_pair_for_ordering = [&](TupleWithSource const& lhs, TupleWithSource const& rhs) -> DbErrorOr<std::pair<Tuple, Tuple>> {
            std::vector<Value> lhs_values;
            std::vector<Value> rhs_values;

            for (auto const& column : m_options.order_by->columns) {
                auto lhs_value = TRY(column.column.evaluate(context, lhs));
                auto rhs_value = TRY(column.column.evaluate(context, rhs));
                if (column.order == OrderBy::Order::Ascending) {
                    lhs_values.push_back(std::move(lhs_value));
                    rhs_values.push_back(std::move(rhs_value));
                }
                else {
                    lhs_values.push_back(std::move(rhs_value));
                    rhs_values.push_back(std::move(lhs_value));
                }
            }

            return std::pair<Tuple, Tuple> { Tuple { lhs_values }, Tuple { rhs_values } };
        };

        std::optional<DbError> error;
        std::stable_sort(rows.begin(), rows.end(), [&](TupleWithSource const& lhs, TupleWithSource const& rhs) -> bool {
            auto pair = generate_tuple_pair_for_ordering(lhs, rhs);
            if (pair.is_error()) {
                error = pair.release_error();
                return false;
            }
            auto [lhs_value, rhs_value] = pair.release_value();
            // std::cout << lhs_value << " < " << rhs_value << std::endl;
            return lhs_value < rhs_value;
        });
        if (error)
            return *error;
    }

    if (m_options.top) {
        if (m_options.top->unit == Top::Unit::Perc) {
            float mul = static_cast<float>(std::min(m_options.top->value, (unsigned)100)) / 100;
            rows.resize(rows.size() * mul, rows.back());
        }
        else {
            rows.resize(std::min(m_options.top->value, (unsigned)rows.size()), rows.back());
        }
    }

    std::vector<std::string> column_names;
    for (auto const& column : columns.columns()) {
        if (column.alias)
            column_names.push_back(*column.alias);
        else
            column_names.push_back(column.column->to_string());
    }

    std::vector<Tuple> output_rows;
    for (auto const& row : rows)
        output_rows.push_back(std::move(row.tuple));

    SelectResult result { column_names, std::move(output_rows) };

    if (m_options.select_into) {
        // TODO: Insert, not overwrite records
        if (db.exists(*m_options.select_into))
            TRY(db.drop_table(*m_options.select_into));
        TRY(db.create_table_from_query(std::move(result), *m_options.select_into));
    }
    return Value::create_select_result(std::move(result));
}

DbErrorOr<std::vector<TupleWithSource>> Select::collect_rows(EvaluationContext& context, Table const& table, std::vector<Tuple> const& input_rows) const {
    auto should_include_row = [&](Tuple const& row) -> DbErrorOr<bool> {
        if (!m_options.where)
            return true;
        return TRY(m_options.where->evaluate(context, TupleWithSource { .tuple = row, .source = {} })).to_bool();
    };

    // Collect all rows that should be included (applying WHERE and GROUP BY)
    // There rows are not yet SELECT'ed - they contain columns from table, no aliases etc.
    std::map<Tuple, std::vector<Tuple>> nonaggregated_row_groups;

    for (const auto& row : input_rows) {
        // WHERE
        if (!TRY(should_include_row(row)))
            continue;

        std::vector<Value> group_key;

        if (m_options.group_by) {
            for (const auto& column_name : m_options.group_by->columns) {
                // TODO: Handle aliases, indexes ("GROUP BY 1") and aggregate functions ("GROUP BY COUNT(x)")
                // https://docs.microsoft.com/en-us/sql/t-sql/queries/select-transact-sql?view=sql-server-ver16#g-using-group-by-with-an-expression
                auto column = table.get_column(column_name);
                if (!column) {
                    // TODO: Store source location info
                    return DbError { "Nonexistent column used in GROUP BY: '" + column_name + "'", start() };
                }
                group_key.push_back(row.value(column->second));
            }
        }

        nonaggregated_row_groups[{ group_key }].push_back(row);
    }

    // Check if grouping / aggregation should be performed
    bool should_group = false;
    if (m_options.group_by) {
        should_group = true;
    }
    else {
        for (auto const& column : context.columns.columns()) {
            if (Util::is<AggregateFunction>(*column.column)) {
                should_group = true;
                break;
            }
        }
    }

    // Special-case for empty sets
    if (input_rows.size() == 0) {
        if (should_group) {
            // We need to create at least one group to make aggregate
            // functions return one row with value "0".
            nonaggregated_row_groups.insert({ Tuple {}, {} });
        }

        // Let's also check column expressions for validity, even
        // if they won't run on real rows.
        std::vector<Value> values;
        for (size_t s = 0; s < table.columns().size(); s++) {
            values.push_back(Value::null());
        }
        Tuple dummy_row { values };
        context.row_group = &input_rows;
        for (auto const& column : m_options.columns.columns()) {
            TRY(column.column->evaluate(context, TupleWithSource { .tuple = dummy_row, .source = {} }));
        }
    }

    // std::cout << "should_group: " << should_group << std::endl;
    // std::cout << "nonaggregated_row_groups.size(): " << nonaggregated_row_groups.size() << std::endl;

    // Group + aggregate rows if needed, otherwise just evaluate column expressions
    std::vector<TupleWithSource> aggregated_rows;
    if (should_group) {
        auto should_include_group = [&](EvaluationContext& context, TupleWithSource const& row) -> DbErrorOr<bool> {
            if (!m_options.having)
                return true;
            return TRY(m_options.having->evaluate(context, row)).to_bool();
        };

        auto is_in_group_by = [&](SelectColumns::Column const& column) {
            if (!m_options.group_by)
                return false;
            for (auto const& group_by_column : m_options.group_by->columns) {
                auto referenced_columns = column.column->referenced_columns();
                if (std::find(referenced_columns.begin(), referenced_columns.end(), group_by_column) != referenced_columns.end())
                    return true;
            }
            return false;
        };

        for (auto const& group : nonaggregated_row_groups) {
            context.row_type = EvaluationContext::RowType::FromTable;
            context.row_group = &group.second;

            std::vector<Value> values;
            for (auto& column : context.columns.columns()) {
                if (auto aggregate_column = dynamic_cast<AggregateFunction*>(column.column.get()); aggregate_column) {
                    values.push_back(TRY(aggregate_column->aggregate(context, group.second)));
                }
                else if (is_in_group_by(column)) {
                    values.push_back(TRY(column.column->evaluate(context, { .tuple = group.second[0], .source = {} })));
                }
                else {
                    // "All columns must be either aggregate or occur in GROUP BY clause"
                    // TODO: Store location info
                    return DbError { "Column '" + column.column->to_string() + "' must be either aggregate or occur in GROUP BY clause", start() };
                }
            }

            context.row_type = EvaluationContext::RowType::FromResultSet;

            TupleWithSource aggregated_row { .tuple = { values }, .source = {} };

            // HAVING
            if (!TRY(should_include_group(context, aggregated_row)))
                continue;

            aggregated_rows.push_back(std::move(aggregated_row));
        }
    }
    else {
        for (auto const& group : nonaggregated_row_groups) {
            for (auto const& row : group.second) {
                std::vector<Value> values;
                for (auto& column : context.columns.columns()) {
                    values.push_back(TRY(column.column->evaluate(context, { .tuple = row, .source = row })));
                }
                aggregated_rows.push_back(TupleWithSource { .tuple = { values }, .source = row });
            }
        }
    }

    return aggregated_rows;
}

DbErrorOr<Value> Union::execute(Database& db) const {
    auto lhs = TRY(TRY(m_lhs->execute(db)).to_select_result());
    auto rhs = TRY(TRY(m_rhs->execute(db)).to_select_result());

    if (lhs.column_names().size() != rhs.column_names().size())
        return DbError { "Querries with different column count", 0 };

    for (size_t i = 0; i < lhs.column_names().size(); i++) {
        if (lhs.column_names()[i] != rhs.column_names()[i])
            return DbError { "Querries with different column set", 0 };
    }

    std::vector<Tuple> rows;

    for (const auto& row : lhs.rows()) {
        rows.push_back(row);
    }

    for (const auto& row : rhs.rows()) {
        if (m_distinct) {
            bool distinct = true;
            for (const auto& to_compare : lhs.rows()) {
                if (TRY(row == to_compare)) {
                    distinct = false;
                    break;
                }
            }

            if (distinct)
                rows.push_back(row);
        }
        else {
            rows.push_back(row);
        }
    }

    return Value::create_select_result({ lhs.column_names(), std::move(rows) });
}

DbErrorOr<Value> DeleteFrom::execute(Database& db) const {
    auto table = TRY(db.table(m_from));

    EvaluationContext context { .columns = {}, .table = table, .row_type = EvaluationContext::RowType::FromTable };

    auto should_include_row = [&](Tuple const& row) -> DbErrorOr<bool> {
        if (!m_where)
            return true;
        return TRY(m_where->evaluate(context, { .tuple = row, .source = {} })).to_bool();
    };
label:;

    for (size_t i = 0; i < table->rows().size(); i++) {
        if (TRY(should_include_row(table->rows()[i]))) {
            table->delete_row(i);

            goto label;
        }
    }

    return Value::null();
}

DbErrorOr<Value> Update::execute(Database& db) const {
    auto table = TRY(db.table(m_table));

    EvaluationContext context { .columns = {}, .table = table, .row_type = EvaluationContext::RowType::FromTable };

    for (const auto& update_pair : m_to_update) {
        auto column = table->get_column(update_pair.column);
        size_t i = 0;

        for (auto& row : table->rows()) {
            TRY(table->update_cell(i, column->second, TRY(update_pair.expr->evaluate(context, { .tuple = row, .source = {} }))));
            i++;
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

    for (const auto& to_add : m_to_add) {
        TRY(table->add_column(to_add));
    }

    for (const auto& to_alter : m_to_alter) {
        TRY(table->alter_column(to_alter));
    }

    for (const auto& to_drop : m_to_drop) {
        TRY(table->drop_column(to_drop));
    }

    return { Value::null() };
}

DbErrorOr<Value> InsertInto::execute(Database& db) const {
    auto table = TRY(db.table(m_name));

    RowWithColumnNames::MapType map;
    EvaluationContext context { .columns = {}, .table = table, .row_type = EvaluationContext::RowType::FromTable };
    if (m_select) {
        auto result = TRY(TRY(m_select.value()->execute(db)).to_select_result());

        if (m_columns.size() != result.column_names().size())
            return DbError { "Values doesn't have corresponding columns", start() };

        for (const auto& row : result.rows()) {
            for (size_t i = 0; i < m_columns.size(); i++) {
                map.insert({ m_columns[i], row.value(i) });
            }

            TRY(table->insert(std::move(map)));
        }
    }
    else {
        if (m_columns.size() != m_values.size())
            return DbError { "Values doesn't have corresponding columns", start() };

        for (size_t i = 0; i < m_columns.size(); i++) {
            map.insert({ m_columns[i], TRY(m_values[i]->evaluate(context, {})) });
        }

        TRY(table->insert(std::move(map)));
    }
    return { Value::null() };
}

DbErrorOr<Value> Import::execute(Database& db) const {
    auto& new_table = db.create_table(m_table);
    switch (m_mode) {
    case Mode::Csv:
        TRY(new_table.import_from_csv(m_filename));
        break;
    }
    return Value::null();
}
}
