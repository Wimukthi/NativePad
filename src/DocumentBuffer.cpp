#include "DocumentBuffer.h"

#include <algorithm>
#include <stdexcept>

namespace NativePad {

DocumentBuffer::DocumentBuffer() = default;

DocumentBuffer::DocumentBuffer(std::wstring text) {
    Reset(std::move(text));
}

void DocumentBuffer::Reset(std::wstring text) {
    // Loading a document starts with a single piece over the immutable original.
    // Later inserts append to add_ and only modify the piece list.
    original_ = std::move(text);
    add_.clear();
    pieces_.clear();
    length_ = original_.size();

    if (!original_.empty()) {
        pieces_.push_back({Source::Original, 0, original_.size()});
    }
}

void DocumentBuffer::Insert(std::size_t position, std::wstring_view text) {
    if (position > length_) {
        throw std::out_of_range("insert position is outside the document");
    }

    if (text.empty()) {
        return;
    }

    const std::size_t addStart = add_.size();
    add_.append(text);
    InsertPiece(position, {Source::Add, addStart, text.size()});
    length_ += text.size();
    NormalizePieces();
}

void DocumentBuffer::Erase(std::size_t position, std::size_t length) {
    ValidateRange(position, length);
    if (length == 0) {
        return;
    }

    std::size_t remaining = length;
    PiecePosition cursor = Locate(position);

    // Erasing can trim, remove, or split pieces. The backing strings remain
    // untouched, so undo/redo and nearby edits do not require large copies.
    while (remaining > 0 && cursor.index < pieces_.size()) {
        Piece& piece = pieces_[cursor.index];
        const std::size_t removable = std::min(remaining, piece.length - cursor.offset);

        if (cursor.offset == 0 && removable == piece.length) {
            pieces_.erase(pieces_.begin() + static_cast<std::ptrdiff_t>(cursor.index));
        } else if (cursor.offset == 0) {
            piece.start += removable;
            piece.length -= removable;
            ++cursor.index;
        } else if (cursor.offset + removable == piece.length) {
            piece.length = cursor.offset;
            ++cursor.index;
        } else {
            Piece right{piece.source, piece.start + cursor.offset + removable, piece.length - cursor.offset - removable};
            piece.length = cursor.offset;
            pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(cursor.index + 1), right);
            cursor.index += 2;
        }

        remaining -= removable;
        cursor.offset = 0;
    }

    length_ -= length;
    NormalizePieces();
}

void DocumentBuffer::Replace(std::size_t position, std::size_t length, std::wstring_view text) {
    ValidateRange(position, length);
    Erase(position, length);
    Insert(position, text);
}

std::size_t DocumentBuffer::Length() const noexcept {
    return length_;
}

std::size_t DocumentBuffer::PieceCount() const noexcept {
    return pieces_.size();
}

std::size_t DocumentBuffer::LineCount() const {
    if (length_ == 0) {
        return 1;
    }

    std::size_t lines = 1;
    for (const Piece& piece : pieces_) {
        const std::wstring& source = SourceText(piece.source);
        lines += static_cast<std::size_t>(std::count(source.begin() + static_cast<std::ptrdiff_t>(piece.start),
                                                    source.begin() + static_cast<std::ptrdiff_t>(piece.start + piece.length),
                                                    L'\n'));
    }

    return lines;
}

wchar_t DocumentBuffer::CharAt(std::size_t position) const {
    if (position >= length_) {
        throw std::out_of_range("character position is outside the document");
    }

    PiecePosition located = Locate(position);
    const Piece& piece = pieces_[located.index];
    return SourceText(piece.source)[piece.start + located.offset];
}

std::wstring DocumentBuffer::TextRange(std::size_t position, std::size_t length) const {
    ValidateRange(position, length);
    if (length == 0) {
        return {};
    }

    std::wstring text;
    text.reserve(length);

    std::size_t remaining = length;
    PiecePosition cursor = Locate(position);
    while (remaining > 0 && cursor.index < pieces_.size()) {
        const Piece& piece = pieces_[cursor.index];
        const std::size_t available = piece.length - cursor.offset;
        const std::size_t toCopy = std::min(remaining, available);
        const std::wstring& source = SourceText(piece.source);
        text.append(source, piece.start + cursor.offset, toCopy);
        remaining -= toCopy;
        ++cursor.index;
        cursor.offset = 0;
    }

    return text;
}

std::wstring DocumentBuffer::Text() const {
    std::wstring text;
    text.reserve(length_);

    for (const Piece& piece : pieces_) {
        const std::wstring& source = SourceText(piece.source);
        text.append(source, piece.start, piece.length);
    }

    return text;
}

const std::wstring& DocumentBuffer::SourceText(Source source) const noexcept {
    return source == Source::Original ? original_ : add_;
}

DocumentBuffer::PiecePosition DocumentBuffer::Locate(std::size_t position) const {
    if (position > length_) {
        throw std::out_of_range("position is outside the document");
    }

    std::size_t cursor = 0;
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        const Piece& piece = pieces_[i];
        if (position < cursor + piece.length) {
            return {i, position - cursor};
        }

        cursor += piece.length;
    }

    return {pieces_.size(), 0};
}

void DocumentBuffer::InsertPiece(std::size_t position, Piece newPiece) {
    PiecePosition target = Locate(position);
    if (target.index == pieces_.size()) {
        pieces_.push_back(newPiece);
        return;
    }

    Piece current = pieces_[target.index];
    auto insertAt = pieces_.begin() + static_cast<std::ptrdiff_t>(target.index);

    if (target.offset == 0) {
        pieces_.insert(insertAt, newPiece);
        return;
    }

    if (target.offset == current.length) {
        pieces_.insert(insertAt + 1, newPiece);
        return;
    }

    Piece left{current.source, current.start, target.offset};
    Piece right{current.source, current.start + target.offset, current.length - target.offset};

    *insertAt = left;
    pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(target.index + 1), newPiece);
    pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(target.index + 2), right);
}

void DocumentBuffer::NormalizePieces() {
    // Keep the table small: delete zero-length pieces and merge adjacent spans
    // that refer to contiguous text in the same backing string.
    pieces_.erase(
        std::remove_if(pieces_.begin(), pieces_.end(), [](const Piece& piece) { return piece.length == 0; }),
        pieces_.end());

    if (pieces_.size() < 2) {
        return;
    }

    std::vector<Piece> normalized;
    normalized.reserve(pieces_.size());

    for (const Piece& piece : pieces_) {
        if (!normalized.empty()) {
            Piece& previous = normalized.back();
            const bool canMerge = previous.source == piece.source && previous.start + previous.length == piece.start;
            if (canMerge) {
                previous.length += piece.length;
                continue;
            }
        }

        normalized.push_back(piece);
    }

    pieces_ = std::move(normalized);
}

void DocumentBuffer::ValidateRange(std::size_t position, std::size_t length) const {
    if (position > length_ || length > length_ - position) {
        throw std::out_of_range("range is outside the document");
    }
}

} // namespace NativePad
