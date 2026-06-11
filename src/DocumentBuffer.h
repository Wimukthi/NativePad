#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NativePad {

// Editable text storage for normal-sized documents.
//
// This is a compact piece-table: the original file text is immutable, inserted
// text is appended to add_, and the document is represented by pieces that point
// into either source. Edits avoid repeatedly copying the whole document.
class DocumentBuffer {
public:
    DocumentBuffer();
    explicit DocumentBuffer(std::wstring text);

    void Reset(std::wstring text = {});
    void Insert(std::size_t position, std::wstring_view text);
    void Erase(std::size_t position, std::size_t length);
    void Replace(std::size_t position, std::size_t length, std::wstring_view text);

    [[nodiscard]] std::size_t Length() const noexcept;
    [[nodiscard]] std::size_t PieceCount() const noexcept;
    [[nodiscard]] std::size_t LineCount() const;
    [[nodiscard]] wchar_t CharAt(std::size_t position) const;
    [[nodiscard]] std::wstring TextRange(std::size_t position, std::size_t length) const;
    [[nodiscard]] std::wstring Text() const;

    // Substring search over the piece table without materializing the document.
    // Returns the first match at or after (forward) / before (backward) start.
    [[nodiscard]] std::optional<std::size_t> Find(std::wstring_view needle, std::size_t start, bool forward, bool matchCase) const;

private:
    // A piece identifies a contiguous span in one of the backing strings.
    enum class Source {
        Original,
        Add,
    };

    struct Piece {
        Source source;
        std::size_t start;
        std::size_t length;
    };

    struct PiecePosition {
        std::size_t index;
        std::size_t offset;
    };

    // Locate translates a logical document position into a piece index/offset.
    [[nodiscard]] const std::wstring& SourceText(Source source) const noexcept;
    [[nodiscard]] PiecePosition Locate(std::size_t position) const;
    void InsertPiece(std::size_t position, Piece piece);
    void NormalizePieces();
    void ValidateRange(std::size_t position, std::size_t length) const;

    std::wstring original_;
    std::wstring add_;
    std::vector<Piece> pieces_;
    std::size_t length_{0};

    // Locate cache: piece-table reads during paint and search are highly
    // sequential, so remembering the last piece turns most lookups into O(1)
    // instead of a linear scan. Invalidated on any mutation.
    mutable std::size_t cacheIndex_{0};
    mutable std::size_t cachePieceStart_{0};
    mutable bool cacheValid_{false};
};

} // namespace NativePad
