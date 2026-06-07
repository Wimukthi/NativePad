#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NativePad {

// Read-only document backend for very large files.
//
// The file stays memory-mapped and pages are faulted by Windows on demand. The
// editor asks for visible ranges, line starts, and search matches without ever
// building one giant std::wstring.
class MappedTextDocument {
public:
    struct Match {
        std::size_t position{};
        std::size_t length{};
    };

    MappedTextDocument() = default;
    ~MappedTextDocument();

    MappedTextDocument(const MappedTextDocument&) = delete;
    MappedTextDocument& operator=(const MappedTextDocument&) = delete;

    MappedTextDocument(MappedTextDocument&& other) noexcept;
    MappedTextDocument& operator=(MappedTextDocument&& other) noexcept;

    bool Open(const std::wstring& path, std::wstring& error);
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] std::uint64_t FileByteCount() const noexcept;
    [[nodiscard]] const std::wstring& EncodingLabel() const noexcept;
    [[nodiscard]] std::size_t Length() const noexcept;
    [[nodiscard]] std::size_t LineCount() const noexcept;
    [[nodiscard]] std::size_t LineStart(std::size_t line) const;
    [[nodiscard]] std::size_t LineFromPosition(std::size_t position) const;
    [[nodiscard]] std::size_t MaxLineLength() const noexcept;
    [[nodiscard]] wchar_t CharAt(std::size_t position) const;
    [[nodiscard]] std::wstring TextRange(std::size_t position, std::size_t length) const;
    [[nodiscard]] std::optional<Match> Find(std::wstring_view needle, std::size_t start, bool down, bool matchCase) const;

private:
    // UTF-16 files use wchar offsets. UTF-8/ANSI files use byte offsets as the
    // editor coordinate, which is fast and exact for ASCII/log-style large files.
    enum class Encoding {
        Utf8,
        Utf8Bom,
        Ansi,
        Utf16Le,
        Utf16Be,
    };

    void DetectEncoding();
    void BuildLineIndex();
    void BuildByteLineIndex();
    void BuildUtf16LineIndex();
    [[nodiscard]] wchar_t Utf16UnitAt(std::size_t position) const;
    [[nodiscard]] std::wstring Utf16TextRange(std::size_t position, std::size_t length) const;
    [[nodiscard]] std::wstring ByteTextRange(std::size_t position, std::size_t length) const;
    [[nodiscard]] std::optional<Match> FindBytes(std::wstring_view needle, std::size_t start, bool down, bool matchCase) const;
    [[nodiscard]] std::optional<Match> FindUtf16(std::wstring_view needle, std::size_t start, bool down, bool matchCase) const;
    [[nodiscard]] bool IsUtf16() const noexcept;

    HANDLE file_{INVALID_HANDLE_VALUE};
    HANDLE mapping_{};
    const unsigned char* data_{};
    std::uint64_t fileByteCount_{0};
    std::size_t dataOffset_{0};
    std::size_t contentByteCount_{0};
    std::size_t length_{0};
    std::size_t maxLineLength_{0};
    Encoding encoding_{Encoding::Utf8};
    std::wstring encodingLabel_{L"UTF-8/ANSI"};
    std::vector<std::size_t> lineStarts_{0};
};

} // namespace NativePad
