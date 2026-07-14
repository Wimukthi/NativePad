#pragma once

#include <windows.h>

#include <optional>
#include <string>

#include "TextFormat.h"

// Crash-recovery journal. While a document has unsaved changes, the shell
// periodically snapshots it into %LOCALAPPDATA%\NativePad\Recovery. On a clean
// save, discard, or exit the journal is deleted. On startup, journals whose
// owning process is no longer running are offered for restore.

namespace NativePad {

struct RecoverySnapshot {
    std::wstring originalPath; // Empty for an Untitled document.
    TextEncoding encoding{TextEncoding::Utf8};
    LineEnding lineEnding{LineEnding::CrLf};
    std::wstring text;
};

class RecoveryJournal {
public:
    // Journals for this process under the default recovery directory.
    RecoveryJournal();

    // Custom root and owning process id, used by tests to simulate journals
    // left behind by a crashed process.
    RecoveryJournal(std::wstring rootDirectory, DWORD processId);

    RecoveryJournal(const RecoveryJournal&) = delete;
    RecoveryJournal& operator=(const RecoveryJournal&) = delete;

    // Writes or replaces this process's journal. The content file lands before
    // the metadata file so a journal is only discoverable once it is complete.
    bool Save(const RecoverySnapshot& snapshot);

    // Deletes this process's journal, if any.
    void Clear() noexcept;

    [[nodiscard]] static std::wstring DefaultRootDirectory();

    // Finds one journal owned by a process that is no longer running, removes
    // it from disk, and returns its snapshot. Corrupt leftovers are cleaned up
    // during the scan. Returns nullopt when nothing is recoverable.
    [[nodiscard]] static std::optional<RecoverySnapshot> ClaimAbandoned(const std::wstring& rootDirectory);

private:
    [[nodiscard]] std::wstring MetaPath() const;
    [[nodiscard]] std::wstring ContentPath() const;

    std::wstring rootDirectory_;
    DWORD processId_{0};
};

} // namespace NativePad
