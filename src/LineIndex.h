#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace NativePad {

// Maps document offsets to line starts for the editor's scroll, hit-test, and
// status-bar paths. Normal editable documents keep this index outside the piece
// table so common caret operations stay O(log line count).
class LineIndex {
public:
    void Reset(std::wstring_view text);
    void ApplyEdit(std::size_t position, std::wstring_view erased, std::wstring_view inserted);

    [[nodiscard]] std::size_t LineCount() const noexcept;
    [[nodiscard]] std::size_t LineStart(std::size_t line) const;
    [[nodiscard]] std::size_t LineFromPosition(std::size_t position) const;
    [[nodiscard]] std::size_t MaxLineLength() const noexcept;

private:
    // maxLineLength_ is used as the horizontal scrollbar upper bound. After
    // incremental edits it may be conservative, which is acceptable for scroll UI.
    static void AppendLineStarts(std::vector<std::size_t>& starts, std::size_t basePosition, std::wstring_view text);
    void RecomputeApproximateMaxLineLength();

    std::vector<std::size_t> starts_{0};
    std::size_t documentLength_{0};
    std::size_t maxLineLength_{0};
};

} // namespace NativePad
