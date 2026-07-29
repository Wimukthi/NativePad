#pragma once

#include <windows.h>

#include <string>
#include <string_view>

// Shared Win32 UI support used by the application shell and every custom dialog:
// DPI scaling, theme color palettes, dark-mode framing, and the small control
// helpers that keep NativePad-owned surfaces visually consistent. Feature modules
// include this header and pull the helpers in with `using namespace NativePad;`.

namespace NativePad {

// Application chrome colors. Centralized so owner-draw menus, status bar,
// dialogs, and the editor background move together when dark mode changes.
struct ThemeColors {
    COLORREF editorBackground;
    COLORREF editorText;
    COLORREF editorLineNumberBackground;
    COLORREF editorLineNumberText;
    COLORREF editorLineNumberSeparator;
    COLORREF menuBackground;
    COLORREF menuHot;
    COLORREF menuPressed;
    COLORREF menuText;
    COLORREF menuDisabledText;
    COLORREF menuBorder;
    COLORREF separator;
    COLORREF statusBackground;
    COLORREF statusText;
};

// Palette shared by custom dialog backgrounds and preview surfaces. The same
// values are supplied to the framework for its standard-control subclasses.
struct DialogColors {
    COLORREF background;
    COLORREF text;
    COLORREF controlBackground;
    COLORREF selectionBackground;
    COLORREF selectionText;
    COLORREF disabledText;
    COLORREF border;
    COLORREF focusBorder;
};

[[nodiscard]] int ScaleForDpi(int value, UINT dpi);
void EnableProcessDpiAwareness();
[[nodiscard]] HFONT CreateUiFontForDpi(UINT dpi);
void DeleteUiFont(HFONT font);
[[nodiscard]] HICON LoadNativePadIcon(HINSTANCE instance, int width, int height);
void AssignWindowClassIcons(WNDCLASSEXW& wc, HINSTANCE instance);
void ApplyWindowIcons(HWND hwnd, HINSTANCE instance);

[[nodiscard]] ThemeColors ColorsForTheme(bool dark);
[[nodiscard]] DialogColors DialogColorsForTheme(bool dark);

[[nodiscard]] std::wstring GetLastErrorText(DWORD error = GetLastError());
[[nodiscard]] bool IsSystemDarkMode();
[[nodiscard]] bool IsHighContrastMode();
[[nodiscard]] bool ConfigureNativeTheme(bool requestedDark, bool followSystem);
[[nodiscard]] bool IsNativeThemeDark();
bool HandleThemeSettingChange(LPARAM lparam);

void ApplyDarkFrame(HWND hwnd, bool dark);
void ApplyDarkControlTheme(HWND hwnd, bool dark);
void ApplyThemedDialog(HWND dialog);
void RefreshThemedDialog(HWND dialog);
int ShowThemedMessageBox(HWND owner, std::wstring_view text, std::wstring_view caption, UINT type);
void SetControlFont(HWND control, HFONT font);
[[nodiscard]] std::wstring ControlText(HWND control);
void SetControlText(HWND control, std::wstring_view text);
[[nodiscard]] bool MessageTargetsWindow(HWND hwnd, const MSG& message);
void CloseModalWindow(HWND dialog, HWND owner, HWND previousFocus);

} // namespace NativePad
