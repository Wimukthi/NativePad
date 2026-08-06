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
    Oem437,
};

enum class LineEnding {
    CrLf,
    Lf,
    Cr,
    Mixed,
};

[[nodiscard]] std::wstring EncodingLabel(TextEncoding encoding);
[[nodiscard]] bool IsValidUtf8(std::string_view bytes) noexcept;
[[nodiscard]] bool LooksLikeOem437(std::string_view bytes) noexcept;
[[nodiscard]] LineEnding DetectLineEnding(std::wstring_view text) noexcept;
[[nodiscard]] std::wstring NormalizeLineEndings(std::wstring_view text, LineEnding target);
[[nodiscard]] std::optional<std::vector<char>> EncodeTextBytes(
    std::wstring_view text,
    TextEncoding encoding,
    LineEnding lineEnding,
    std::wstring& error);

} // namespace NativePad
