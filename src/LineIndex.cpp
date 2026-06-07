#include "LineIndex.h"

#include <algorithm>

namespace NativePad {

void LineIndex::Reset(std::wstring_view text) {
    // Store the start offset for each logical line. The first line always starts
    // at zero, including empty documents.
    starts_.clear();
    starts_.push_back(0);
    documentLength_ = text.size();
    maxLineLength_ = 0;

    std::size_t lineStart = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != L'\n') {
            continue;
        }

        std::size_t lineEnd = i;
        if (lineEnd > lineStart && text[lineEnd - 1] == L'\r') {
            --lineEnd;
        }
        maxLineLength_ = std::max(maxLineLength_, lineEnd - lineStart);
        starts_.push_back(i + 1);
        lineStart = i + 1;
    }

    maxLineLength_ = std::max(maxLineLength_, text.size() - lineStart);
}

void LineIndex::ApplyEdit(std::size_t position, std::wstring_view erased, std::wstring_view inserted) {
    // Incremental updates keep typing cheap: remove starts from the erased span,
    // shift starts after it, then splice in starts introduced by the inserted text.
    const std::size_t eraseEnd = position + erased.size();
    if (inserted.size() >= erased.size()) {
        documentLength_ += inserted.size() - erased.size();
    } else {
        documentLength_ -= erased.size() - inserted.size();
    }

    std::vector<std::size_t> updated;
    updated.reserve(starts_.size() + inserted.size());

    for (const std::size_t start : starts_) {
        if (start > position && start <= eraseEnd) {
            continue;
        }

        if (start > eraseEnd) {
            if (inserted.size() >= erased.size()) {
                updated.push_back(start + (inserted.size() - erased.size()));
            } else {
                updated.push_back(start - (erased.size() - inserted.size()));
            }
            continue;
        }

        updated.push_back(start);
    }

    std::vector<std::size_t> insertedStarts;
    insertedStarts.reserve(inserted.size());
    AppendLineStarts(insertedStarts, position, inserted);

    const auto insertAt = std::upper_bound(updated.begin(), updated.end(), position);
    updated.insert(insertAt, insertedStarts.begin(), insertedStarts.end());
    starts_ = std::move(updated);

    if (starts_.empty() || starts_.front() != 0) {
        starts_.insert(starts_.begin(), 0);
    }

    RecomputeApproximateMaxLineLength();
}

std::size_t LineIndex::LineCount() const noexcept {
    return starts_.size();
}

std::size_t LineIndex::LineStart(std::size_t line) const {
    if (starts_.empty()) {
        return 0;
    }

    return starts_[std::min(line, starts_.size() - 1)];
}

std::size_t LineIndex::LineFromPosition(std::size_t position) const {
    if (starts_.empty()) {
        return 0;
    }

    const auto it = std::upper_bound(starts_.begin(), starts_.end(), position);
    if (it == starts_.begin()) {
        return 0;
    }

    return static_cast<std::size_t>((it - starts_.begin()) - 1);
}

std::size_t LineIndex::MaxLineLength() const noexcept {
    return maxLineLength_;
}

void LineIndex::AppendLineStarts(std::vector<std::size_t>& starts, std::size_t basePosition, std::wstring_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\n') {
            starts.push_back(basePosition + i + 1);
        }
    }
}

void LineIndex::RecomputeApproximateMaxLineLength() {
    maxLineLength_ = 0;
    if (starts_.empty()) {
        return;
    }

    for (std::size_t i = 0; i < starts_.size(); ++i) {
        const std::size_t start = starts_[i];
        const std::size_t end = i + 1 < starts_.size() ? starts_[i + 1] : documentLength_;
        if (end >= start) {
            maxLineLength_ = std::max(maxLineLength_, end - start);
        }
    }
}

} // namespace NativePad
