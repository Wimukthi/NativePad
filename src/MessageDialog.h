#pragma once

#include <windows.h>

#include <string_view>

// Custom modal replacement for app-owned MessageBoxW prompts. It preserves the
// usual Win32 button result IDs while using NativePad's DPI and dark-mode theme.

namespace NativePad {

enum class MessageDialogIcon {
    None,
    Information,
    Warning,
    Error,
    Question,
};

enum class MessageDialogButtons {
    Ok,
    OkCancel,
    YesNo,
    YesNoCancel,
};

int ShowMessageDialog(
    HWND owner,
    HINSTANCE instance,
    UINT dpi,
    bool dark,
    std::wstring_view title,
    std::wstring_view message,
    MessageDialogIcon icon = MessageDialogIcon::Information,
    MessageDialogButtons buttons = MessageDialogButtons::Ok,
    int defaultResult = IDOK);

} // namespace NativePad
