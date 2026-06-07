#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NativePad {

enum class TextEncoding {
    Utf8,
    Utf8Bom,
    Utf16Le,
    Utf16Be,
    Ansi,
};

enum class LineEnding {
    CrLf,
    Lf,
    Cr,
    Mixed,
};

[[nodiscard]] std::wstring EncodingLabel(TextEncoding encoding);
[[nodiscard]] LineEnding DetectLineEnding(std::wstring_view text) noexcept;
[[nodiscard]] std::wstring NormalizeLineEndings(std::wstring_view text, LineEnding target);
[[nodiscard]] std::optional<std::vector<char>> EncodeTextBytes(
    std::wstring_view text,
    TextEncoding encoding,
    LineEnding lineEnding,
    std::wstring& error);

} // namespace NativePad
