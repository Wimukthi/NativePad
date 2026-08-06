#include "../src/SyntaxHighlighter.h"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

bool HasColor(const std::vector<NativePad::SyntaxSpan>& spans, NativePad::SyntaxColor color) {
    for (const NativePad::SyntaxSpan& span : spans) {
        if (span.color == color && span.length > 0) {
            return true;
        }
    }
    return false;
}

void Expect(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAILED: " << name << "\n";
        throw std::runtime_error(name);
    }
}

} // namespace

void RunSyntaxHighlighterTests() {
    using NativePad::SyntaxColor;
    using NativePad::SyntaxHighlighter;
    using NativePad::SyntaxLanguage;

    const SyntaxHighlighter highlighter;
    const auto json = highlighter.HighlightLine(SyntaxLanguage::Json, L"{\"enabled\": true, \"count\": 42} // note");
    Expect(HasColor(json, SyntaxColor::String), "JSON strings");
    Expect(HasColor(json, SyntaxColor::Keyword), "JSON keywords");
    Expect(HasColor(json, SyntaxColor::Number), "JSON numbers");
    Expect(HasColor(json, SyntaxColor::Comment), "JSON comments");
    Expect(HasColor(json, SyntaxColor::Punctuation), "JSON punctuation");

    const auto ini = highlighter.HighlightLine(SyntaxLanguage::Ini, L"port = 8080 ; development");
    Expect(HasColor(ini, SyntaxColor::Keyword), "INI keys");
    Expect(HasColor(ini, SyntaxColor::Number), "INI numbers");
    Expect(HasColor(ini, SyntaxColor::Comment), "INI comments");

    const auto markdown = highlighter.HighlightLine(SyntaxLanguage::Markdown, L"# NativePad `editor`");
    Expect(HasColor(markdown, SyntaxColor::Keyword), "Markdown headings");
    Expect(HasColor(markdown, SyntaxColor::String), "Markdown code spans");

    const auto xml = highlighter.HighlightLine(SyntaxLanguage::Xml, L"<item name=\"value\" />");
    Expect(HasColor(xml, SyntaxColor::Keyword), "XML names");
    Expect(HasColor(xml, SyntaxColor::String), "XML attributes");
    Expect(HasColor(xml, SyntaxColor::Punctuation), "XML punctuation");
    Expect(highlighter.HighlightLine(SyntaxLanguage::PlainText, L"plain text").empty(), "plain text has no spans");

    std::cout << "SyntaxHighlighter tests passed\n";
}
