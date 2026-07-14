#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "TextFormat.h"

namespace NativePad {

// Editable document backend for very large files.
//
// The original file stays memory-mapped and is never decoded in full. Content
// is represented as a piece table over two sources: the read-only mapped file
// and an append-only in-memory add buffer that holds inserted text. Edits only
// manipulate piece descriptors, so opening is instant and memory use scales
// with the number of edits rather than the file size.
//
// Coordinate space matches MappedTextDocument: UTF-16 files use wchar offsets,
// while UTF-8/ANSI files use byte offsets. CharAt returns raw bytes for
// UTF-8/ANSI so the editor can navigate and search in byte space; TextRange
// decodes spans to UTF-16 for display.
class LargeTextDocument {
public:
    struct Match {
        std::size_t position{};
        std::size_t length{};
    };

    LargeTextDocument() = default;
    ~LargeTextDocument();

    LargeTextDocument(const LargeTextDocument&) = delete;
    LargeTextDocument& operator=(const LargeTextDocument&) = delete;

    LargeTextDocument(LargeTextDocument&& other) noexcept;
    LargeTextDocument& operator=(LargeTextDocument&& other) noexcept;

    bool Open(const std::wstring& path, std::wstring& error);
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] std::uint64_t FileByteCount() const noexcept;
    [[nodiscard]] const std::wstring& EncodingLabel() const noexcept;
    [[nodiscard]] TextEncoding Encoding() const noexcept;
    [[nodiscard]] LineEnding DetectedLineEnding() const noexcept;
    [[nodiscard]] bool Dirty() const noexcept;

    [[nodiscard]] std::size_t Length() const noexcept;
    [[nodiscard]] std::size_t LineCount() const noexcept;
    [[nodiscard]] std::size_t LineStart(std::size_t line) const;
    [[nodiscard]] std::size_t LineFromPosition(std::size_t position) const;
    [[nodiscard]] std::size_t MaxLineLength() const noexcept;
    [[nodiscard]] wchar_t CharAt(std::size_t position) const;
    [[nodiscard]] std::wstring TextRange(std::size_t position, std::size_t length) const;
    [[nodiscard]] std::optional<Match> Find(std::wstring_view needle, std::size_t start, bool down, bool matchCase) const;

    // Replaces eraseLength units at position with the given UTF-16 text. The
    // erase range and position are snapped outward to code-point boundaries for
    // UTF-8 so a multibyte sequence can never be split.
    void Replace(std::size_t position, std::size_t eraseLength, std::wstring_view insertText);

    // Writes the current content to path in the file's encoding (with BOM where
    // the encoding uses one). Used to stage a temp file the shell renames over
    // the original.
    [[nodiscard]] bool SaveTo(const std::wstring& path, std::wstring& error) const;

private:
    enum class FileEncoding {
        Utf8,
        Utf8Bom,
        Ansi,
        Utf16Le,
        Utf16Be,
    };

    enum class Source {
        Original,
        Add,
    };

    struct Piece {
        Source source{Source::Original};
        std::size_t start{0};      // offset in the source, in units
        std::size_t length{0};     // length in units
        std::size_t newlines{0};   // newline units within [start, start+length)
    };

    void DetectEncoding();
    void BuildOriginalNewlineIndex();
    void EnsurePrefixSums() const;
    [[nodiscard]] bool IsUtf16() const noexcept;

    [[nodiscard]] std::size_t OriginalUnitCount() const noexcept;
    [[nodiscard]] wchar_t OriginalUnitAt(std::size_t index) const;
    [[nodiscard]] wchar_t AddUnitAt(std::size_t index) const;
    [[nodiscard]] wchar_t SourceUnitAt(Source source, std::size_t index) const;
    [[nodiscard]] const std::vector<std::size_t>& SourceNewlines(Source source) const noexcept;

    [[nodiscard]] std::size_t CountNewlines(Source source, std::size_t start, std::size_t length) const;
    [[nodiscard]] std::size_t PieceIndexForPosition(std::size_t position, std::size_t& pieceStart) const;
    [[nodiscard]] std::wstring DecodeUnits(Source source, std::size_t start, std::size_t length) const;
    [[nodiscard]] std::size_t SnapLeftToBoundary(std::size_t position) const;
    void AppendToAddBuffer(std::wstring_view text, std::size_t& addStart, std::size_t& addLength, std::size_t& addNewlines);
    void UpdateMaxLineLengthAround(std::size_t position);

    // Mapping state.
    HANDLE file_{INVALID_HANDLE_VALUE};
    HANDLE mapping_{};
    const unsigned char* data_{};
    std::uint64_t fileByteCount_{0};
    std::size_t dataOffset_{0};
    std::size_t originalUnitCount_{0};
    FileEncoding encoding_{FileEncoding::Utf8};
    std::wstring encodingLabel_{L"UTF-8/ANSI"};
    LineEnding lineEnding_{LineEnding::CrLf};

    // Piece table state.
    std::vector<Piece> pieces_;
    std::vector<unsigned char> addBytes_;   // UTF-8/ANSI inserted bytes
    std::vector<wchar_t> addUnits_;          // UTF-16 inserted units
    std::vector<std::size_t> originalNewlines_;
    std::vector<std::size_t> addNewlines_;
    std::size_t maxLineLength_{0};
    bool dirty_{false};

    // Lazily rebuilt prefix sums over pieces_.
    mutable std::vector<std::size_t> prefixLength_;
    mutable std::vector<std::size_t> prefixNewlines_;
    mutable bool prefixValid_{false};
};

} // namespace NativePad
