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
// Maps each UTF-16 index of decoded UTF-8 text back to its byte offset. On
// success indexToByte has utf16Length + 1 entries (the last is bytes.size()).
// Returns false when the bytes are not strict UTF-8 or decode to a different
// number of UTF-16 units, so callers can fall back to a simpler mapping.
[[nodiscard]] bool MapUtf16IndicesToUtf8Bytes(
    std::string_view bytes,
    std::size_t utf16Length,
    std::vector<std::size_t>& indexToByte);
[[nodiscard]] std::optional<std::vector<char>> EncodeTextBytes(
    std::wstring_view text,
    TextEncoding encoding,
    LineEnding lineEnding,
    std::wstring& error);

} // namespace NativePad
