#pragma once

#include <string>

// One place decides where NativePad keeps its own files: settings, the
// normal-exit tab session, crash-recovery journals, and update downloads.
//
// Installed copies use %LOCALAPPDATA%\NativePad. A portable copy is marked by a
// file named NativePad.portable beside NativePad.exe; it then keeps everything
// in a Data folder beside the executable, so copying the folder (for example to
// a USB stick) carries the settings and unsaved tabs with it. If that folder
// cannot be written, NativePad falls back to %LOCALAPPDATA%\NativePad and
// reports the fallback so the shell can tell the user.

namespace NativePad {

inline constexpr wchar_t kPortableMarkerFileName[] = L"NativePad.portable";
inline constexpr wchar_t kPortableDataFolderName[] = L"Data";

enum class DataLocation {
    LocalAppData,
    Portable,
};

struct DataRootInfo {
    // Folder that holds NativePad.ini, Session, Recovery, and Updates. Empty
    // only when neither location could be resolved.
    std::wstring root;
    DataLocation location{DataLocation::LocalAppData};
    // The marker was present but the portable folder was not writable.
    bool portableFallback{false};
    // The portable Data folder did not exist before this run.
    bool portableFolderCreated{false};
};

// Pure decision used by the process-wide resolver and by the tests.
// executableDirectory is the folder holding NativePad.exe; localAppDataRoot is
// the installed-mode folder (normally %LOCALAPPDATA%\NativePad).
[[nodiscard]] DataRootInfo ResolveDataRoot(const std::wstring& executableDirectory, const std::wstring& localAppDataRoot);

// Resolved once per process and cached, so every module agrees even if the
// marker or folders change while NativePad is running.
[[nodiscard]] const DataRootInfo& ApplicationDataRoot();

// root\child, created on demand. Empty if the folder cannot be created.
[[nodiscard]] std::wstring ApplicationDataDirectory(const wchar_t* child);

[[nodiscard]] bool IsPortableMode();

// The installed-mode folder, %LOCALAPPDATA%\NativePad (not created).
[[nodiscard]] std::wstring LocalAppDataRoot();

// Copies NativePad.ini and the saved Session folder from the installed-mode
// folder into the portable folder. Never deletes or moves the originals.
// Returns false if anything that existed could not be copied.
bool CopyLocalDataToPortable(const std::wstring& localAppDataRoot, const std::wstring& portableRoot);

// True when the installed-mode folder has settings or a saved session that a
// new portable folder could start from.
[[nodiscard]] bool LocalDataWorthCopying(const std::wstring& localAppDataRoot);

} // namespace NativePad
