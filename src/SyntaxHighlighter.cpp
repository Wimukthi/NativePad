#include "SyntaxHighlighter.h"

#include <algorithm>
#include <cwctype>

namespace NativePad {

namespace {

bool IsIdentifierStart(wchar_t value) noexcept {
    return value == L'_' || std::iswalpha(value) != 0;
}

bool IsIdentifierPart(wchar_t value) noexcept {
    return value == L'_' || value == L'-' || std::iswalnum(value) != 0;
}

bool IsNumberStart(wchar_t value) noexcept {
    return value == L'+' || value == L'-' || value == L'.' || std::iswdigit(value) != 0;
}

bool IsNumberPart(wchar_t value) noexcept {
    return value == L'.' || value == L'+' || value == L'-' || value == L'e' || value == L'E' || std::iswdigit(value) != 0;
}

bool IsJsonKeyword(std::wstring_view value) noexcept {
    return value == L"true" || value == L"false" || value == L"null";
}

bool IsXmlNamePart(wchar_t value) noexcept {
    return IsIdentifierPart(value) || value == L':' || value == L'.';
}

void AddSpan(std::vector<SyntaxSpan>& spans, std::size_t start, std::size_t end, SyntaxColor color) {
    if (end > start) {
        spans.push_back(SyntaxSpan{start, end - start, color});
    }
}

std::size_t ConsumeQuoted(std::wstring_view line, std::size_t start, wchar_t quote) {
    std::size_t index = start + 1;
    while (index < line.size()) {
        if (line[index] == L'\\') {
            index += std::min<std::size_t>(2, line.size() - index);
            continue;
        }
        if (line[index] == quote) {
            return index + 1;
        }
        ++index;
    }
    return line.size();
}

std::size_t ConsumeIdentifier(std::wstring_view line, std::size_t start) {
    std::size_t index = start + 1;
    while (index < line.size() && IsIdentifierPart(line[index])) {
        ++index;
    }
    return index;
}

std::size_t ConsumeNumber(std::wstring_view line, std::size_t start) {
    std::size_t index = start;
    while (index < line.size() && IsNumberPart(line[index])) {
        ++index;
    }
    return index;
}

} // namespace

std::vector<SyntaxSpan> SyntaxHighlighter::HighlightLine(SyntaxLanguage language, std::wstring_view line) const {
    switch (language) {
    case SyntaxLanguage::Json:
        return HighlightJson(line);
    case SyntaxLanguage::Ini:
        return HighlightIni(line);
    case SyntaxLanguage::Markdown:
        return HighlightMarkdown(line);
    case SyntaxLanguage::Xml:
        return HighlightXml(line);
    case SyntaxLanguage::PlainText:
    default:
        return {};
    }
}

std::vector<SyntaxSpan> SyntaxHighlighter::HighlightJson(std::wstring_view line) const {
    std::vector<SyntaxSpan> spans;
    for (std::size_t index = 0; index < line.size();) {
        if (line[index] == L'"') {
            const std::size_t end = ConsumeQuoted(line, index, L'"');
            AddSpan(spans, index, end, SyntaxColor::String);
            index = end;
            continue;
        }
        if (line[index] == L'/' && index + 1 < line.size() && line[index + 1] == L'/') {
            AddSpan(spans, index, line.size(), SyntaxColor::Comment);
            break;
        }
        if (IsNumberStart(line[index]) &&
            (std::iswdigit(line[index]) != 0 || (index + 1 < line.size() && std::iswdigit(line[index + 1]) != 0))) {
            const std::size_t end = ConsumeNumber(line, index);
            AddSpan(spans, index, end, SyntaxColor::Number);
            index = end;
            continue;
        }
        if (IsIdentifierStart(line[index])) {
            const std::size_t end = ConsumeIdentifier(line, index);
            if (IsJsonKeyword(line.substr(index, end - index))) {
                AddSpan(spans, index, end, SyntaxColor::Keyword);
            }
            index = end;
            continue;
        }
        if (std::wstring_view(L"{}[]:,").find(line[index]) != std::wstring_view::npos) {
            AddSpan(spans, index, index + 1, SyntaxColor::Punctuation);
        }
        ++index;
    }
    return spans;
}

std::vector<SyntaxSpan> SyntaxHighlighter::HighlightIni(std::wstring_view line) const {
    std::vector<SyntaxSpan> spans;
    std::size_t first = 0;
    while (first < line.size() && std::iswspace(line[first]) != 0) {
        ++first;
    }
    if (first < line.size() && (line[first] == L';' || line[first] == L'#')) {
        AddSpan(spans, first, line.size(), SyntaxColor::Comment);
        return spans;
    }
    if (first < line.size() && line[first] == L'[') {
        const std::size_t end = line.find(L']', first + 1);
        AddSpan(spans, first, end == std::wstring_view::npos ? line.size() : end + 1, SyntaxColor::Keyword);
    }

    bool keyDone = false;
    for (std::size_t index = first; index < line.size();) {
        if ((line[index] == L'"' || line[index] == L'\'') && keyDone) {
            const std::size_t end = ConsumeQuoted(line, index, line[index]);
            AddSpan(spans, index, end, SyntaxColor::String);
            index = end;
            continue;
        }
        if ((line[index] == L';' || line[index] == L'#') && keyDone) {
            AddSpan(spans, index, line.size(), SyntaxColor::Comment);
            break;
        }
        if (!keyDone && (line[index] == L'=' || line[index] == L':')) {
            keyDone = true;
            AddSpan(spans, index, index + 1, SyntaxColor::Punctuation);
            ++index;
            continue;
        }
        if (keyDone && IsNumberStart(line[index]) &&
            (std::iswdigit(line[index]) != 0 || (index + 1 < line.size() && std::iswdigit(line[index + 1]) != 0))) {
            const std::size_t end = ConsumeNumber(line, index);
            AddSpan(spans, index, end, SyntaxColor::Number);
            index = end;
            continue;
        }
        if (!keyDone && IsIdentifierStart(line[index])) {
            const std::size_t end = ConsumeIdentifier(line, index);
            AddSpan(spans, index, end, SyntaxColor::Keyword);
            index = end;
            continue;
        }
        ++index;
    }
    return spans;
}

std::vector<SyntaxSpan> SyntaxHighlighter::HighlightMarkdown(std::wstring_view line) const {
    std::vector<SyntaxSpan> spans;
    std::size_t first = 0;
    while (first < line.size() && line[first] == L' ') {
        ++first;
    }
    if (first < line.size() && line[first] == L'#') {
        AddSpan(spans, first, line.size(), SyntaxColor::Keyword);
    }
    if (line.substr(first, 3) == L"```") {
        AddSpan(spans, first, line.size(), SyntaxColor::Comment);
        return spans;
    }
    for (std::size_t index = 0; index < line.size();) {
        if (line[index] == L'`') {
            const std::size_t end = ConsumeQuoted(line, index, L'`');
            AddSpan(spans, index, end, SyntaxColor::String);
            index = end;
            continue;
        }
        if (line[index] == L'>' || line[index] == L'*' || line[index] == L'_') {
            AddSpan(spans, index, index + 1, SyntaxColor::Punctuation);
        }
        ++index;
    }
    return spans;
}

std::vector<SyntaxSpan> SyntaxHighlighter::HighlightXml(std::wstring_view line) const {
    std::vector<SyntaxSpan> spans;
    std::size_t index = 0;
    while (index < line.size()) {
        if (line.substr(index, 4) == L"<!--") {
            const std::size_t close = line.find(L"-->", index + 4);
            AddSpan(spans, index, close == std::wstring_view::npos ? line.size() : close + 3, SyntaxColor::Comment);
            break;
        }
        if (line[index] == L'<') {
            AddSpan(spans, index, index + 1, SyntaxColor::Punctuation);
            ++index;
            if (index < line.size() && line[index] == L'/') {
                AddSpan(spans, index, index + 1, SyntaxColor::Punctuation);
                ++index;
            }
            if (index < line.size() && IsIdentifierStart(line[index])) {
                const std::size_t end = index + 1;
                std::size_t nameEnd = end;
                while (nameEnd < line.size() && IsXmlNamePart(line[nameEnd])) {
                    ++nameEnd;
                }
                AddSpan(spans, index, nameEnd, SyntaxColor::Keyword);
                index = nameEnd;
            }
            continue;
        }
        if (line[index] == L'"' || line[index] == L'\'') {
            const std::size_t end = ConsumeQuoted(line, index, line[index]);
            AddSpan(spans, index, end, SyntaxColor::String);
            index = end;
            continue;
        }
        if (line[index] == L'>' || line[index] == L'/' || line[index] == L'=') {
            AddSpan(spans, index, index + 1, SyntaxColor::Punctuation);
            ++index;
            continue;
        }
        if (IsIdentifierStart(line[index])) {
            const std::size_t end = ConsumeIdentifier(line, index);
            std::size_t after = end;
            while (after < line.size() && std::iswspace(line[after]) != 0) {
                ++after;
            }
            AddSpan(spans, index, end, after < line.size() && line[after] == L'=' ? SyntaxColor::Keyword : SyntaxColor::PlainText);
            index = end;
            continue;
        }
        ++index;
    }
    return spans;
}

} // namespace NativePad
