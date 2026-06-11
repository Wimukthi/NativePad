#pragma once

#include <windows.h>

#include <string>

// Default-editor registration. Windows does not let an application silently
// force itself as the default handler for a file type (the per-extension choice
// is protected and tampering is reset by the OS). The supported flow is to
// register NativePad as a capable handler under the current user, then send the
// user to the Windows "Default apps" UI to confirm the choice. Everything here
// is per-user (no elevation) and reversible from Windows Settings.

namespace NativePad {

// Registers NativePad as a per-user handler for .txt and opens the Windows
// "Default apps" UI for confirmation. Returns false (with error) only when the
// registration itself fails; opening the Settings UI is best-effort.
bool PromptSetDefaultEditor(HWND owner, HINSTANCE instance, std::wstring& error);

// True when NativePad is the current effective default handler for .txt.
[[nodiscard]] bool IsDefaultEditor();

} // namespace NativePad
