#pragma once

#include <windows.h>

#include <string>

// Update checks run on worker threads and report back to the UI thread through
// private window messages. The UI owns prompting, dirty-document handling, and
// launching the elevated installer; this module only handles release discovery,
// download, hash verification, and update-check preferences.

namespace NativePad {

constexpr UINT WM_NATIVEPAD_UPDATE_CHECK_COMPLETE = WM_APP + 304;
constexpr UINT WM_NATIVEPAD_UPDATE_DOWNLOAD_COMPLETE = WM_APP + 305;

enum class UpdateCheckKind {
    Manual,
    Automatic,
};

struct UpdateInfo {
    std::wstring version;
    std::wstring assetName;
    std::wstring downloadUrl;
    std::wstring digest;
    std::wstring releaseUrl;
};

struct UpdateCheckResult {
    UpdateCheckKind kind{UpdateCheckKind::Manual};
    bool success{false};
    bool updateAvailable{false};
    UpdateInfo update;
    std::wstring message;
};

struct UpdateDownloadResult {
    bool success{false};
    UpdateInfo update;
    std::wstring installerPath;
    std::wstring message;
};

[[nodiscard]] bool AutomaticUpdateChecksEnabled();
void SetAutomaticUpdateChecksEnabled(bool enabled);
[[nodiscard]] bool AutomaticUpdateCheckDue();

[[nodiscard]] std::wstring CurrentExecutableVersion(HINSTANCE instance);
void StartUpdateCheck(HWND notifyWindow, HINSTANCE instance, UpdateCheckKind kind);
void StartUpdateDownload(HWND notifyWindow, UpdateInfo update);

} // namespace NativePad
