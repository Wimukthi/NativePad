#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "TextFileTypes.h"

namespace NativePad {

enum class SyntaxColor {
    PlainText,
    Keyword,
    String,
    Number,
    Comment,
    Punctuation,
};

struct SyntaxSpan {
    std::size_t start{};
    std::size_t length{};
    SyntaxColor color{SyntaxColor::Keyword};
};

// The first highlighter slice is intentionally line-local. It gives ordinary
// files useful coloring without scanning an entire document or making mapped
// large-file rendering depend on a global parser state.
class SyntaxHighlighter {
public:
    [[nodiscard]] std::vector<SyntaxSpan> HighlightLine(
        SyntaxLanguage language,
        std::wstring_view line) const;

private:
    [[nodiscard]] std::vector<SyntaxSpan> HighlightJson(std::wstring_view line) const;
    [[nodiscard]] std::vector<SyntaxSpan> HighlightIni(std::wstring_view line) const;
    [[nodiscard]] std::vector<SyntaxSpan> HighlightMarkdown(std::wstring_view line) const;
    [[nodiscard]] std::vector<SyntaxSpan> HighlightXml(std::wstring_view line) const;
};

} // namespace NativePad
