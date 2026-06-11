#pragma once

#include <windows.h>

// Custom modal About box. It is a NativePad-owned window (not a resource dialog)
// so it can share the dark frame, app icon, and DPI-aware layout used elsewhere.

namespace NativePad {

void ShowAboutDialog(HWND owner, HINSTANCE instance, UINT dpi, bool dark);

} // namespace NativePad
