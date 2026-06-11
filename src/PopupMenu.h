#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Owner-drawn dark-mode popup menu used for the main window's menu bar and
// context menus, plus the soft drop shadow behind it. The application window
// owns the menu data and lifetime; this module supplies the window classes,
// window procedures, item model, and shadow rendering.

namespace NativePad {

constexpr wchar_t kPopupMenuClass[] = L"NativePadPopupMenu";
constexpr wchar_t kPopupShadowClass[] = L"NativePadPopupShadow";

struct MenuEntry {
    const wchar_t* text;
    HMENU menu;
    RECT rect;
};

struct PopupMenuItem {
    UINT id{};
    RECT rect{};
    std::wstring label;
    std::wstring shortcut;
    bool separator{false};
    bool enabled{true};
    bool checked{false};
};

struct PopupMenuWindowState {
    HWND hwnd{};
    HWND owner{};
    HWND shadow{};
    UINT dpi{USER_DEFAULT_SCREEN_DPI};
    HFONT font{};
    bool dark{false};
    std::vector<PopupMenuItem> items;
    int hotIndex{-1};
    UINT command{};
};

LRESULT CALLBACK CustomPopupMenuProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK PopupShadowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

[[nodiscard]] HWND CreatePopupShadowWindow(HWND owner, HINSTANCE instance, POINT popupPosition, SIZE popupSize, UINT dpi);
bool RouteCustomPopupKey(HWND popup, const MSG& message);

} // namespace NativePad
