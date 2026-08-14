#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "TextFormat.h"

namespace NativePad {

enum class SessionDocumentKind : std::uint32_t {
    Normal,
    Mapped,
    LargeEditable,
};

struct SessionTabState {
    SessionDocumentKind kind{SessionDocumentKind::Normal};
    std::uint64_t untitledNumber{1};
    std::wstring currentPath;
    TextEncoding encoding{TextEncoding::Utf8};
    LineEnding lineEnding{LineEnding::CrLf};
    bool dirty{false};
    bool readOnlyPreview{false};
    bool followTail{false};

    // Normal dirty/untitled buffers are materialized here when loading. Dirty
    // large-file snapshots stay on disk and are mapped from this path.
    bool hasTextSnapshot{false};
    std::wstring text;
    std::wstring largeSnapshotPath;
};

struct SessionState {
    std::size_t activeIndex{0};
    std::vector<SessionTabState> tabs;
};

enum class SessionLoadStatus {
    NotFound,
    Loaded,
    Failed,
};

// Called only for a dirty editable-large-file tab. The writer materializes the
// tab's current bytes at the supplied session path.
using LargeSessionSnapshotWriter =
    std::function<bool(std::size_t tabIndex, const std::wstring& targetPath, std::wstring& error)>;

// Persists the normal-exit workspace independently of crash-recovery journals.
// A successfully loaded manifest is consumed, so explicitly closed tabs cannot
// reappear if the running process later crashes.
class SessionStore {
public:
    SessionStore();
    explicit SessionStore(std::wstring rootDirectory);

    [[nodiscard]] bool Save(
        const SessionState& state,
        const LargeSessionSnapshotWriter& largeWriter,
        std::wstring& error) const;
    [[nodiscard]] SessionLoadStatus LoadAndConsume(SessionState& state, std::wstring& error) const;
    void Clear() const noexcept;

    [[nodiscard]] const std::wstring& RootDirectory() const noexcept;
    [[nodiscard]] static std::wstring DefaultRootDirectory();

private:
    std::wstring rootDirectory_;
};

} // namespace NativePad
