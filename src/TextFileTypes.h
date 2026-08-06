#pragma once

#include <span>
#include <string>
#include <string_view>

namespace NativePad {

// The language enum is deliberately shared by shell/file-association code and
// the editor highlighter. Association and highlighting remain independent:
// an extension can be registered without enabling a lexer for it.
enum class SyntaxLanguage {
    PlainText,
    Json,
    Ini,
    Markdown,
    Xml,
};

struct TextFileType {
    std::wstring_view extension;
    std::wstring_view description;
    SyntaxLanguage syntax{SyntaxLanguage::PlainText};
};

[[nodiscard]] std::span<const TextFileType> SupportedTextFileTypes() noexcept;
[[nodiscard]] const TextFileType* TextFileTypeForExtension(std::wstring_view extension) noexcept;
[[nodiscard]] const TextFileType* TextFileTypeForPath(std::wstring_view path) noexcept;
[[nodiscard]] std::wstring_view FileExtension(std::wstring_view path) noexcept;
[[nodiscard]] std::wstring PlainTextFilePattern();
[[nodiscard]] std::wstring PlainTextFileFilter();
[[nodiscard]] std::wstring DefaultExtensionForPath(std::wstring_view path);

} // namespace NativePad
