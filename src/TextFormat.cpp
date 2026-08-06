#include "TextFormat.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace NativePad {

namespace {

std::optional<std::vector<char>> WideToCodePage(UINT codePage, std::wstring_view text, DWORD flags, bool failOnDefaultChar) {
    if (text.empty()) {
        return std::vector<char>();
    }

    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    BOOL usedDefaultChar = FALSE;
    LPBOOL usedDefaultCharPtr = failOnDefaultChar ? &usedDefaultChar : nullptr;
    const int inputLength = static_cast<int>(text.size());
    const int required = WideCharToMultiByte(
        codePage,
        flags,
        text.data(),
        inputLength,
        nullptr,
        0,
        nullptr,
        usedDefaultCharPtr);

    if (required <= 0 || usedDefaultChar) {
        return std::nullopt;
    }

    std::vector<char> bytes(static_cast<std::size_t>(required));
    usedDefaultChar = FALSE;
    const int written = WideCharToMultiByte(
        codePage,
        flags,
        text.data(),
        inputLength,
        bytes.data(),
        required,
        nullptr,
        usedDefaultCharPtr);

    if (written <= 0 || usedDefaultChar) {
        return std::nullopt;
    }

    return bytes;
}

void AppendUtf16Le(std::vector<char>& bytes, std::wstring_view text) {
    for (const wchar_t value : text) {
        const auto unit = static_cast<std::uint16_t>(value);
        bytes.push_back(static_cast<char>(unit & 0xFFu));
        bytes.push_back(static_cast<char>((unit >> 8) & 0xFFu));
    }
}

void AppendUtf16Be(std::vector<char>& bytes, std::wstring_view text) {
    for (const wchar_t value : text) {
        const auto unit = static_cast<std::uint16_t>(value);
        bytes.push_back(static_cast<char>((unit >> 8) & 0xFFu));
        bytes.push_back(static_cast<char>(unit & 0xFFu));
    }
}

std::wstring_view LineEndingText(LineEnding ending) noexcept {
    switch (ending) {
    case LineEnding::Lf:
        return L"\n";
    case LineEnding::Cr:
        return L"\r";
    case LineEnding::CrLf:
    case LineEnding::Mixed:
    default:
        return L"\r\n";
    }
}

bool IsUtf8Continuation(unsigned char value) noexcept {
    return (value & 0xC0u) == 0x80u;
}

} // namespace

bool IsValidUtf8(std::string_view bytes) noexcept {
    for (std::size_t index = 0; index < bytes.size();) {
        const unsigned char lead = static_cast<unsigned char>(bytes[index]);
        if (lead <= 0x7Fu) {
            ++index;
            continue;
        }

        std::size_t length = 0;
        unsigned int codePoint = 0;
        unsigned int minimum = 0;
        if (lead >= 0xC2u && lead <= 0xDFu) {
            length = 2;
            codePoint = lead & 0x1Fu;
            minimum = 0x80u;
        } else if (lead >= 0xE0u && lead <= 0xEFu) {
            length = 3;
            codePoint = lead & 0x0Fu;
            minimum = 0x800u;
        } else if (lead >= 0xF0u && lead <= 0xF4u) {
            length = 4;
            codePoint = lead & 0x07u;
            minimum = 0x10000u;
        } else {
            return false;
        }

        if (index + length > bytes.size()) {
            return false;
        }

        for (std::size_t offset = 1; offset < length; ++offset) {
            const unsigned char continuation = static_cast<unsigned char>(bytes[index + offset]);
            if (!IsUtf8Continuation(continuation)) {
                return false;
            }
            codePoint = (codePoint << 6) | (continuation & 0x3Fu);
        }

        if (codePoint < minimum || codePoint > 0x10FFFFu || (codePoint >= 0xD800u && codePoint <= 0xDFFFu)) {
            return false;
        }
        index += length;
    }

    return true;
}

bool LooksLikeOem437(std::string_view bytes) noexcept {
    // DOS NFO art commonly uses CP437 shading, box-drawing, and block glyphs.
    // Requiring two graphic bytes avoids treating a normal UTF-8 accented word
    // as OEM data while catching the paired line characters used by NFO files.
    std::size_t graphicCount = 0;
    for (const char raw : bytes) {
        const unsigned char value = static_cast<unsigned char>(raw);
        if ((value >= 0xB0u && value <= 0xDFu) || (value >= 0xF0u && value <= 0xFEu)) {
            ++graphicCount;
            if (graphicCount >= 2) {
                return true;
            }
        }
    }
    return false;
}

std::wstring EncodingLabel(TextEncoding encoding) {
    switch (encoding) {
    case TextEncoding::Utf8Bom:
        return L"UTF-8 BOM";
    case TextEncoding::Utf16Le:
        return L"UTF-16 LE";
    case TextEncoding::Utf16Be:
        return L"UTF-16 BE";
    case TextEncoding::Ansi:
        return L"ANSI";
    case TextEncoding::Oem437:
        return L"OEM 437";
    case TextEncoding::Utf8:
    default:
        return L"UTF-8";
    }
}

LineEnding DetectLineEnding(std::wstring_view text) noexcept {
    bool sawCrLf = false;
    bool sawLf = false;
    bool sawCr = false;

    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') {
                sawCrLf = true;
                i += 2;
            } else {
                sawCr = true;
                ++i;
            }
            continue;
        }

        if (text[i] == L'\n') {
            sawLf = true;
        }
        ++i;
    }

    const int kinds = (sawCrLf ? 1 : 0) + (sawLf ? 1 : 0) + (sawCr ? 1 : 0);
    if (kinds > 1) {
        return LineEnding::Mixed;
    }
    if (sawLf) {
        return LineEnding::Lf;
    }
    if (sawCr) {
        return LineEnding::Cr;
    }
    return LineEnding::CrLf;
}

std::wstring NormalizeLineEndings(std::wstring_view text, LineEnding target) {
    if (target == LineEnding::Mixed) {
        return std::wstring(text);
    }

    const std::wstring_view replacement = LineEndingText(target);
    std::wstring normalized;
    normalized.reserve(text.size());

    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') {
                i += 2;
            } else {
                ++i;
            }
            normalized.append(replacement);
            continue;
        }

        if (text[i] == L'\n') {
            ++i;
            normalized.append(replacement);
            continue;
        }

        normalized.push_back(text[i]);
        ++i;
    }

    return normalized;
}

std::optional<std::vector<char>> EncodeTextBytes(
    std::wstring_view text,
    TextEncoding encoding,
    LineEnding lineEnding,
    std::wstring& error) {
    // Save uses the detected document line-ending policy as the final step.
    // Mixed files are left untouched so NativePad does not rewrite intentional
    // mixed-line content just because the file was opened and saved.
    const std::wstring normalized = NormalizeLineEndings(text, lineEnding);

    switch (encoding) {
    case TextEncoding::Utf8: {
        auto bytes = WideToCodePage(CP_UTF8, normalized, WC_ERR_INVALID_CHARS, false);
        if (!bytes) {
            error = L"Could not encode the document as UTF-8.";
        }
        return bytes;
    }
    case TextEncoding::Utf8Bom: {
        auto bytes = WideToCodePage(CP_UTF8, normalized, WC_ERR_INVALID_CHARS, false);
        if (!bytes) {
            error = L"Could not encode the document as UTF-8.";
            return std::nullopt;
        }
        bytes->insert(bytes->begin(), {static_cast<char>(0xEF), static_cast<char>(0xBB), static_cast<char>(0xBF)});
        return bytes;
    }
    case TextEncoding::Utf16Le: {
        std::vector<char> bytes{static_cast<char>(0xFF), static_cast<char>(0xFE)};
        bytes.reserve(2 + (normalized.size() * sizeof(wchar_t)));
        AppendUtf16Le(bytes, normalized);
        return bytes;
    }
    case TextEncoding::Utf16Be: {
        std::vector<char> bytes{static_cast<char>(0xFE), static_cast<char>(0xFF)};
        bytes.reserve(2 + (normalized.size() * sizeof(wchar_t)));
        AppendUtf16Be(bytes, normalized);
        return bytes;
    }
    case TextEncoding::Ansi: {
        auto bytes = WideToCodePage(CP_ACP, normalized, WC_NO_BEST_FIT_CHARS, true);
        if (!bytes) {
            error = L"Could not encode every character using the system ANSI code page.";
        }
        return bytes;
    }
    case TextEncoding::Oem437: {
        auto bytes = WideToCodePage(437, normalized, WC_NO_BEST_FIT_CHARS, true);
        if (!bytes) {
            error = L"Could not encode every character using OEM code page 437.";
        }
        return bytes;
    }
    default:
        error = L"Unsupported text encoding.";
        return std::nullopt;
    }
}

} // namespace NativePad
