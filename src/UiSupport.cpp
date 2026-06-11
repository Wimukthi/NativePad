#include "UiSupport.h"

#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>

#include "resource.h"

// Older Windows SDKs lack this attribute constant; current SDKs expose it as a
// DWMWINDOWATTRIBUTE enumerator. The fallback is defined after <dwmapi.h> so it
// never rewrites the enum declaration on SDKs that already provide it.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace NativePad {

namespace {

constexpr DWORD kDarkModeBefore20H1 = 19;

} // namespace

int ScaleForDpi(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi), USER_DEFAULT_SCREEN_DPI);
}

void EnableProcessDpiAwareness() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

HFONT CreateUiFontForDpi(UINT dpi) {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    BOOL loaded = SystemParametersInfoForDpi(
        SPI_GETNONCLIENTMETRICS,
        sizeof(metrics),
        &metrics,
        0,
        dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi);
    if (!loaded) {
        loaded = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    }

    HFONT font = loaded ? CreateFontIndirectW(&metrics.lfMessageFont) : nullptr;
    return font != nullptr ? font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

void DeleteUiFont(HFONT font) {
    if (font != nullptr && font != GetStockObject(DEFAULT_GUI_FONT)) {
        DeleteObject(font);
    }
}

HICON LoadNativePadIcon(HINSTANCE instance, int width, int height) {
    // The executable embeds a multi-size .ico; LoadImage lets Windows pick the
    // nearest resource for title bars, Alt+Tab, and shell surfaces.
    HICON icon = static_cast<HICON>(LoadImageW(
        instance,
        MAKEINTRESOURCEW(IDI_NATIVEPAD),
        IMAGE_ICON,
        width,
        height,
        LR_DEFAULTCOLOR | LR_SHARED));
    return icon != nullptr ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

void AssignWindowClassIcons(WNDCLASSEXW& wc, HINSTANCE instance) {
    wc.hIcon = LoadNativePadIcon(instance, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    wc.hIconSm = LoadNativePadIcon(instance, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
}

void ApplyWindowIcons(HWND hwnd, HINSTANCE instance) {
    // Dialog-style windows can otherwise inherit a generic small icon even when
    // the main frame uses the embedded resource, so set both icon slots.
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadNativePadIcon(instance, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON))));
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadNativePadIcon(instance, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON))));
}

ThemeColors ColorsForTheme(bool dark) {
    // Keep application chrome colors centralized so owner-draw menus, status bar,
    // dialogs, and editor background move together when dark mode changes.
    if (dark) {
        return {
            RGB(30, 30, 30),
            RGB(238, 238, 238),
            RGB(28, 28, 28),
            RGB(150, 150, 150),
            RGB(54, 54, 54),
            RGB(31, 31, 31),
            RGB(49, 49, 49),
            RGB(64, 64, 64),
            RGB(238, 238, 238),
            RGB(140, 140, 140),
            RGB(42, 42, 42),
            RGB(54, 54, 54),
            RGB(38, 38, 38),
            RGB(222, 222, 222),
        };
    }

    return {
        RGB(255, 255, 255),
        RGB(0, 0, 0),
        RGB(247, 247, 247),
        RGB(96, 96, 96),
        RGB(220, 220, 220),
        RGB(250, 250, 250),
        RGB(229, 241, 251),
        RGB(204, 228, 247),
        RGB(0, 0, 0),
        RGB(120, 120, 120),
        RGB(210, 210, 210),
        RGB(210, 210, 210),
        RGB(240, 240, 240),
        RGB(0, 0, 0),
    };
}

std::wstring GetLastErrorText(DWORD error) {
    if (error == ERROR_SUCCESS) {
        return L"No error.";
    }

    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstring message = length > 0 && buffer != nullptr ? std::wstring(buffer, length) : L"Unknown error.";
    if (buffer != nullptr) {
        LocalFree(buffer);
    }

    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
        message.pop_back();
    }

    return message;
}

bool IsSystemDarkMode() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);

    return status == ERROR_SUCCESS && value == 0;
}

void ApplyDarkFrame(HWND hwnd, bool dark) {
    BOOL enabled = dark ? TRUE : FALSE;
    HRESULT result = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled, sizeof(enabled));
    if (FAILED(result)) {
        DwmSetWindowAttribute(hwnd, kDarkModeBefore20H1, &enabled, sizeof(enabled));
    }
}

void ApplyDarkControlTheme(HWND hwnd, bool dark) {
    // DarkMode_Explorer is an undocumented but commonly used Windows theme name.
    // Owner-draw code still supplies exact colors where native theming falls short.
    if (hwnd == nullptr) {
        return;
    }

    SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

void SetControlFont(HWND control, HFONT font) {
    if (control != nullptr && font != nullptr) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

std::wstring ControlText(HWND control) {
    if (control == nullptr) {
        return {};
    }

    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }

    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

void SetControlText(HWND control, std::wstring_view text) {
    if (control != nullptr) {
        SetWindowTextW(control, std::wstring(text).c_str());
    }
}

DialogColors DialogColorsForTheme(bool dark) {
    // Custom dialogs share this palette so borders, edit controls, owner-draw
    // toggles, and list boxes do not drift into different dark-mode shades.
    if (dark) {
        return {
            RGB(31, 31, 31),
            RGB(238, 238, 238),
            RGB(30, 30, 30),
            RGB(55, 78, 112),
            RGB(255, 255, 255),
            RGB(150, 150, 150),
            RGB(62, 62, 62),
            RGB(78, 115, 158),
        };
    }

    return {
        RGB(240, 240, 240),
        RGB(0, 0, 0),
        RGB(255, 255, 255),
        RGB(0, 120, 215),
        RGB(255, 255, 255),
        RGB(120, 120, 120),
        RGB(170, 170, 170),
        RGB(0, 120, 215),
    };
}

void ApplyDialogControlTheme(HWND control, bool dark) {
    if (control != nullptr) {
        ApplyDarkControlTheme(control, dark);
    }
}

void MoveBorderedControl(HWND control, int x, int y, int width, int height) {
    // Parent windows draw the one-pixel border for dark-mode consistency. Child
    // edit/list controls are inset so their native client area never covers it.
    if (control == nullptr) {
        return;
    }

    constexpr int border = 1;
    MoveWindow(
        control,
        x + border,
        y + border,
        std::max(0, width - (border * 2)),
        std::max(0, height - (border * 2)),
        TRUE);
}

bool MessageTargetsWindow(HWND hwnd, const MSG& message) {
    return hwnd != nullptr && (message.hwnd == hwnd || IsChild(hwnd, message.hwnd));
}

void DrawControlBorder(HDC hdc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FrameRect(hdc, &rect, brush);
    DeleteObject(brush);
}

void DrawDialogChildBorder(HWND parent, HWND child, HDC hdc, const DialogColors& colors) {
    if (parent == nullptr || child == nullptr || hdc == nullptr) {
        return;
    }

    RECT rect{};
    if (!GetWindowRect(child, &rect)) {
        return;
    }
    MapWindowPoints(HWND_DESKTOP, parent, reinterpret_cast<POINT*>(&rect), 2);
    InflateRect(&rect, 1, 1);

    HWND focus = GetFocus();
    const bool focused = focus == child || IsChild(child, focus);
    DrawControlBorder(hdc, rect, focused ? colors.focusBorder : colors.border);
}

} // namespace NativePad
