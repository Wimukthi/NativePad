#pragma once

#include <windows.h>

#include <string>
#include <string_view>

// Shared Win32 UI support used by the application shell and every custom dialog:
// DPI scaling, theme color palettes, dark-mode framing, and the small control
// helpers that keep owner-draw dialogs visually consistent. Feature modules
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

// Palette shared by the custom dialogs so borders, edit controls, owner-draw
// toggles, and list boxes do not drift into different dark-mode shades.
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

void ApplyDarkFrame(HWND hwnd, bool dark);
void ApplyDarkControlTheme(HWND hwnd, bool dark);
void ApplyDialogControlTheme(HWND control, bool dark);
void SetControlFont(HWND control, HFONT font);
[[nodiscard]] std::wstring ControlText(HWND control);
void SetControlText(HWND control, std::wstring_view text);
void MoveBorderedControl(HWND control, int x, int y, int width, int height);
[[nodiscard]] bool MessageTargetsWindow(HWND hwnd, const MSG& message);
void DrawControlBorder(HDC hdc, RECT rect, COLORREF color);
void DrawDialogChildBorder(HWND parent, HWND child, HDC hdc, const DialogColors& colors);

} // namespace NativePad
