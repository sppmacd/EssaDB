#pragma once

#include <EssaGUI/gui/textediting/SyntaxHighlighter.hpp>

namespace EssaDB {

class SQLSyntaxHighlighter : public GUI::SyntaxHighlighter {
    virtual std::vector<GUI::TextStyle> styles() const override;
    virtual std::vector<GUI::StyledTextSpan> spans(Util::UString const& input) const override;
};

}
