#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <strsafe.h>
#include <uxtheme.h>
#include <winver.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "DocumentBuffer.h"
#include "EditorView.h"
#include "MappedTextDocument.h"
#include "resource.h"
#include "TextFormat.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

namespace {

// main.cpp contains the Win32 shell around the editor: menus, dialogs, file I/O,
// printing, theming, DPI/layout, and application lifetime. The editor control
// itself lives in EditorView so painting and text navigation remain isolated.
constexpr wchar_t kWindowClass[] = L"NativePadWindow";
constexpr wchar_t kMenuStripClass[] = L"NativePadMenuStrip";
constexpr wchar_t kPopupMenuClass[] = L"NativePadPopupMenu";
constexpr wchar_t kPopupShadowClass[] = L"NativePadPopupShadow";
constexpr wchar_t kAboutDialogClass[] = L"NativePadAboutDialog";
constexpr wchar_t kGoToDialogClass[] = L"NativePadGoToDialog";
constexpr wchar_t kFindReplaceDialogClass[] = L"NativePadFindReplaceDialog";
constexpr wchar_t kFontDialogClass[] = L"NativePadFontDialog";
constexpr wchar_t kUntitled[] = L"Untitled";
constexpr wchar_t kSettingsKey[] = L"Software\\NativePad";
constexpr wchar_t kNativePadFallbackVersion[] = L"1.0.0.0";
constexpr wchar_t kNativePadAuthor[] = L"Wimukthi Bandara";
constexpr wchar_t kNativePadLicense[] = L"GPL V3";
constexpr DWORD kDarkModeBefore20H1 = 19;
constexpr DWORD kReadChunkLimit = 512u * 1024u * 1024u;
constexpr size_t kLargeFilePreviewBytes = 16u * 1024u * 1024u;
constexpr int kGoToEditId = 50001;
constexpr int kFindTextEditId = 50101;
constexpr int kReplaceTextEditId = 50102;
constexpr int kFindMatchCaseId = 50103;
constexpr int kFindDirectionUpId = 50104;
constexpr int kFindDirectionDownId = 50105;
constexpr int kFindReplaceButtonId = 50106;
constexpr int kFindReplaceAllButtonId = 50107;
constexpr int kFontFamilyEditId = 50201;
constexpr int kFontFamilyListId = 50202;
constexpr int kFontStyleEditId = 50203;
constexpr int kFontStyleListId = 50204;
constexpr int kFontSizeEditId = 50205;
constexpr int kFontSizeListId = 50206;
constexpr int kFontPreviewId = 50207;
constexpr DWORD kSaveEncodingComboId = 50301;
constexpr UINT WM_NATIVEPAD_PRINT_COMPLETE = WM_APP + 302;
constexpr UINT WM_NATIVEPAD_FIND_REPLACE = WM_APP + 303;

#define NATIVEPAD_WIDEN2(value) L##value
#define NATIVEPAD_WIDEN(value) NATIVEPAD_WIDEN2(value)
constexpr wchar_t kNativePadBuildTimestamp[] = NATIVEPAD_WIDEN(__DATE__) L" " NATIVEPAD_WIDEN(__TIME__);
#undef NATIVEPAD_WIDEN
#undef NATIVEPAD_WIDEN2

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

struct SaveEncodingOption {
    DWORD id{};
    NativePad::TextEncoding encoding{NativePad::TextEncoding::Utf8};
    const wchar_t* label{};
};

constexpr std::array<SaveEncodingOption, 5> kSaveEncodingOptions{{
    {1, NativePad::TextEncoding::Utf8, L"UTF-8"},
    {2, NativePad::TextEncoding::Utf8Bom, L"UTF-8 with BOM"},
    {3, NativePad::TextEncoding::Utf16Le, L"UTF-16 LE"},
    {4, NativePad::TextEncoding::Utf16Be, L"UTF-16 BE"},
    {5, NativePad::TextEncoding::Ansi, L"ANSI"},
}};

const SaveEncodingOption& SaveEncodingOptionForEncoding(NativePad::TextEncoding encoding) noexcept {
    for (const auto& option : kSaveEncodingOptions) {
        if (option.encoding == encoding) {
            return option;
        }
    }

    return kSaveEncodingOptions.front();
}

const SaveEncodingOption& SaveEncodingOptionForId(DWORD id) noexcept {
    for (const auto& option : kSaveEncodingOptions) {
        if (option.id == id) {
            return option;
        }
    }

    return kSaveEncodingOptions.front();
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

struct PopupShadowMetrics {
    int margin{};
    int offsetX{};
    int offsetY{};
    int radius{};
    BYTE maxAlpha{};
    SIZE windowSize{};
    RECT popupRect{};
    RECT casterRect{};
};

int PopupScale(const PopupMenuWindowState* state, int value) {
    return ScaleForDpi(value, state != nullptr ? state->dpi : USER_DEFAULT_SCREEN_DPI);
}

PopupShadowMetrics PopupShadowMetricsForDpi(SIZE popupSize, UINT dpi) {
    PopupShadowMetrics metrics{};
    metrics.radius = ScaleForDpi(14, dpi);
    metrics.margin = ScaleForDpi(12, dpi);
    metrics.offsetX = ScaleForDpi(2, dpi);
    metrics.offsetY = ScaleForDpi(4, dpi);
    metrics.maxAlpha = 58;
    metrics.windowSize = {
        popupSize.cx + (metrics.margin * 2) + metrics.offsetX,
        popupSize.cy + (metrics.margin * 2) + metrics.offsetY,
    };
    metrics.popupRect = {
        metrics.margin,
        metrics.margin,
        metrics.margin + popupSize.cx,
        metrics.margin + popupSize.cy,
    };
    metrics.casterRect = {
        metrics.margin + metrics.offsetX,
        metrics.margin + metrics.offsetY,
        metrics.margin + metrics.offsetX + popupSize.cx,
        metrics.margin + metrics.offsetY + popupSize.cy,
    };
    return metrics;
}

BYTE PopupShadowAlphaForPixel(const PopupShadowMetrics& metrics, int x, int y) {
    if (x >= metrics.popupRect.left && x < metrics.popupRect.right && y >= metrics.popupRect.top && y < metrics.popupRect.bottom) {
        return 0;
    }

    const int dx = std::max(
        std::max(static_cast<int>(metrics.casterRect.left) - x, 0),
        x - static_cast<int>(metrics.casterRect.right - 1));
    const int dy = std::max(
        std::max(static_cast<int>(metrics.casterRect.top) - y, 0),
        y - static_cast<int>(metrics.casterRect.bottom - 1));
    const double distance = std::sqrt(static_cast<double>((dx * dx) + (dy * dy)));
    if (distance > static_cast<double>(metrics.radius)) {
        return 0;
    }

    const double falloff = 1.0 - (distance / static_cast<double>(metrics.radius));
    return static_cast<BYTE>(static_cast<double>(metrics.maxAlpha) * falloff * falloff);
}

int HitTestPopupMenuItem(const PopupMenuWindowState* state, POINT point) {
    if (state == nullptr) {
        return -1;
    }

    for (size_t i = 0; i < state->items.size(); ++i) {
        const PopupMenuItem& item = state->items[i];
        if (!item.separator && PtInRect(&item.rect, point)) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

bool IsPointInsideWindowClient(HWND hwnd, POINT point) {
    RECT client{};
    return hwnd != nullptr && GetClientRect(hwnd, &client) && PtInRect(&client, point);
}

HWND PopupContextTargetAtScreenPoint(const PopupMenuWindowState* state, POINT screenPoint) {
    if (state == nullptr || state->owner == nullptr) {
        return nullptr;
    }

    HWND target = WindowFromPoint(screenPoint);
    if (target != nullptr &&
        target != state->hwnd &&
        target != state->shadow &&
        (target == state->owner || IsChild(state->owner, target))) {
        return target;
    }

    POINT ownerPoint = screenPoint;
    if (!ScreenToClient(state->owner, &ownerPoint) || !IsPointInsideWindowClient(state->owner, ownerPoint)) {
        return nullptr;
    }

    // Mouse capture sends outside clicks to the popup. Re-resolve inside the
    // owner so a right-click over the editor still follows the normal
    // WM_CONTEXTMENU route after the open top-level menu is dismissed.
    HWND child = ChildWindowFromPointEx(state->owner, ownerPoint, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
    return child != nullptr ? child : state->owner;
}

void PostPopupContextMenu(PopupMenuWindowState* state, POINT popupClientPoint) {
    if (state == nullptr || state->hwnd == nullptr || IsPointInsideWindowClient(state->hwnd, popupClientPoint)) {
        return;
    }

    POINT screenPoint = popupClientPoint;
    ClientToScreen(state->hwnd, &screenPoint);
    HWND target = PopupContextTargetAtScreenPoint(state, screenPoint);
    if (target == nullptr) {
        return;
    }

    const LPARAM packedPoint = MAKELPARAM(screenPoint.x, screenPoint.y);
    PostMessageW(state->owner, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(target), packedPoint);
}

void SelectAdjacentPopupItem(PopupMenuWindowState* state, int direction) {
    if (state == nullptr || state->items.empty()) {
        return;
    }

    int index = state->hotIndex;
    for (size_t attempts = 0; attempts < state->items.size(); ++attempts) {
        index += direction;
        if (index < 0) {
            index = static_cast<int>(state->items.size()) - 1;
        } else if (index >= static_cast<int>(state->items.size())) {
            index = 0;
        }

        if (!state->items[static_cast<size_t>(index)].separator) {
            state->hotIndex = index;
            InvalidateRect(state->hwnd, nullptr, FALSE);
            return;
        }
    }
}

void PaintCustomPopupMenu(PopupMenuWindowState* state) {
    if (state == nullptr || state->hwnd == nullptr) {
        return;
    }

    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(state->hwnd, &ps);
    if (hdc == nullptr) {
        return;
    }

    const ThemeColors colors = ColorsForTheme(state->dark);
    RECT client{};
    GetClientRect(state->hwnd, &client);

    HBRUSH border = CreateSolidBrush(colors.menuBorder);
    FillRect(hdc, &client, border);
    DeleteObject(border);

    RECT body = client;
    InflateRect(&body, -1, -1);
    HBRUSH background = CreateSolidBrush(colors.menuBackground);
    FillRect(hdc, &body, background);
    DeleteObject(background);

    HFONT font = state->font != nullptr ? state->font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);

    for (size_t i = 0; i < state->items.size(); ++i) {
        const PopupMenuItem& item = state->items[i];
        if (item.separator) {
            const int midY = item.rect.top + ((item.rect.bottom - item.rect.top) / 2);
            HPEN separatorPen = CreatePen(PS_SOLID, 1, colors.separator);
            HGDIOBJ oldPen = SelectObject(hdc, separatorPen);
            MoveToEx(hdc, item.rect.left + PopupScale(state, 8), midY, nullptr);
            LineTo(hdc, item.rect.right - PopupScale(state, 8), midY);
            SelectObject(hdc, oldPen);
            DeleteObject(separatorPen);
            continue;
        }

        const bool selected = static_cast<int>(i) == state->hotIndex && item.enabled;
        const COLORREF itemBackground = selected ? colors.menuHot : colors.menuBackground;
        HBRUSH itemBrush = CreateSolidBrush(itemBackground);
        FillRect(hdc, &item.rect, itemBrush);
        DeleteObject(itemBrush);

        SetTextColor(hdc, item.enabled ? colors.menuText : colors.menuDisabledText);
        if (item.checked) {
            HPEN checkPen = CreatePen(PS_SOLID, PopupScale(state, 2), item.enabled ? colors.menuText : colors.menuDisabledText);
            HGDIOBJ oldPen = SelectObject(hdc, checkPen);
            const int left = item.rect.left + PopupScale(state, 9);
            const int midY = item.rect.top + ((item.rect.bottom - item.rect.top) / 2);
            MoveToEx(hdc, left, midY, nullptr);
            LineTo(hdc, left + PopupScale(state, 4), midY + PopupScale(state, 4));
            LineTo(hdc, left + PopupScale(state, 12), midY - PopupScale(state, 5));
            SelectObject(hdc, oldPen);
            DeleteObject(checkPen);
        }

        RECT labelRect = item.rect;
        labelRect.left += PopupScale(state, 30);
        labelRect.right -= PopupScale(state, 14);

        RECT shortcutRect = labelRect;
        shortcutRect.left += PopupScale(state, 40);

        DrawTextW(hdc, item.label.c_str(), -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (!item.shortcut.empty()) {
            DrawTextW(hdc, item.shortcut.c_str(), -1, &shortcutRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    SelectObject(hdc, oldFont);
    EndPaint(state->hwnd, &ps);
}

LRESULT CALLBACK PopupShadowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }
    if (message == WM_MOUSEACTIVATE) {
        return MA_NOACTIVATE;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool RegisterShadowWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &PopupShadowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPopupShadowClass;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool UpdatePopupShadowBitmap(HWND shadow, SIZE popupSize, UINT dpi) {
    if (shadow == nullptr || popupSize.cx <= 0 || popupSize.cy <= 0) {
        return false;
    }

    const PopupShadowMetrics metrics = PopupShadowMetricsForDpi(popupSize, dpi);
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = metrics.windowSize.cx;
    bitmapInfo.bmiHeader.biHeight = -metrics.windowSize.cy;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr) {
        return false;
    }

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr || bits == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    auto* pixels = static_cast<std::uint32_t*>(bits);
    for (int y = 0; y < metrics.windowSize.cy; ++y) {
        for (int x = 0; x < metrics.windowSize.cx; ++x) {
            const BYTE alpha = PopupShadowAlphaForPixel(metrics, x, y);
            pixels[(static_cast<size_t>(y) * static_cast<size_t>(metrics.windowSize.cx)) + static_cast<size_t>(x)] =
                static_cast<std::uint32_t>(alpha) << 24;
        }
    }

    HDC memoryDc = CreateCompatibleDC(screenDc);
    if (memoryDc == nullptr) {
        DeleteObject(bitmap);
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
    POINT source{0, 0};
    SIZE size = metrics.windowSize;
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    const BOOL updated = UpdateLayeredWindow(shadow, screenDc, nullptr, &size, memoryDc, &source, 0, &blend, ULW_ALPHA);

    SelectObject(memoryDc, oldBitmap);
    DeleteDC(memoryDc);
    DeleteObject(bitmap);
    ReleaseDC(nullptr, screenDc);
    return updated != FALSE;
}

HWND CreateShadowWindow(HWND owner, HINSTANCE instance, POINT targetPosition, SIZE targetSize, UINT dpi, bool topmost) {
    if (!RegisterShadowWindowClass(instance)) {
        return nullptr;
    }

    const PopupShadowMetrics metrics = PopupShadowMetricsForDpi(targetSize, dpi);
    DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT;
    if (topmost) {
        exStyle |= WS_EX_TOPMOST;
    }

    HWND shadow = CreateWindowExW(
        exStyle,
        kPopupShadowClass,
        nullptr,
        WS_POPUP,
        targetPosition.x - metrics.margin,
        targetPosition.y - metrics.margin,
        metrics.windowSize.cx,
        metrics.windowSize.cy,
        owner,
        nullptr,
        instance,
        nullptr);
    if (shadow == nullptr) {
        return nullptr;
    }

    if (!UpdatePopupShadowBitmap(shadow, targetSize, dpi)) {
        DestroyWindow(shadow);
        return nullptr;
    }

    ShowWindow(shadow, SW_SHOWNOACTIVATE);
    return shadow;
}

HWND CreatePopupShadowWindow(HWND owner, HINSTANCE instance, POINT popupPosition, SIZE popupSize, UINT dpi) {
    return CreateShadowWindow(owner, instance, popupPosition, popupSize, dpi, true);
}

LRESULT CALLBACK CustomPopupMenuProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<PopupMenuWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<PopupMenuWindowState*>(create->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_PAINT:
        PaintCustomPopupMenu(state);
        return 0;
    case WM_MOUSEMOVE: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int hit = HitTestPopupMenuItem(state, point);
        if (state != nullptr && hit != state->hotIndex) {
            state->hotIndex = hit;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int hit = HitTestPopupMenuItem(state, point);
        if (state != nullptr && hit >= 0) {
            const PopupMenuItem& item = state->items[static_cast<size_t>(hit)];
            if (item.enabled) {
                state->command = item.id;
            }
        }
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (state == nullptr || HitTestPopupMenuItem(state, point) < 0) {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (!IsPointInsideWindowClient(hwnd, point)) {
            PostPopupContextMenu(state, point);
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_RBUTTONDOWN:
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (wParam == VK_UP) {
            SelectAdjacentPopupItem(state, -1);
            return 0;
        }
        if (wParam == VK_DOWN) {
            SelectAdjacentPopupItem(state, 1);
            return 0;
        }
        if (wParam == VK_RETURN && state != nullptr && state->hotIndex >= 0) {
            const PopupMenuItem& item = state->items[static_cast<size_t>(state->hotIndex)];
            if (item.enabled) {
                state->command = item.id;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        break;
    case WM_CANCELMODE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (state != nullptr && state->shadow != nullptr) {
            DestroyWindow(state->shadow);
            state->shadow = nullptr;
        }
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool RouteCustomPopupKey(HWND popup, const MSG& message) {
    if (popup == nullptr || (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN)) {
        return false;
    }

    switch (message.wParam) {
    case VK_ESCAPE:
    case VK_UP:
    case VK_DOWN:
    case VK_RETURN:
        SendMessageW(popup, WM_KEYDOWN, message.wParam, message.lParam);
        return true;
    default:
        return false;
    }
}

std::wstring GetLastErrorText(DWORD error = GetLastError()) {
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

std::wstring ModulePath(HINSTANCE instance) {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(instance, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size() - 1) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::wstring FormatVersionNumber(DWORD mostSignificant, DWORD leastSignificant) {
    std::wstring version = std::to_wstring(HIWORD(mostSignificant));
    version += L".";
    version += std::to_wstring(LOWORD(mostSignificant));
    version += L".";
    version += std::to_wstring(HIWORD(leastSignificant));
    version += L".";
    version += std::to_wstring(LOWORD(leastSignificant));
    return version;
}

std::wstring ExecutableVersionText(HINSTANCE instance) {
    // About uses the executable resource as the single version source so the
    // displayed version, file properties, and release artifacts stay aligned.
    const std::wstring path = ModulePath(instance);
    if (path.empty()) {
        return kNativePadFallbackVersion;
    }

    DWORD ignoredHandle = 0;
    const DWORD infoSize = GetFileVersionInfoSizeW(path.c_str(), &ignoredHandle);
    if (infoSize == 0) {
        return kNativePadFallbackVersion;
    }

    std::vector<BYTE> versionInfo(infoSize);
    if (!GetFileVersionInfoW(path.c_str(), 0, infoSize, versionInfo.data())) {
        return kNativePadFallbackVersion;
    }

    VS_FIXEDFILEINFO* fixedInfo = nullptr;
    UINT fixedInfoSize = 0;
    if (!VerQueryValueW(versionInfo.data(), L"\\", reinterpret_cast<LPVOID*>(&fixedInfo), &fixedInfoSize) ||
        fixedInfo == nullptr ||
        fixedInfoSize < sizeof(VS_FIXEDFILEINFO) ||
        fixedInfo->dwSignature != 0xfeef04bd) {
        return kNativePadFallbackVersion;
    }

    return FormatVersionNumber(fixedInfo->dwFileVersionMS, fixedInfo->dwFileVersionLS);
}

std::wstring BaseName(std::wstring_view path) {
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring_view::npos) {
        return std::wstring(path);
    }

    return std::wstring(path.substr(pos + 1));
}

bool TextMatchesAt(std::wstring_view text, std::wstring_view needle, size_t position, bool matchCase) {
    if (position + needle.size() > text.size()) {
        return false;
    }

    for (size_t i = 0; i < needle.size(); ++i) {
        wchar_t left = text[position + i];
        wchar_t right = needle[i];
        if (!matchCase) {
            left = static_cast<wchar_t>(std::towlower(left));
            right = static_cast<wchar_t>(std::towlower(right));
        }
        if (left != right) {
            return false;
        }
    }

    return true;
}

std::optional<size_t> FindForward(std::wstring_view text, std::wstring_view needle, size_t start, bool matchCase) {
    if (needle.empty() || needle.size() > text.size()) {
        return std::nullopt;
    }

    const size_t last = text.size() - needle.size();
    for (size_t position = std::min(start, text.size()); position <= last; ++position) {
        if (TextMatchesAt(text, needle, position, matchCase)) {
            return position;
        }
    }

    return std::nullopt;
}

std::optional<size_t> FindBackward(std::wstring_view text, std::wstring_view needle, size_t startExclusive, bool matchCase) {
    if (needle.empty() || needle.size() > text.size()) {
        return std::nullopt;
    }

    size_t position = std::min(startExclusive, text.size() - needle.size() + 1);
    while (position > 0) {
        --position;
        if (TextMatchesAt(text, needle, position, matchCase)) {
            return position;
        }
    }

    return std::nullopt;
}

std::wstring UserDateTimeText() {
    SYSTEMTIME now{};
    GetLocalTime(&now);

    wchar_t time[128]{};
    wchar_t date[128]{};
    const int timeLength = GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &now, nullptr, time, static_cast<int>(std::size(time)));
    const int dateLength = GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &now, nullptr, date, static_cast<int>(std::size(date)), nullptr);
    if (timeLength > 0 && dateLength > 0) {
        std::wstring result = time;
        result += L" ";
        result += date;
        return result;
    }

    wchar_t fallback[64]{};
    StringCchPrintfW(
        fallback,
        std::size(fallback),
        L"%02u:%02u %u/%u/%u",
        now.wHour,
        now.wMinute,
        now.wMonth,
        now.wDay,
        now.wYear);
    return fallback;
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

std::optional<DWORD> ReadSettingsDword(const wchar_t* name) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        kSettingsKey,
        name,
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);

    if (status != ERROR_SUCCESS || size != sizeof(value)) {
        return std::nullopt;
    }

    return value;
}

std::optional<int> ReadSettingsInt(const wchar_t* name) {
    auto value = ReadSettingsDword(name);
    if (!value) {
        return std::nullopt;
    }
    return static_cast<int>(static_cast<LONG>(*value));
}

std::optional<std::wstring> ReadSettingsString(const wchar_t* name) {
    DWORD byteCount = 0;
    LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        kSettingsKey,
        name,
        RRF_RT_REG_SZ,
        nullptr,
        nullptr,
        &byteCount);

    if (status != ERROR_SUCCESS || byteCount < sizeof(wchar_t)) {
        return std::nullopt;
    }

    std::wstring value(byteCount / sizeof(wchar_t), L'\0');
    status = RegGetValueW(
        HKEY_CURRENT_USER,
        kSettingsKey,
        name,
        RRF_RT_REG_SZ,
        nullptr,
        value.data(),
        &byteCount);

    if (status != ERROR_SUCCESS) {
        return std::nullopt;
    }

    value.resize(byteCount / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

bool CreateSettingsKey(HKEY& key) {
    key = nullptr;
    return RegCreateKeyExW(
               HKEY_CURRENT_USER,
               kSettingsKey,
               0,
               nullptr,
               REG_OPTION_NON_VOLATILE,
               KEY_SET_VALUE,
               nullptr,
               &key,
               nullptr) == ERROR_SUCCESS;
}

void WriteSettingsDword(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

void WriteSettingsInt(HKEY key, const wchar_t* name, int value) {
    WriteSettingsDword(key, name, static_cast<DWORD>(static_cast<LONG>(value)));
}

void WriteSettingsString(HKEY key, const wchar_t* name, const std::wstring& value) {
    const DWORD byteCount = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), byteCount);
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

std::optional<std::wstring> MultiByteToWide(UINT codePage, const char* data, int byteCount, DWORD flags) {
    if (byteCount == 0) {
        return std::wstring();
    }

    const int required = MultiByteToWideChar(codePage, flags, data, byteCount, nullptr, 0);
    if (required <= 0) {
        return std::nullopt;
    }

    std::wstring text(static_cast<size_t>(required), L'\0');
    const int written = MultiByteToWideChar(codePage, flags, data, byteCount, text.data(), required);
    if (written <= 0) {
        return std::nullopt;
    }

    return text;
}

std::wstring Utf16FromLeBytes(const char* bytes, size_t size, size_t offset) {
    const size_t byteCount = size - offset;
    const size_t wcharCount = byteCount / sizeof(wchar_t);
    std::wstring text(wcharCount, L'\0');
    if (wcharCount > 0) {
        memcpy(text.data(), bytes + offset, wcharCount * sizeof(wchar_t));
    }
    return text;
}

std::wstring Utf16FromBeBytes(const char* bytes, size_t size, size_t offset) {
    const size_t byteCount = size - offset;
    const size_t wcharCount = byteCount / sizeof(wchar_t);
    std::wstring text;
    text.reserve(wcharCount);

    for (size_t i = 0; i + 1 < byteCount; i += 2) {
        const auto high = static_cast<unsigned char>(bytes[offset + i]);
        const auto low = static_cast<unsigned char>(bytes[offset + i + 1]);
        text.push_back(static_cast<wchar_t>((high << 8) | low));
    }

    return text;
}

struct DecodedFile {
    std::wstring text;
    std::wstring encodingLabel;
    NativePad::TextEncoding encoding{NativePad::TextEncoding::Utf8};
    NativePad::LineEnding lineEnding{NativePad::LineEnding::CrLf};
    uint64_t fileByteCount{0};
    size_t decodedByteCount{0};
    bool readOnlyPreview{false};
    bool truncated{false};
};

DecodedFile MakeDecodedFile(std::wstring text, NativePad::TextEncoding encoding, size_t decodedByteCount) {
    DecodedFile decoded;
    decoded.text = std::move(text);
    decoded.encoding = encoding;
    decoded.encodingLabel = NativePad::EncodingLabel(encoding);
    decoded.lineEnding = NativePad::DetectLineEnding(decoded.text);
    decoded.decodedByteCount = decodedByteCount;
    return decoded;
}

std::optional<std::wstring> DecodeUtf8BestEffort(const char* bytes, size_t size) {
    for (size_t trim = 0; trim <= 3 && trim <= size; ++trim) {
        if (auto text = MultiByteToWide(CP_UTF8, bytes, static_cast<int>(size - trim), MB_ERR_INVALID_CHARS)) {
            return text;
        }
    }

    return std::nullopt;
}

DecodedFile DecodeBytes(const char* bytes, size_t size, bool allowTruncatedUtf8 = false) {
    // Small/editable files are decoded completely into UTF-16 for the piece table.
    // Large files bypass this path and use MappedTextDocument instead.
    if (size >= 2) {
        const auto b0 = static_cast<unsigned char>(bytes[0]);
        const auto b1 = static_cast<unsigned char>(bytes[1]);

        if (b0 == 0xFF && b1 == 0xFE) {
            return MakeDecodedFile(Utf16FromLeBytes(bytes, size, 2), NativePad::TextEncoding::Utf16Le, size);
        }

        if (b0 == 0xFE && b1 == 0xFF) {
            return MakeDecodedFile(Utf16FromBeBytes(bytes, size, 2), NativePad::TextEncoding::Utf16Be, size);
        }
    }

    if (size >= 3) {
        const auto b0 = static_cast<unsigned char>(bytes[0]);
        const auto b1 = static_cast<unsigned char>(bytes[1]);
        const auto b2 = static_cast<unsigned char>(bytes[2]);

        if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) {
            if (auto text = MultiByteToWide(CP_UTF8, bytes + 3, static_cast<int>(size - 3), MB_ERR_INVALID_CHARS)) {
                return MakeDecodedFile(*text, NativePad::TextEncoding::Utf8Bom, size);
            }
            if (allowTruncatedUtf8) {
                if (auto text = DecodeUtf8BestEffort(bytes + 3, size - 3)) {
                    return MakeDecodedFile(*text, NativePad::TextEncoding::Utf8Bom, size);
                }
            }
        }
    }

    if (auto text = MultiByteToWide(CP_UTF8, bytes, static_cast<int>(size), MB_ERR_INVALID_CHARS)) {
        return MakeDecodedFile(*text, NativePad::TextEncoding::Utf8, size);
    }

    if (allowTruncatedUtf8) {
        if (auto text = DecodeUtf8BestEffort(bytes, size)) {
            return MakeDecodedFile(*text, NativePad::TextEncoding::Utf8, size);
        }
    }

    if (auto text = MultiByteToWide(CP_ACP, bytes, static_cast<int>(size), 0)) {
        return MakeDecodedFile(*text, NativePad::TextEncoding::Ansi, size);
    }

    return MakeDecodedFile(L"", NativePad::TextEncoding::Utf8, size);
}

std::optional<DecodedFile> ReadTextFileWithReadFile(HANDLE file, size_t byteCount, std::wstring& error) {
    std::vector<char> bytes(byteCount);
    char* cursor = bytes.data();
    size_t remaining = bytes.size();

    while (remaining > 0) {
        const DWORD toRead = static_cast<DWORD>(std::min<size_t>(remaining, 16u * 1024u * 1024u));
        DWORD read = 0;
        if (!ReadFile(file, cursor, toRead, &read, nullptr)) {
            error = GetLastErrorText();
            return std::nullopt;
        }

        if (read == 0) {
            break;
        }

        cursor += read;
        remaining -= read;
    }

    return DecodeBytes(bytes.data(), bytes.size());
}

std::optional<DecodedFile> ReadLargeFilePreview(HANDLE file, uint64_t fileByteCount, std::wstring& error) {
    const size_t previewBytes = static_cast<size_t>(std::min<uint64_t>(fileByteCount, kLargeFilePreviewBytes));
    std::vector<char> bytes(previewBytes);
    char* cursor = bytes.data();
    size_t remaining = bytes.size();

    while (remaining > 0) {
        const DWORD toRead = static_cast<DWORD>(std::min<size_t>(remaining, 4u * 1024u * 1024u));
        DWORD read = 0;
        if (!ReadFile(file, cursor, toRead, &read, nullptr)) {
            error = GetLastErrorText();
            return std::nullopt;
        }

        if (read == 0) {
            break;
        }

        cursor += read;
        remaining -= read;
    }

    DecodedFile decoded = DecodeBytes(bytes.data(), bytes.size(), true);
    decoded.fileByteCount = fileByteCount;
    decoded.decodedByteCount = bytes.size();
    decoded.readOnlyPreview = true;
    decoded.truncated = fileByteCount > bytes.size();

    if (decoded.truncated) {
        decoded.text += L"\r\n\r\n--- NativePad read-only preview: file is larger than the editable limit; showing the first ";
        decoded.text += std::to_wstring(decoded.decodedByteCount / (1024u * 1024u));
        decoded.text += L" MB of ";
        decoded.text += std::to_wstring(fileByteCount / (1024u * 1024u));
        decoded.text += L" MB. ---\r\n";
    }

    return decoded;
}

std::optional<DecodedFile> ReadTextFile(const std::wstring& path, std::wstring& error) {
    // This loader is intentionally capped by kReadChunkLimit. Files above that
    // threshold are opened through the read-only memory-mapped backend.
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastErrorText();
        return std::nullopt;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        error = GetLastErrorText();
        CloseHandle(file);
        return std::nullopt;
    }

    if (size.QuadPart > kReadChunkLimit) {
        auto preview = ReadLargeFilePreview(file, static_cast<uint64_t>(size.QuadPart), error);
        CloseHandle(file);
        return preview;
    }

    const size_t byteCount = static_cast<size_t>(size.QuadPart);
    if (byteCount == 0) {
        CloseHandle(file);
        DecodedFile decoded = DecodeBytes(nullptr, 0);
        decoded.fileByteCount = 0;
        decoded.decodedByteCount = 0;
        return decoded;
    }

    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        auto fallback = ReadTextFileWithReadFile(file, byteCount, error);
        CloseHandle(file);
        return fallback;
    }

    const auto* mapped = static_cast<const char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    if (mapped == nullptr) {
        CloseHandle(mapping);
        SetFilePointer(file, 0, nullptr, FILE_BEGIN);
        auto fallback = ReadTextFileWithReadFile(file, byteCount, error);
        CloseHandle(file);
        return fallback;
    }

    DecodedFile decoded = DecodeBytes(mapped, byteCount);
    decoded.fileByteCount = byteCount;
    decoded.decodedByteCount = byteCount;
    UnmapViewOfFile(mapped);
    CloseHandle(mapping);
    CloseHandle(file);
    return decoded;
}

std::optional<uint64_t> FileByteCountForPath(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        return std::nullopt;
    }

    ULARGE_INTEGER size{};
    size.HighPart = attributes.nFileSizeHigh;
    size.LowPart = attributes.nFileSizeLow;
    return size.QuadPart;
}

bool WriteTextFile(
    const std::wstring& path,
    std::wstring_view text,
    NativePad::TextEncoding encoding,
    NativePad::LineEnding lineEnding,
    std::wstring& error) {
    // Encode into one contiguous buffer first so the file is only truncated
    // after NativePad knows the target format can represent the document.
    auto bytes = NativePad::EncodeTextBytes(text, encoding, lineEnding, error);
    if (!bytes) {
        return false;
    }

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastErrorText();
        return false;
    }

    const char* cursor = bytes->data();
    size_t remaining = bytes->size();

    while (remaining > 0) {
        const DWORD toWrite = static_cast<DWORD>(std::min<size_t>(remaining, 16u * 1024u * 1024u));
        DWORD written = 0;
        if (!WriteFile(file, cursor, toWrite, &written, nullptr)) {
            error = GetLastErrorText();
            CloseHandle(file);
            return false;
        }

        cursor += written;
        remaining -= written;
    }

    CloseHandle(file);
    return true;
}

std::optional<std::wstring> ShowOpenDialog(HWND owner) {
    std::array<wchar_t, 32768> buffer{};

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&ofn)) {
        return std::nullopt;
    }

    return std::wstring(buffer.data());
}

struct SaveDialogResult {
    std::wstring path;
    NativePad::TextEncoding encoding{NativePad::TextEncoding::Utf8};
};

std::optional<SaveDialogResult> ShowLegacySaveDialog(
    HWND owner,
    const std::wstring& currentPath,
    NativePad::TextEncoding currentEncoding) {
    std::array<wchar_t, 32768> buffer{};
    if (!currentPath.empty()) {
        StringCchCopyW(buffer.data(), buffer.size(), currentPath.c_str());
    }

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrDefExt = L"txt";
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;

    if (!GetSaveFileNameW(&ofn)) {
        return std::nullopt;
    }

    return SaveDialogResult{std::wstring(buffer.data()), currentEncoding};
}

std::wstring FileNamePart(const std::wstring& path) {
    const std::wstring::size_type separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

std::wstring ParentDirectoryPart(const std::wstring& path) {
    const std::wstring::size_type separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return {};
    }

    if (separator == 2 && path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) {
        return path.substr(0, 3);
    }

    return path.substr(0, separator);
}

std::optional<SaveDialogResult> ShowSaveDialog(
    HWND owner,
    const std::wstring& currentPath,
    NativePad::TextEncoding currentEncoding) {
    using Microsoft::WRL::ComPtr;

    ComPtr<IFileSaveDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) {
        return ShowLegacySaveDialog(owner, currentPath, currentEncoding);
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
    }

    COMDLG_FILTERSPEC filters[] = {
        {L"Text Documents (*.txt)", L"*.txt"},
        {L"All Files (*.*)", L"*.*"},
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetFileTypeIndex(1);
    dialog->SetDefaultExtension(L"txt");
    dialog->SetTitle(L"Save As");

    if (!currentPath.empty()) {
        const std::wstring fileName = FileNamePart(currentPath);
        if (!fileName.empty()) {
            dialog->SetFileName(fileName.c_str());
        }

        const std::wstring parent = ParentDirectoryPart(currentPath);
        if (!parent.empty()) {
            ComPtr<IShellItem> folder;
            if (SUCCEEDED(SHCreateItemFromParsingName(parent.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                dialog->SetFolder(folder.Get());
            }
        }
    }

    ComPtr<IFileDialogCustomize> customize;
    const bool hasEncodingPicker = SUCCEEDED(dialog.As(&customize));
    if (hasEncodingPicker) {
        // The modern file dialog owns layout and accessibility; NativePad only
        // contributes the classic Notepad-style encoding choice.
        customize->AddComboBox(kSaveEncodingComboId);
        customize->SetControlLabel(kSaveEncodingComboId, L"Encoding:");
        for (const auto& option : kSaveEncodingOptions) {
            customize->AddControlItem(kSaveEncodingComboId, option.id, option.label);
        }
        customize->SetSelectedControlItem(kSaveEncodingComboId, SaveEncodingOptionForEncoding(currentEncoding).id);
    }

    hr = dialog->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return std::nullopt;
    }
    if (FAILED(hr)) {
        return std::nullopt;
    }

    DWORD selectedEncodingId = SaveEncodingOptionForEncoding(currentEncoding).id;
    if (hasEncodingPicker) {
        customize->GetSelectedControlItem(kSaveEncodingComboId, &selectedEncodingId);
    }

    ComPtr<IShellItem> result;
    if (FAILED(dialog->GetResult(&result))) {
        return std::nullopt;
    }

    PWSTR rawPath = nullptr;
    const HRESULT pathHr = result->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
    if (FAILED(pathHr) || rawPath == nullptr) {
        if (rawPath != nullptr) {
            CoTaskMemFree(rawPath);
        }
        return std::nullopt;
    }

    SaveDialogResult saveResult{std::wstring(rawPath), SaveEncodingOptionForId(selectedEncodingId).encoding};
    CoTaskMemFree(rawPath);
    return saveResult;
}

LOGFONTW LogFontFromEditorFont(const NativePad::EditorFontSpec& font, UINT dpi) {
    LOGFONTW logFont{};
    logFont.lfHeight = -MulDiv(static_cast<int>(std::round(font.sizeDips * 72.0f / 96.0f)), static_cast<int>(dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi), 72);
    logFont.lfWeight = font.weight;
    logFont.lfItalic = font.italic ? TRUE : FALSE;
    StringCchCopyW(logFont.lfFaceName, std::size(logFont.lfFaceName), font.family.c_str());
    return logFont;
}

NativePad::EditorFontSpec EditorFontFromLogFont(const LOGFONTW& logFont, int pointSizeTenths, UINT dpi) {
    const float pointSize = pointSizeTenths > 0
                                ? static_cast<float>(pointSizeTenths) / 10.0f
                                : static_cast<float>(std::abs(logFont.lfHeight) * 72) / static_cast<float>(dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi);

    NativePad::EditorFontSpec font{};
    font.family = logFont.lfFaceName[0] != L'\0' ? logFont.lfFaceName : L"Consolas";
    font.sizeDips = std::max(6.0f, pointSize * 96.0f / 72.0f);
    font.weight = logFont.lfWeight == 0 ? FW_NORMAL : logFont.lfWeight;
    font.italic = logFont.lfItalic != FALSE;
    return font;
}

struct FontStyleChoice {
    const wchar_t* name;
    LONG weight;
    bool italic;
};

constexpr std::array<FontStyleChoice, 4> kFontStyleChoices{{
    {L"Regular", FW_NORMAL, false},
    {L"Italic", FW_NORMAL, true},
    {L"Bold", FW_BOLD, false},
    {L"Bold Italic", FW_BOLD, true},
}};

struct FontDialogState {
    // Custom replacement for ChooseFont. It gives us control over dark-mode list
    // painting, DPI layout, and the resizable sample preview.
    HWND hwnd{};
    HWND owner{};
    HWND familyLabel{};
    HWND familyEdit{};
    HWND familyList{};
    HWND styleLabel{};
    HWND styleEdit{};
    HWND styleList{};
    HWND sizeLabel{};
    HWND sizeEdit{};
    HWND sizeList{};
    HWND sampleLabel{};
    HWND sample{};
    HWND ok{};
    HWND cancel{};
    HFONT uiFont{};
    HFONT previewFont{};
    HBRUSH backgroundBrush{};
    HBRUSH controlBrush{};
    UINT dpi{USER_DEFAULT_SCREEN_DPI};
    bool dark{false};
    bool accepted{false};
    NativePad::EditorFontSpec initialFont{};
    NativePad::EditorFontSpec selectedFont{};
    std::vector<std::wstring> families;
    std::vector<int> sizes;
};

std::array<HWND, 13> FontDialogControls(FontDialogState* state) {
    if (state == nullptr) {
        return {};
    }

    return {
        state->familyLabel,
        state->familyEdit,
        state->familyList,
        state->styleLabel,
        state->styleEdit,
        state->styleList,
        state->sizeLabel,
        state->sizeEdit,
        state->sizeList,
        state->sampleLabel,
        state->sample,
        state->ok,
        state->cancel,
    };
}

void RedrawFontDialogAfterLayout(FontDialogState* state) {
    if (state == nullptr || state->hwnd == nullptr) {
        return;
    }

    for (HWND control : FontDialogControls(state)) {
        if (control != nullptr) {
            RedrawWindow(control, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
        }
    }

    RedrawWindow(state->hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

float FontPointSize(const NativePad::EditorFontSpec& font) {
    return font.sizeDips * 72.0f / 96.0f;
}

std::wstring FormatPointSize(float pointSize) {
    wchar_t buffer[32]{};
    const float rounded = std::round(pointSize);
    if (std::fabs(pointSize - rounded) < 0.05f) {
        StringCchPrintfW(buffer, std::size(buffer), L"%d", static_cast<int>(rounded));
    } else {
        StringCchPrintfW(buffer, std::size(buffer), L"%.1f", pointSize);
    }
    return buffer;
}

std::wstring Trim(std::wstring text) {
    const auto first = std::find_if_not(text.begin(), text.end(), [](wchar_t ch) {
        return std::iswspace(ch) != 0;
    });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](wchar_t ch) {
        return std::iswspace(ch) != 0;
    }).base();

    if (first >= last) {
        return {};
    }

    return std::wstring(first, last);
}

bool TryParsePointSize(const std::wstring& text, float& pointSize) {
    const std::wstring trimmed = Trim(text);
    if (trimmed.empty()) {
        return false;
    }

    wchar_t* end = nullptr;
    const float value = wcstof(trimmed.c_str(), &end);
    while (end != nullptr && std::iswspace(*end) != 0) {
        ++end;
    }

    if (end == trimmed.c_str() || (end != nullptr && *end != L'\0') || !std::isfinite(value)) {
        return false;
    }

    pointSize = value;
    return pointSize >= 1.0f && pointSize <= 200.0f;
}

int CALLBACK EnumFontFamilyProc(const LOGFONTW* logFont, const TEXTMETRICW*, DWORD, LPARAM param) {
    auto* families = reinterpret_cast<std::vector<std::wstring>*>(param);
    if (families == nullptr || logFont == nullptr || logFont->lfFaceName[0] == L'\0' || logFont->lfFaceName[0] == L'@') {
        return 1;
    }

    families->push_back(logFont->lfFaceName);
    return 1;
}

std::vector<std::wstring> EnumerateFontFamilies() {
    std::vector<std::wstring> families;
    HDC dc = GetDC(nullptr);
    if (dc != nullptr) {
        LOGFONTW query{};
        query.lfCharSet = DEFAULT_CHARSET;
        EnumFontFamiliesExW(dc, &query, EnumFontFamilyProc, reinterpret_cast<LPARAM>(&families), 0);
        ReleaseDC(nullptr, dc);
    }

    std::sort(families.begin(), families.end(), [](const std::wstring& left, const std::wstring& right) {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    families.erase(std::unique(families.begin(), families.end(), [](const std::wstring& left, const std::wstring& right) {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
    }), families.end());

    if (families.empty()) {
        families.push_back(L"Consolas");
    }

    return families;
}

int FontStyleIndexFor(const NativePad::EditorFontSpec& font) {
    const bool bold = font.weight >= FW_SEMIBOLD;
    if (bold && font.italic) {
        return 3;
    }
    if (bold) {
        return 2;
    }
    if (font.italic) {
        return 1;
    }
    return 0;
}

void FillFontSizeChoices(FontDialogState* state) {
    state->sizes = {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72};
    const int current = static_cast<int>(std::round(FontPointSize(state->initialFont)));
    if (current > 0 && std::find(state->sizes.begin(), state->sizes.end(), current) == state->sizes.end()) {
        state->sizes.push_back(current);
        std::sort(state->sizes.begin(), state->sizes.end());
    }
}

void SelectListBoxText(HWND list, const std::wstring& text) {
    if (list == nullptr) {
        return;
    }

    LRESULT index = SendMessageW(list, LB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(text.c_str()));
    if (index == LB_ERR) {
        index = SendMessageW(list, LB_FINDSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(text.c_str()));
    }
    if (index != LB_ERR) {
        SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
    }
}

void PopulateFontDialog(FontDialogState* state) {
    state->families = EnumerateFontFamilies();
    for (const auto& family : state->families) {
        SendMessageW(state->familyList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(family.c_str()));
    }

    for (const auto& style : kFontStyleChoices) {
        SendMessageW(state->styleList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(style.name));
    }

    FillFontSizeChoices(state);
    for (int size : state->sizes) {
        const std::wstring text = std::to_wstring(size);
        SendMessageW(state->sizeList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }

    SetControlText(state->familyEdit, state->initialFont.family);
    SelectListBoxText(state->familyList, state->initialFont.family);

    const int styleIndex = FontStyleIndexFor(state->initialFont);
    SendMessageW(state->styleList, LB_SETCURSEL, static_cast<WPARAM>(styleIndex), 0);
    SetControlText(state->styleEdit, kFontStyleChoices[static_cast<size_t>(styleIndex)].name);

    const std::wstring sizeText = FormatPointSize(FontPointSize(state->initialFont));
    SetControlText(state->sizeEdit, sizeText);
    SelectListBoxText(state->sizeList, sizeText);
}

bool ReadFontDialogSelection(FontDialogState* state, NativePad::EditorFontSpec& font, bool showErrors) {
    std::wstring family = Trim(ControlText(state->familyEdit));
    if (family.empty()) {
        if (showErrors) {
            MessageBoxW(state->hwnd, L"Choose a font family.", L"NativePad - Font", MB_ICONINFORMATION | MB_OK);
            SetFocus(state->familyEdit);
        }
        return false;
    }

    float pointSize = 0.0f;
    if (!TryParsePointSize(ControlText(state->sizeEdit), pointSize)) {
        if (showErrors) {
            MessageBoxW(state->hwnd, L"Font size must be between 1 and 200 points.", L"NativePad - Font", MB_ICONINFORMATION | MB_OK);
            SetFocus(state->sizeEdit);
            SendMessageW(state->sizeEdit, EM_SETSEL, 0, -1);
        }
        return false;
    }

    LRESULT styleIndex = SendMessageW(state->styleList, LB_GETCURSEL, 0, 0);
    if (styleIndex == LB_ERR) {
        styleIndex = 0;
    }
    const auto& style = kFontStyleChoices[static_cast<size_t>(std::clamp<LRESULT>(styleIndex, 0, static_cast<LRESULT>(kFontStyleChoices.size() - 1)))];

    font.family = family;
    font.sizeDips = pointSize * 96.0f / 72.0f;
    font.weight = style.weight;
    font.italic = style.italic;
    return true;
}

void UpdateFontPreview(FontDialogState* state) {
    if (state == nullptr) {
        return;
    }

    NativePad::EditorFontSpec font{};
    if (!ReadFontDialogSelection(state, font, false)) {
        font = state->initialFont;
    }

    LOGFONTW logFont = LogFontFromEditorFont(font, state->dpi);
    HFONT preview = CreateFontIndirectW(&logFont);
    if (preview != nullptr) {
        if (state->previewFont != nullptr) {
            DeleteObject(state->previewFont);
        }
        state->previewFont = preview;
    }

    InvalidateRect(state->sample, nullptr, TRUE);
}

void LayoutFontDialog(FontDialogState* state) {
    // Extra height is shared between the list boxes and preview area, while OK
    // and Cancel stay anchored to the lower-right corner.
    if (state == nullptr || state->hwnd == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(state->hwnd, &client);
    const int margin = ScaleForDpi(14, state->dpi);
    const int gap = ScaleForDpi(10, state->dpi);
    const int labelHeight = ScaleForDpi(18, state->dpi);
    const int editHeight = ScaleForDpi(24, state->dpi);
    const int buttonWidth = ScaleForDpi(82, state->dpi);
    const int buttonHeight = ScaleForDpi(28, state->dpi);
    const int sizeWidth = ScaleForDpi(78, state->dpi);
    const int styleWidth = ScaleForDpi(132, state->dpi);
    const int clientWidth = static_cast<int>(client.right - client.left);
    const int clientHeight = static_cast<int>(client.bottom - client.top);
    const int fullWidth = std::max(ScaleForDpi(390, state->dpi), clientWidth - (margin * 2));
    const int familyWidth = std::max(ScaleForDpi(160, state->dpi), fullWidth - styleWidth - sizeWidth - (gap * 2));
    const int top = margin;
    const int editTop = top + labelHeight + ScaleForDpi(2, state->dpi);
    const int listTop = editTop + editHeight + ScaleForDpi(4, state->dpi);
    const int styleLeft = margin + familyWidth + gap;
    const int sizeLeft = styleLeft + styleWidth + gap;
    const int buttonTop = clientHeight - margin - buttonHeight;
    const int sampleMinHeight = ScaleForDpi(84, state->dpi);
    const int listMinHeight = ScaleForDpi(148, state->dpi);
    const int listMaxHeight = ScaleForDpi(320, state->dpi);
    const int baseClientHeight = ScaleForDpi(397, state->dpi);
    const int extraHeight = std::max(0, clientHeight - baseClientHeight);
    const int listHeight = std::clamp(listMinHeight + (extraHeight / 2), listMinHeight, listMaxHeight);

    MoveWindow(state->familyLabel, margin, top, familyWidth, labelHeight, TRUE);
    MoveBorderedControl(state->familyEdit, margin, editTop, familyWidth, editHeight);
    MoveBorderedControl(state->familyList, margin, listTop, familyWidth, listHeight);

    MoveWindow(state->styleLabel, styleLeft, top, styleWidth, labelHeight, TRUE);
    MoveBorderedControl(state->styleEdit, styleLeft, editTop, styleWidth, editHeight);
    MoveBorderedControl(state->styleList, styleLeft, listTop, styleWidth, listHeight);

    MoveWindow(state->sizeLabel, sizeLeft, top, sizeWidth, labelHeight, TRUE);
    MoveBorderedControl(state->sizeEdit, sizeLeft, editTop, sizeWidth, editHeight);
    MoveBorderedControl(state->sizeList, sizeLeft, listTop, sizeWidth, listHeight);

    const int sampleLabelTop = listTop + listHeight + ScaleForDpi(6, state->dpi);
    const int sampleTop = sampleLabelTop + labelHeight + ScaleForDpi(4, state->dpi);
    const int sampleHeight = std::max<int>(
        sampleMinHeight,
        buttonTop - sampleTop - ScaleForDpi(16, state->dpi));
    MoveWindow(state->sampleLabel, margin, sampleLabelTop, fullWidth, labelHeight, TRUE);
    MoveWindow(state->sample, margin, sampleTop, fullWidth, sampleHeight, TRUE);

    MoveWindow(state->cancel, clientWidth - margin - buttonWidth, buttonTop, buttonWidth, buttonHeight, TRUE);
    MoveWindow(state->ok, clientWidth - margin - (buttonWidth * 2) - gap, buttonTop, buttonWidth, buttonHeight, TRUE);
    RedrawFontDialogAfterLayout(state);
}

LRESULT FontDialogCtlColor(FontDialogState* state, UINT message, WPARAM wParam) {
    if (state == nullptr) {
        return 0;
    }

    const DialogColors colors = DialogColorsForTheme(state->dark);
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetTextColor(dc, colors.text);
    SetBkMode(dc, TRANSPARENT);

    if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) {
        SetBkColor(dc, colors.controlBackground);
        return reinterpret_cast<LRESULT>(state->controlBrush);
    }

    SetBkColor(dc, colors.background);
    return reinterpret_cast<LRESULT>(state->backgroundBrush);
}

void DrawFontPreview(FontDialogState* state, DRAWITEMSTRUCT* draw) {
    if (state == nullptr || draw == nullptr) {
        return;
    }

    const DialogColors colors = DialogColorsForTheme(state->dark);
    HBRUSH background = CreateSolidBrush(colors.controlBackground);
    FillRect(draw->hDC, &draw->rcItem, background);
    DeleteObject(background);

    HPEN borderPen = CreatePen(PS_SOLID, 1, colors.border);
    HGDIOBJ oldPen = SelectObject(draw->hDC, borderPen);
    HGDIOBJ oldBrush = SelectObject(draw->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(draw->hDC, draw->rcItem.left, draw->rcItem.top, draw->rcItem.right, draw->rcItem.bottom);
    SelectObject(draw->hDC, oldBrush);
    SelectObject(draw->hDC, oldPen);
    DeleteObject(borderPen);

    HFONT font = state->previewFont != nullptr ? state->previewFont : state->uiFont;
    HGDIOBJ oldFont = SelectObject(draw->hDC, font);
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, colors.text);
    RECT textRect = draw->rcItem;
    InflateRect(&textRect, -ScaleForDpi(12, state->dpi), -ScaleForDpi(8, state->dpi));
    DrawTextW(draw->hDC, L"AaBbYyZz", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(draw->hDC, oldFont);
}

bool IsFontDialogListId(UINT id) {
    return id == kFontFamilyListId || id == kFontStyleListId || id == kFontSizeListId;
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

void DrawFontListItem(FontDialogState* state, DRAWITEMSTRUCT* draw) {
    // Owner-draw avoids light listbox backgrounds and stale native focus marks
    // while scrolling or resizing in dark mode.
    if (state == nullptr || draw == nullptr) {
        return;
    }

    const DialogColors colors = DialogColorsForTheme(state->dark);
    const bool selected = (draw->itemState & ODS_SELECTED) != 0;
    const bool disabled = (draw->itemState & ODS_DISABLED) != 0;
    const COLORREF backgroundColor = selected ? colors.selectionBackground : colors.controlBackground;
    const COLORREF textColor = disabled ? colors.disabledText : (selected ? colors.selectionText : colors.text);

    HBRUSH background = CreateSolidBrush(backgroundColor);
    FillRect(draw->hDC, &draw->rcItem, background);
    DeleteObject(background);

    if (draw->itemID != static_cast<UINT>(-1)) {
        const LRESULT length = SendMessageW(draw->hwndItem, LB_GETTEXTLEN, draw->itemID, 0);
        if (length != LB_ERR) {
            std::wstring text(static_cast<size_t>(length) + 1, L'\0');
            SendMessageW(draw->hwndItem, LB_GETTEXT, draw->itemID, reinterpret_cast<LPARAM>(text.data()));
            text.resize(static_cast<size_t>(length));

            HFONT font = state->uiFont != nullptr ? state->uiFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HGDIOBJ oldFont = SelectObject(draw->hDC, font);
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, textColor);
            RECT textRect = draw->rcItem;
            textRect.left += ScaleForDpi(4, state->dpi);
            textRect.right -= ScaleForDpi(4, state->dpi);
            DrawTextW(draw->hDC, text.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(draw->hDC, oldFont);
        }
    }
}

void ApplyFontDialogTheme(FontDialogState* state) {
    if (state == nullptr) {
        return;
    }

    ApplyDarkFrame(state->hwnd, state->dark);
    const std::array<HWND, 15> controls{
        state->familyLabel,
        state->familyEdit,
        state->familyList,
        state->styleLabel,
        state->styleEdit,
        state->styleList,
        state->sizeLabel,
        state->sizeEdit,
        state->sizeList,
        state->sampleLabel,
        state->sample,
        state->ok,
        state->cancel,
        nullptr,
        nullptr,
    };
    for (HWND control : controls) {
        ApplyDialogControlTheme(control, state->dark);
    }
}

void PaintFontDialog(FontDialogState* state) {
    if (state == nullptr || state->hwnd == nullptr) {
        return;
    }

    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(state->hwnd, &ps);
    if (hdc == nullptr) {
        return;
    }

    const DialogColors colors = DialogColorsForTheme(state->dark);
    FillRect(hdc, &ps.rcPaint, state->backgroundBrush);
    DrawDialogChildBorder(state->hwnd, state->familyEdit, hdc, colors);
    DrawDialogChildBorder(state->hwnd, state->familyList, hdc, colors);
    DrawDialogChildBorder(state->hwnd, state->styleEdit, hdc, colors);
    DrawDialogChildBorder(state->hwnd, state->styleList, hdc, colors);
    DrawDialogChildBorder(state->hwnd, state->sizeEdit, hdc, colors);
    DrawDialogChildBorder(state->hwnd, state->sizeList, hdc, colors);
    EndPaint(state->hwnd, &ps);
}

LRESULT CALLBACK FontDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<FontDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<FontDialogState*>(create->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        const UINT dpi = state != nullptr ? state->dpi : USER_DEFAULT_SCREEN_DPI;
        info->ptMinTrackSize.x = ScaleForDpi(532, dpi);
        info->ptMinTrackSize.y = ScaleForDpi(438, dpi);
        return 0;
    }
    case WM_CREATE: {
        const DialogColors colors = DialogColorsForTheme(state->dark);
        state->backgroundBrush = CreateSolidBrush(colors.background);
        state->controlBrush = CreateSolidBrush(colors.controlBackground);
        state->uiFont = CreateUiFontForDpi(state->dpi);

        state->familyLabel = CreateWindowExW(0, L"STATIC", L"Font:", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        state->familyEdit = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontFamilyEditId)), nullptr, nullptr);
        state->familyList = CreateWindowExW(0, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontFamilyListId)), nullptr, nullptr);

        state->styleLabel = CreateWindowExW(0, L"STATIC", L"Font style:", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        state->styleEdit = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | ES_AUTOHSCROLL | ES_READONLY, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontStyleEditId)), nullptr, nullptr);
        state->styleList = CreateWindowExW(0, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontStyleListId)), nullptr, nullptr);

        state->sizeLabel = CreateWindowExW(0, L"STATIC", L"Size:", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        state->sizeEdit = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontSizeEditId)), nullptr, nullptr);
        state->sizeList = CreateWindowExW(0, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontSizeListId)), nullptr, nullptr);

        state->sampleLabel = CreateWindowExW(0, L"STATIC", L"Sample", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        state->sample = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontPreviewId)), nullptr, nullptr);
        state->ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
        state->cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);

        const std::array<HWND, 15> controls{
            state->familyLabel,
            state->familyEdit,
            state->familyList,
            state->styleLabel,
            state->styleEdit,
            state->styleList,
            state->sizeLabel,
            state->sizeEdit,
            state->sizeList,
            state->sampleLabel,
            state->sample,
            state->ok,
            state->cancel,
            nullptr,
            nullptr,
        };
        for (HWND control : controls) {
            SetControlFont(control, state->uiFont);
        }

        PopulateFontDialog(state);
        ApplyFontDialogTheme(state);
        LayoutFontDialog(state);
        UpdateFontPreview(state);
        SetFocus(state->familyEdit);
        SendMessageW(state->familyEdit, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
        return 0;
    }
    case WM_SIZE:
        LayoutFontDialog(state);
        return 0;
    case WM_PAINT:
        PaintFontDialog(state);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            NativePad::EditorFontSpec font{};
            if (ReadFontDialogSelection(state, font, true)) {
                state->selectedFont = font;
                state->accepted = true;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case IDCANCEL:
            DestroyWindow(hwnd);
            return 0;
        case kFontFamilyListId:
            if (HIWORD(wParam) == LBN_SELCHANGE) {
                const LRESULT index = SendMessageW(state->familyList, LB_GETCURSEL, 0, 0);
                if (index != LB_ERR) {
                    std::array<wchar_t, LF_FACESIZE> family{};
                    SendMessageW(state->familyList, LB_GETTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(family.data()));
                    SetControlText(state->familyEdit, family.data());
                }
                UpdateFontPreview(state);
            }
            return 0;
        case kFontStyleListId:
            if (HIWORD(wParam) == LBN_SELCHANGE) {
                const LRESULT index = SendMessageW(state->styleList, LB_GETCURSEL, 0, 0);
                if (index != LB_ERR) {
                    SetControlText(state->styleEdit, kFontStyleChoices[static_cast<size_t>(index)].name);
                }
                UpdateFontPreview(state);
            }
            return 0;
        case kFontSizeListId:
            if (HIWORD(wParam) == LBN_SELCHANGE) {
                const LRESULT index = SendMessageW(state->sizeList, LB_GETCURSEL, 0, 0);
                if (index != LB_ERR) {
                    std::array<wchar_t, 32> size{};
                    SendMessageW(state->sizeList, LB_GETTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(size.data()));
                    SetControlText(state->sizeEdit, size.data());
                }
                UpdateFontPreview(state);
            }
            return 0;
        case kFontFamilyEditId:
        case kFontSizeEditId:
            if (HIWORD(wParam) == EN_CHANGE) {
                UpdateFontPreview(state);
            }
            return 0;
        default:
            break;
        }
        break;
    case WM_MEASUREITEM: {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (measure != nullptr && IsFontDialogListId(measure->CtlID)) {
            measure->itemHeight = static_cast<UINT>(ScaleForDpi(24, state != nullptr ? state->dpi : USER_DEFAULT_SCREEN_DPI));
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (draw == nullptr) {
            return FALSE;
        }
        if (draw->CtlID == kFontPreviewId) {
            DrawFontPreview(state, draw);
            return TRUE;
        }
        if (IsFontDialogListId(draw->CtlID)) {
            DrawFontListItem(state, draw);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return FontDialogCtlColor(state, message, wParam);
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
        return 1;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        DeleteUiFont(state != nullptr ? state->uiFont : nullptr);
        if (state != nullptr) {
            if (state->previewFont != nullptr) {
                DeleteObject(state->previewFont);
            }
            if (state->backgroundBrush != nullptr) {
                DeleteObject(state->backgroundBrush);
            }
            if (state->controlBrush != nullptr) {
                DeleteObject(state->controlBrush);
            }
            state->uiFont = nullptr;
            state->previewFont = nullptr;
            state->backgroundBrush = nullptr;
            state->controlBrush = nullptr;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

std::optional<NativePad::EditorFontSpec> ShowFontDialog(HWND owner, HINSTANCE instance, const NativePad::EditorFontSpec& currentFont, UINT dpi, bool dark) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &FontDialogProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kFontDialogClass;
    AssignWindowClassIcons(wc, instance);
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        MessageBoxW(owner, GetLastErrorText().c_str(), L"NativePad - Font", MB_ICONERROR | MB_OK);
        return std::nullopt;
    }

    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    dpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
    const int width = ScaleForDpi(532, dpi);
    const int height = ScaleForDpi(438, dpi);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left - width) / 2);
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top - height) / 2);

    FontDialogState state{};
    state.owner = owner;
    state.dpi = dpi;
    state.dark = dark;
    state.initialFont = currentFont;
    state.selectedFont = currentFont;

    HWND dialog = CreateWindowExW(
        WS_EX_WINDOWEDGE | WS_EX_CONTROLPARENT,
        kFontDialogClass,
        L"Font",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
        x,
        y,
        width,
        height,
        owner,
        nullptr,
        instance,
        &state);
    if (dialog == nullptr) {
        MessageBoxW(owner, GetLastErrorText().c_str(), L"NativePad - Font", MB_ICONERROR | MB_OK);
        return std::nullopt;
    }
    ApplyWindowIcons(dialog, instance);

    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (MessageTargetsWindow(dialog, message) && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
            DestroyWindow(dialog);
            continue;
        }
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    SetFocus(owner);

    if (state.accepted) {
        return state.selectedFont;
    }

    return std::nullopt;
}

struct PrintResult {
    bool success{false};
    std::wstring message;
};

struct PrintJob {
    HWND owner{};
    HDC printerDc{};
    std::wstring documentName;
    std::wstring text;
    LOGFONTW font{};
    RECT marginsThousandths{1000, 1000, 1000, 1000};
    bool wordWrap{false};
};

bool NextPrintLine(std::wstring_view text, size_t& offset, std::wstring_view& line) {
    if (offset > text.size()) {
        return false;
    }

    const size_t start = offset;
    while (offset < text.size() && text[offset] != L'\r' && text[offset] != L'\n') {
        ++offset;
    }

    line = text.substr(start, offset - start);
    if (offset < text.size()) {
        if (text[offset] == L'\r' && offset + 1 < text.size() && text[offset + 1] == L'\n') {
            offset += 2;
        } else {
            ++offset;
        }
    } else {
        offset = text.size() + 1;
    }

    return true;
}

bool StartNextPrintPage(HDC dc, bool& pageOpen, std::wstring& error) {
    if (pageOpen && EndPage(dc) <= 0) {
        error = L"Could not finish the current print page.";
        pageOpen = false;
        return false;
    }

    if (StartPage(dc) <= 0) {
        error = L"Could not start a print page.";
        pageOpen = false;
        return false;
    }

    pageOpen = true;
    return true;
}

bool PrintDocument(PrintJob& job, std::wstring& error) {
    // Pagination and printer DC calls can block. This function runs on the print
    // worker thread; the UI only receives the final PrintResult.
    HFONT font = CreateFontIndirectW(&job.font);
    if (font == nullptr) {
        error = L"Could not create the selected printer font.";
        return false;
    }

    HGDIOBJ oldFont = SelectObject(job.printerDc, font);
    SetBkMode(job.printerDc, TRANSPARENT);
    SetTextColor(job.printerDc, RGB(0, 0, 0));

    const int dpiX = std::max(1, GetDeviceCaps(job.printerDc, LOGPIXELSX));
    const int dpiY = std::max(1, GetDeviceCaps(job.printerDc, LOGPIXELSY));
    RECT pageRect{
        MulDiv(job.marginsThousandths.left, dpiX, 1000),
        MulDiv(job.marginsThousandths.top, dpiY, 1000),
        GetDeviceCaps(job.printerDc, HORZRES) - MulDiv(job.marginsThousandths.right, dpiX, 1000),
        GetDeviceCaps(job.printerDc, VERTRES) - MulDiv(job.marginsThousandths.bottom, dpiY, 1000),
    };
    if (pageRect.right <= pageRect.left || pageRect.bottom <= pageRect.top) {
        pageRect = {dpiX / 2, dpiY / 2, GetDeviceCaps(job.printerDc, HORZRES) - (dpiX / 2), GetDeviceCaps(job.printerDc, VERTRES) - (dpiY / 2)};
    }

    TEXTMETRICW metrics{};
    GetTextMetricsW(job.printerDc, &metrics);
    const int lineHeight = std::max<int>(1, static_cast<int>(metrics.tmHeight + metrics.tmExternalLeading));

    DOCINFOW docInfo{};
    docInfo.cbSize = sizeof(docInfo);
    docInfo.lpszDocName = job.documentName.empty() ? L"NativePad Document" : job.documentName.c_str();
    if (StartDocW(job.printerDc, &docInfo) <= 0) {
        error = L"Could not start the print job.";
        SelectObject(job.printerDc, oldFont);
        DeleteObject(font);
        return false;
    }

    bool pageOpen = false;
    bool ok = StartNextPrintPage(job.printerDc, pageOpen, error);
    int y = pageRect.top;

    size_t offset = 0;
    std::wstring_view line;
    while (ok && NextPrintLine(job.text, offset, line)) {
        RECT measureRect{pageRect.left, y, pageRect.right, pageRect.bottom};
        const UINT format = DT_NOPREFIX | DT_EXPANDTABS | (job.wordWrap ? DT_WORDBREAK : DT_SINGLELINE);
        int textHeight = lineHeight;
        if (!line.empty()) {
            RECT calcRect{pageRect.left, 0, pageRect.right, pageRect.bottom};
            DrawTextW(job.printerDc, line.data(), static_cast<int>(line.size()), &calcRect, format | DT_CALCRECT);
            textHeight = std::max<int>(lineHeight, calcRect.bottom - calcRect.top);
        }

        if (y + textHeight > pageRect.bottom) {
            ok = StartNextPrintPage(job.printerDc, pageOpen, error);
            y = pageRect.top;
            measureRect.top = y;
            measureRect.bottom = pageRect.bottom;
        }

        if (ok && !line.empty()) {
            DrawTextW(job.printerDc, line.data(), static_cast<int>(line.size()), &measureRect, format);
        }
        y += textHeight;
    }

    if (pageOpen && EndPage(job.printerDc) <= 0) {
        ok = false;
        error = L"Could not finish the final print page.";
    }

    if (ok) {
        EndDoc(job.printerDc);
    } else {
        AbortDoc(job.printerDc);
    }

    SelectObject(job.printerDc, oldFont);
    DeleteObject(font);
    return ok;
}

void StartPrintWorker(PrintJob job) {
    // Ownership of the printer DC transfers here. The worker posts back to the
    // window and releases the result pointer only if the message is queued.
    std::thread([job = std::move(job)]() mutable {
        std::unique_ptr<PrintResult> result = std::make_unique<PrintResult>();
        result->success = PrintDocument(job, result->message);
        DeleteDC(job.printerDc);
        if (!PostMessageW(job.owner, WM_NATIVEPAD_PRINT_COMPLETE, 0, reinterpret_cast<LPARAM>(result.get()))) {
            return;
        }
        result.release();
    }).detach();
}

struct AboutDialogState {
    // The About box is a custom modal window so it can use the same dark frame,
    // app icon, and DPI-aware layout as the other NativePad dialogs.
    HWND hwnd{};
    HWND owner{};
    HWND icon{};
    HWND title{};
    HWND description{};
    std::array<HWND, 4> metadataLabels{};
    HWND ok{};
    HINSTANCE instance{};
    HFONT uiFont{};
    HFONT titleFont{};
    HBRUSH backgroundBrush{};
    UINT dpi{USER_DEFAULT_SCREEN_DPI};
    bool dark{false};
};

std::array<HWND, 7> AboutDialogControls(AboutDialogState* state) {
    if (state == nullptr) {
        return {};
    }

    return {
        state->title,
        state->description,
        state->metadataLabels[0],
        state->metadataLabels[1],
        state->metadataLabels[2],
        state->metadataLabels[3],
        state->ok,
    };
}

HFONT CreateAboutTitleFont(HFONT baseFont, UINT dpi) {
    LOGFONTW logFont{};
    if (baseFont == nullptr || GetObjectW(baseFont, sizeof(logFont), &logFont) == 0) {
        return nullptr;
    }

    logFont.lfHeight = -ScaleForDpi(24, dpi);
    logFont.lfWeight = FW_SEMIBOLD;
    return CreateFontIndirectW(&logFont);
}

void LayoutAboutDialog(AboutDialogState* state) {
    if (state == nullptr || state->hwnd == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(state->hwnd, &client);
    const int margin = ScaleForDpi(20, state->dpi);
    const int iconSize = ScaleForDpi(56, state->dpi);
    const int titleLeft = margin + iconSize + ScaleForDpi(16, state->dpi);
    const int titleHeight = ScaleForDpi(32, state->dpi);
    const int descriptionTop = margin + titleHeight + ScaleForDpi(4, state->dpi);
    const int descriptionHeight = ScaleForDpi(42, state->dpi);
    const int metadataTop = margin + ScaleForDpi(102, state->dpi);
    const int metadataHeight = ScaleForDpi(22, state->dpi);
    const int buttonWidth = ScaleForDpi(92, state->dpi);
    const int buttonHeight = ScaleForDpi(30, state->dpi);
    const int contentWidth = client.right - client.left - titleLeft - margin;
    const int buttonTop = client.bottom - margin - buttonHeight;

    MoveWindow(state->icon, margin, margin + ScaleForDpi(2, state->dpi), iconSize, iconSize, TRUE);
    MoveWindow(state->title, titleLeft, margin, contentWidth, titleHeight, TRUE);
    MoveWindow(state->description, titleLeft, descriptionTop, contentWidth, descriptionHeight, TRUE);

    for (size_t i = 0; i < state->metadataLabels.size(); ++i) {
        MoveWindow(
            state->metadataLabels[i],
            margin,
            metadataTop + (static_cast<int>(i) * metadataHeight),
            client.right - client.left - (margin * 2),
            metadataHeight,
            TRUE);
    }

    MoveWindow(state->ok, client.right - margin - buttonWidth, buttonTop, buttonWidth, buttonHeight, TRUE);
    InvalidateRect(state->hwnd, nullptr, TRUE);
}

void ApplyAboutDialogTheme(AboutDialogState* state) {
    if (state == nullptr) {
        return;
    }

    ApplyDarkFrame(state->hwnd, state->dark);
    for (HWND control : AboutDialogControls(state)) {
        ApplyDialogControlTheme(control, state->dark);
    }
}

LRESULT AboutDialogCtlColor(AboutDialogState* state, WPARAM wParam) {
    if (state == nullptr) {
        return 0;
    }

    const DialogColors colors = DialogColorsForTheme(state->dark);
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetTextColor(dc, colors.text);
    SetBkColor(dc, colors.background);
    SetBkMode(dc, TRANSPARENT);
    return reinterpret_cast<LRESULT>(state->backgroundBrush);
}

void PaintAboutDialog(AboutDialogState* state) {
    if (state == nullptr || state->hwnd == nullptr) {
        return;
    }

    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(state->hwnd, &ps);
    if (hdc == nullptr) {
        return;
    }

    const DialogColors colors = DialogColorsForTheme(state->dark);
    FillRect(hdc, &ps.rcPaint, state->backgroundBrush);

    RECT client{};
    GetClientRect(state->hwnd, &client);
    const int margin = ScaleForDpi(20, state->dpi);
    const int lineY = ScaleForDpi(92, state->dpi);
    HPEN separatorPen = CreatePen(PS_SOLID, 1, colors.border);
    HGDIOBJ oldPen = SelectObject(hdc, separatorPen);
    MoveToEx(hdc, margin, lineY, nullptr);
    LineTo(hdc, client.right - margin, lineY);
    SelectObject(hdc, oldPen);
    DeleteObject(separatorPen);

    EndPaint(state->hwnd, &ps);
}

LRESULT CALLBACK AboutDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<AboutDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<AboutDialogState*>(create->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_CREATE: {
        const DialogColors colors = DialogColorsForTheme(state->dark);
        state->backgroundBrush = CreateSolidBrush(colors.background);
        state->uiFont = CreateUiFontForDpi(state->dpi);
        state->titleFont = CreateAboutTitleFont(state->uiFont, state->dpi);

        state->icon = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_ICON | SS_CENTERIMAGE, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(
            state->icon,
            STM_SETICON,
            reinterpret_cast<WPARAM>(LoadNativePadIcon(state->instance, ScaleForDpi(64, state->dpi), ScaleForDpi(64, state->dpi))),
            0);

        state->title = CreateWindowExW(0, L"STATIC", L"NativePad", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_NOPREFIX, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        state->description = CreateWindowExW(
            0,
            L"STATIC",
            L"A native C++ Win32 notepad replacement focused on fast startup, dark mode, and large-file safety.",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_NOPREFIX,
            0,
            0,
            0,
            0,
            hwnd,
            nullptr,
            nullptr,
            nullptr);

        const std::array<std::wstring, 4> metadataText{
            std::wstring(L"Version: ") + ExecutableVersionText(state->instance),
            std::wstring(L"Build: ") + kNativePadBuildTimestamp,
            std::wstring(L"Author: ") + kNativePadAuthor,
            std::wstring(L"Licence: ") + kNativePadLicense,
        };
        for (size_t i = 0; i < state->metadataLabels.size(); ++i) {
            state->metadataLabels[i] = CreateWindowExW(
                0,
                L"STATIC",
                metadataText[i].c_str(),
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_NOPREFIX,
                0,
                0,
                0,
                0,
                hwnd,
                nullptr,
                nullptr,
                nullptr);
        }

        state->ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);

        for (HWND control : AboutDialogControls(state)) {
            SetControlFont(control, state->uiFont);
        }
        SetControlFont(state->title, state->titleFont != nullptr ? state->titleFont : state->uiFont);

        ApplyAboutDialogTheme(state);
        LayoutAboutDialog(state);
        SetFocus(state->ok);
        return 0;
    }
    case WM_SIZE:
        LayoutAboutDialog(state);
        return 0;
    case WM_PAINT:
        PaintAboutDialog(state);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return AboutDialogCtlColor(state, wParam);
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
        return 1;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        if (state != nullptr) {
            DeleteUiFont(state->uiFont);
            if (state->titleFont != nullptr) {
                DeleteObject(state->titleFont);
            }
            if (state->backgroundBrush != nullptr) {
                DeleteObject(state->backgroundBrush);
            }
            state->uiFont = nullptr;
            state->titleFont = nullptr;
            state->backgroundBrush = nullptr;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void ShowAboutDialog(HWND owner, HINSTANCE instance, UINT dpi, bool dark) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &AboutDialogProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kAboutDialogClass;
    AssignWindowClassIcons(wc, instance);
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        MessageBoxW(owner, GetLastErrorText().c_str(), L"NativePad - About", MB_ICONERROR | MB_OK);
        return;
    }

    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int effectiveDpi = static_cast<int>(dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi);
    const int width = ScaleForDpi(500, static_cast<UINT>(effectiveDpi));
    const int height = ScaleForDpi(264, static_cast<UINT>(effectiveDpi));
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left - width) / 2);
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top - height) / 2);

    AboutDialogState state{};
    state.owner = owner;
    state.instance = instance;
    state.dpi = static_cast<UINT>(effectiveDpi);
    state.dark = dark;

    HWND dialog = CreateWindowExW(
        WS_EX_WINDOWEDGE | WS_EX_CONTROLPARENT,
        kAboutDialogClass,
        L"About NativePad",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        x,
        y,
        width,
        height,
        owner,
        nullptr,
        instance,
        &state);
    if (dialog == nullptr) {
        MessageBoxW(owner, GetLastErrorText().c_str(), L"NativePad - About", MB_ICONERROR | MB_OK);
        return;
    }
    ApplyWindowIcons(dialog, instance);

    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (MessageTargetsWindow(dialog, message) && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
            DestroyWindow(dialog);
            continue;
        }
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    SetFocus(owner);
}

struct GoToDialogState {
    // Custom modal Go To dialog state. The dialog uses the same parent-painted
    // borders and dark-control theme as Find/Replace and Font.
    HWND hwnd{};
    HWND owner{};
    HWND label{};
    HWND edit{};
    HWND rangeLabel{};
    HWND ok{};
    HWND cancel{};
    HFONT font{};
    HBRUSH backgroundBrush{};
    HBRUSH controlBrush{};
    UINT dpi{USER_DEFAULT_SCREEN_DPI};
    size_t currentLine{1};
    size_t maxLine{1};
    size_t selectedLine{0};
    bool dark{false};
    bool accepted{false};
};

std::array<HWND, 5> GoToDialogControls(GoToDialogState* state) {
    if (state == nullptr) {
        return {};
    }

    return {
        state->label,
        state->edit,
        state->rangeLabel,
        state->ok,
        state->cancel,
    };
}

void LayoutGoToDialog(GoToDialogState* state) {
    if (state == nullptr || state->hwnd == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(state->hwnd, &client);
    const int margin = ScaleForDpi(14, state->dpi);
    const int labelHeight = ScaleForDpi(18, state->dpi);
    const int editHeight = ScaleForDpi(24, state->dpi);
    const int hintHeight = ScaleForDpi(18, state->dpi);
    const int buttonWidth = ScaleForDpi(82, state->dpi);
    const int buttonHeight = ScaleForDpi(28, state->dpi);
    const int gap = ScaleForDpi(8, state->dpi);
    const int width = client.right - client.left;
    const int buttonTop = client.bottom - margin - buttonHeight;

    MoveWindow(state->label, margin, margin, width - (margin * 2), labelHeight, TRUE);
    MoveBorderedControl(state->edit, margin, margin + labelHeight + ScaleForDpi(4, state->dpi), width - (margin * 2), editHeight);
    MoveWindow(state->rangeLabel, margin, margin + labelHeight + editHeight + ScaleForDpi(8, state->dpi), width - (margin * 2), hintHeight, TRUE);
    MoveWindow(state->cancel, width - margin - buttonWidth, buttonTop, buttonWidth, buttonHeight, TRUE);
    MoveWindow(state->ok, width - margin - (buttonWidth * 2) - gap, buttonTop, buttonWidth, buttonHeight, TRUE);
    InvalidateRect(state->hwnd, nullptr, TRUE);
}

bool TryReadGoToLine(HWND owner, HWND edit, size_t maxLine, size_t& line) {
    // UI input is one-based to match Notepad. EditorView consumes zero-based
    // line indices, so convert only after validation succeeds.
    std::array<wchar_t, 64> buffer{};
    GetWindowTextW(edit, buffer.data(), static_cast<int>(buffer.size()));

    wchar_t* end = nullptr;
    const unsigned long long value = wcstoull(buffer.data(), &end, 10);
    while (end != nullptr && std::iswspace(*end) != 0) {
        ++end;
    }

    if (buffer[0] == L'\0' || end == buffer.data() || (end != nullptr && *end != L'\0') || value == 0 || value > maxLine) {
        std::wstring message = L"Line number must be between 1 and ";
        message += std::to_wstring(maxLine);
        message += L".";
        MessageBoxW(owner, message.c_str(), L"NativePad - Go To Line", MB_ICONINFORMATION | MB_OK);
        SetFocus(edit);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        return false;
    }

    line = static_cast<size_t>(value - 1);
    return true;
}

LRESULT GoToDialogCtlColor(GoToDialogState* state, UINT message, WPARAM wParam) {
    if (state == nullptr) {
        return 0;
    }

    const DialogColors colors = DialogColorsForTheme(state->dark);
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetTextColor(dc, colors.text);
    SetBkMode(dc, TRANSPARENT);

    if (message == WM_CTLCOLOREDIT) {
        SetBkColor(dc, colors.controlBackground);
        return reinterpret_cast<LRESULT>(state->controlBrush);
    }

    SetBkColor(dc, colors.background);
    return reinterpret_cast<LRESULT>(state->backgroundBrush);
}

void ApplyGoToDialogTheme(GoToDialogState* state) {
    if (state == nullptr) {
        return;
    }

    ApplyDarkFrame(state->hwnd, state->dark);
    for (HWND control : GoToDialogControls(state)) {
        ApplyDialogControlTheme(control, state->dark);
    }
}

void PaintGoToDialog(GoToDialogState* state) {
    // The edit border is drawn by the parent to avoid the light client-edge
    // border that Windows still applies to some themed edit controls.
    if (state == nullptr || state->hwnd == nullptr) {
        return;
    }

    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(state->hwnd, &ps);
    if (hdc == nullptr) {
        return;
    }

    const DialogColors colors = DialogColorsForTheme(state->dark);
    FillRect(hdc, &ps.rcPaint, state->backgroundBrush);
    DrawDialogChildBorder(state->hwnd, state->edit, hdc, colors);
    EndPaint(state->hwnd, &ps);
}

LRESULT CALLBACK GoToDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<GoToDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<GoToDialogState*>(create->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_CREATE: {
        const DialogColors colors = DialogColorsForTheme(state->dark);
        state->backgroundBrush = CreateSolidBrush(colors.background);
        state->controlBrush = CreateSolidBrush(colors.controlBackground);
        state->font = CreateUiFontForDpi(state->dpi);
        state->label = CreateWindowExW(0, L"STATIC", L"Line number:", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        state->edit = CreateWindowExW(
            0,
            L"EDIT",
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
            0,
            0,
            0,
            0,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGoToEditId)),
            nullptr,
            nullptr);
        std::wstring rangeText = L"Valid range: 1 - ";
        rangeText += std::to_wstring(state->maxLine);
        state->rangeLabel = CreateWindowExW(0, L"STATIC", rangeText.c_str(), WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        state->ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
        state->cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);

        for (HWND control : GoToDialogControls(state)) {
            SetControlFont(control, state->font);
        }

        std::wstring line = std::to_wstring(state->currentLine);
        SetWindowTextW(state->edit, line.c_str());
        ApplyGoToDialogTheme(state);
        LayoutGoToDialog(state);
        SetFocus(state->edit);
        SendMessageW(state->edit, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_SIZE:
        LayoutGoToDialog(state);
        return 0;
    case WM_PAINT:
        PaintGoToDialog(state);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            if (TryReadGoToLine(hwnd, state->edit, state->maxLine, state->selectedLine)) {
                state->accepted = true;
                DestroyWindow(hwnd);
            }
            return 0;
        case IDCANCEL:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return GoToDialogCtlColor(state, message, wParam);
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
        return 1;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        DeleteUiFont(state != nullptr ? state->font : nullptr);
        if (state != nullptr) {
            if (state->backgroundBrush != nullptr) {
                DeleteObject(state->backgroundBrush);
            }
            if (state->controlBrush != nullptr) {
                DeleteObject(state->controlBrush);
            }
            state->font = nullptr;
            state->backgroundBrush = nullptr;
            state->controlBrush = nullptr;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

std::optional<size_t> ShowGoToLineDialog(HWND owner, HINSTANCE instance, UINT dpi, bool dark, size_t currentLine, size_t maxLine) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &GoToDialogProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kGoToDialogClass;
    AssignWindowClassIcons(wc, instance);
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        MessageBoxW(owner, GetLastErrorText().c_str(), L"NativePad - Go To Line", MB_ICONERROR | MB_OK);
        return std::nullopt;
    }

    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int width = ScaleForDpi(328, dpi);
    const int height = ScaleForDpi(164, dpi);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left - width) / 2);
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top - height) / 2);

    GoToDialogState state{};
    state.owner = owner;
    state.dpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
    state.currentLine = std::max<size_t>(1, currentLine);
    state.maxLine = std::max<size_t>(1, maxLine);
    state.dark = dark;

    HWND dialog = CreateWindowExW(
        WS_EX_WINDOWEDGE | WS_EX_CONTROLPARENT,
        kGoToDialogClass,
        L"Go To Line",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        x,
        y,
        width,
        height,
        owner,
        nullptr,
        instance,
        &state);
    if (dialog == nullptr) {
        MessageBoxW(owner, GetLastErrorText().c_str(), L"NativePad - Go To Line", MB_ICONERROR | MB_OK);
        return std::nullopt;
    }
    ApplyWindowIcons(dialog, instance);

    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (MessageTargetsWindow(dialog, message) && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
            DestroyWindow(dialog);
            continue;
        }
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    SetFocus(owner);

    if (state.accepted) {
        return state.selectedLine;
    }

    return std::nullopt;
}

enum class FindReplaceDialogAction {
    Closed,
    FindNext,
    Replace,
    ReplaceAll,
};

struct FindReplaceDialogRequest {
    HWND dialog{};
    FindReplaceDialogAction action{FindReplaceDialogAction::Closed};
    std::wstring findText;
    std::wstring replaceText;
    bool matchCase{false};
    bool down{true};
};

struct FindReplaceDialogState {
    // Modeless Find/Replace dialog. It sends compact action requests back to
    // AppWindow instead of relying on the common dialog FINDREPLACE structure.
    HWND hwnd{};
    HWND owner{};
    HWND findLabel{};
    HWND findEdit{};
    HWND replaceLabel{};
    HWND replaceEdit{};
    HWND matchCase{};
    HWND upRadio{};
    HWND downRadio{};
    HWND findNext{};
    HWND replace{};
    HWND replaceAll{};
    HWND cancel{};
    HFONT font{};
    HBRUSH backgroundBrush{};
    HBRUSH controlBrush{};
    UINT dpi{USER_DEFAULT_SCREEN_DPI};
    bool dark{false};
    bool replaceMode{false};
    bool closedNotified{false};
    bool matchCaseChecked{false};
    bool downSelected{true};
    RECT directionFrame{};
    std::wstring initialFindText;
    std::wstring initialReplaceText;
    bool initialMatchCase{false};
    bool initialDown{true};
};

bool FindDialogHasFindText(const FindReplaceDialogState* state) {
    return state != nullptr && GetWindowTextLengthW(state->findEdit) > 0;
}

void ReadFindReplaceDialogRequest(FindReplaceDialogState* state, FindReplaceDialogAction action, FindReplaceDialogRequest& request) {
    request.dialog = state->hwnd;
    request.action = action;
    request.findText = ControlText(state->findEdit);
    request.replaceText = state->replaceMode ? ControlText(state->replaceEdit) : state->initialReplaceText;
    request.matchCase = state->matchCaseChecked;
    request.down = state->downSelected;
}

void SendFindReplaceAction(FindReplaceDialogState* state, FindReplaceDialogAction action) {
    if (state == nullptr || state->owner == nullptr) {
        return;
    }

    FindReplaceDialogRequest request{};
    if (action == FindReplaceDialogAction::Closed) {
        request.dialog = state->hwnd;
        request.action = action;
    } else {
        ReadFindReplaceDialogRequest(state, action, request);
    }
    SendMessageW(state->owner, WM_NATIVEPAD_FIND_REPLACE, 0, reinterpret_cast<LPARAM>(&request));
}

void NotifyFindReplaceClosed(FindReplaceDialogState* state) {
    if (state == nullptr || state->closedNotified) {
        return;
    }

    state->closedNotified = true;
    SendFindReplaceAction(state, FindReplaceDialogAction::Closed);
}

void UpdateFindReplaceButtons(FindReplaceDialogState* state) {
    if (state == nullptr) {
        return;
    }

    const BOOL enabled = FindDialogHasFindText(state) ? TRUE : FALSE;
    EnableWindow(state->findNext, enabled);
    if (state->replaceMode) {
        EnableWindow(state->replace, enabled);
        EnableWindow(state->replaceAll, enabled);
    }
}

void LayoutFindReplaceDialog(FindReplaceDialogState* state) {
    // One layout routine handles both Find and Replace. Fixed row metrics keep
    // owner-draw radio/check controls from overlapping the command buttons.
    if (state == nullptr || state->hwnd == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(state->hwnd, &client);
    const int margin = ScaleForDpi(12, state->dpi);
    const int gap = ScaleForDpi(8, state->dpi);
    const int labelWidth = ScaleForDpi(78, state->dpi);
    const int rowHeight = ScaleForDpi(24, state->dpi);
    const int labelHeight = ScaleForDpi(18, state->dpi);
    const int buttonWidth = ScaleForDpi(110, state->dpi);
    const int buttonHeight = ScaleForDpi(28, state->dpi);
    const int buttonGap = ScaleForDpi(8, state->dpi);
    const int rightPaneLeft = client.right - margin - buttonWidth;
    const int editLeft = margin + labelWidth + gap;
    const int editWidth = std::max(ScaleForDpi(120, state->dpi), rightPaneLeft - gap - editLeft);
    const int top = margin + ScaleForDpi(2, state->dpi);

    MoveWindow(state->findLabel, margin, top + ScaleForDpi(3, state->dpi), labelWidth, labelHeight, TRUE);
    MoveBorderedControl(state->findEdit, editLeft, top, editWidth, rowHeight);
    MoveWindow(state->findNext, rightPaneLeft, top, buttonWidth, buttonHeight, TRUE);

    int nextY = top + rowHeight + gap;
    if (state->replaceMode) {
        MoveWindow(state->replaceLabel, margin, nextY + ScaleForDpi(3, state->dpi), labelWidth, labelHeight, TRUE);
        MoveBorderedControl(state->replaceEdit, editLeft, nextY, editWidth, rowHeight);
        MoveWindow(state->replace, rightPaneLeft, top + buttonHeight + buttonGap, buttonWidth, buttonHeight, TRUE);
        nextY += rowHeight + gap;
        MoveWindow(state->replaceAll, rightPaneLeft, top + ((buttonHeight + buttonGap) * 2), buttonWidth, buttonHeight, TRUE);
    }

    const int groupTop = state->replaceMode ? top + ScaleForDpi(84, state->dpi) : top + ScaleForDpi(68, state->dpi);
    const int preferredGroupWidth = ScaleForDpi(196, state->dpi);
    const int minDirectionLeft = margin + ScaleForDpi(164, state->dpi);
    const int directionLeft = std::max(minDirectionLeft, rightPaneLeft - gap - preferredGroupWidth);
    const int groupWidth = std::max(ScaleForDpi(180, state->dpi), rightPaneLeft - gap - directionLeft);
    const int groupHeight = ScaleForDpi(64, state->dpi);
    state->directionFrame = {directionLeft, groupTop, directionLeft + groupWidth, groupTop + groupHeight};
    const int matchWidth = std::max(ScaleForDpi(96, state->dpi), directionLeft - margin - gap);
    const int optionTop = groupTop + ScaleForDpi(30, state->dpi);
    const int radioLeft = directionLeft + ScaleForDpi(16, state->dpi);
    const int radioGap = ScaleForDpi(18, state->dpi);
    const int radioRight = directionLeft + groupWidth - ScaleForDpi(12, state->dpi);
    // Keep owner-draw radio labels inside the hand-painted group frame across DPI
    // sizes instead of relying on fixed offsets that can cross the right border.
    const int radioWidth = std::max(ScaleForDpi(62, state->dpi), (radioRight - radioLeft - radioGap) / 2);
    const int downLeft = std::min(radioLeft + radioWidth + radioGap, radioRight - ScaleForDpi(68, state->dpi));
    MoveWindow(state->matchCase, margin, optionTop, matchWidth, rowHeight, TRUE);
    MoveWindow(state->upRadio, radioLeft, optionTop, radioWidth, rowHeight, TRUE);
    MoveWindow(state->downRadio, downLeft, optionTop, radioRight - downLeft, rowHeight, TRUE);

    const int cancelTop = state->replaceMode ? top + ((buttonHeight + buttonGap) * 3)
                                             : top + buttonHeight + ScaleForDpi(16, state->dpi);
    MoveWindow(state->cancel, rightPaneLeft, cancelTop, buttonWidth, buttonHeight, TRUE);
    InvalidateRect(state->hwnd, nullptr, TRUE);
}

LRESULT FindReplaceDialogCtlColor(FindReplaceDialogState* state, UINT message, WPARAM wParam) {
    if (state == nullptr) {
        return 0;
    }

    const DialogColors colors = DialogColorsForTheme(state->dark);
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetTextColor(dc, colors.text);
    SetBkMode(dc, TRANSPARENT);

    if (message == WM_CTLCOLOREDIT) {
        SetBkColor(dc, colors.controlBackground);
        return reinterpret_cast<LRESULT>(state->controlBrush);
    }

    SetBkColor(dc, colors.background);
    return reinterpret_cast<LRESULT>(state->backgroundBrush);
}

void ApplyFindReplaceDialogTheme(FindReplaceDialogState* state) {
    if (state == nullptr) {
        return;
    }

    ApplyDarkFrame(state->hwnd, state->dark);
    const std::array<HWND, 11> controls{
        state->findLabel,
        state->findEdit,
        state->replaceLabel,
        state->replaceEdit,
        state->matchCase,
        state->upRadio,
        state->downRadio,
        state->findNext,
        state->replace,
        state->replaceAll,
        state->cancel,
    };
    for (HWND control : controls) {
        ApplyDialogControlTheme(control, state->dark);
    }
}

void PaintFindReplaceDialog(FindReplaceDialogState* state) {
    if (state == nullptr || state->hwnd == nullptr) {
        return;
    }

    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(state->hwnd, &ps);
    if (hdc == nullptr) {
        return;
    }

    const DialogColors colors = DialogColorsForTheme(state->dark);
    FillRect(hdc, &ps.rcPaint, state->backgroundBrush);
    DrawDialogChildBorder(state->hwnd, state->findEdit, hdc, colors);
    if (state->replaceMode) {
        DrawDialogChildBorder(state->hwnd, state->replaceEdit, hdc, colors);
    }

    if (state->directionFrame.right > state->directionFrame.left && state->directionFrame.bottom > state->directionFrame.top) {
        RECT frame = state->directionFrame;
        const int textGap = ScaleForDpi(8, state->dpi);
        const int textLeft = frame.left + textGap;
        const int textRight = textLeft + ScaleForDpi(62, state->dpi);
        const int midY = frame.top + ScaleForDpi(9, state->dpi);
        HPEN borderPen = CreatePen(PS_SOLID, 1, colors.border);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        MoveToEx(hdc, frame.left, midY, nullptr);
        LineTo(hdc, textLeft - ScaleForDpi(3, state->dpi), midY);
        MoveToEx(hdc, textRight + ScaleForDpi(3, state->dpi), midY, nullptr);
        LineTo(hdc, frame.right, midY);
        LineTo(hdc, frame.right, frame.bottom);
        LineTo(hdc, frame.left, frame.bottom);
        LineTo(hdc, frame.left, midY);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        HGDIOBJ oldFont = SelectObject(hdc, state->font);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, colors.text);
        RECT labelRect{textLeft, frame.top, textRight, frame.top + ScaleForDpi(18, state->dpi)};
        DrawTextW(hdc, L"Direction", -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, oldFont);
    }
    EndPaint(state->hwnd, &ps);
}

bool IsFindReplaceToggleId(UINT id) {
    return id == kFindMatchCaseId || id == kFindDirectionUpId || id == kFindDirectionDownId;
}

void DrawCheckGlyph(HDC hdc, RECT rect, COLORREF color, UINT dpi) {
    HPEN pen = CreatePen(PS_SOLID, std::max(1, ScaleForDpi(2, dpi)), color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    const int left = rect.left + ScaleForDpi(4, dpi);
    const int midY = rect.top + ((rect.bottom - rect.top) / 2);
    MoveToEx(hdc, left, midY, nullptr);
    LineTo(hdc, left + ScaleForDpi(4, dpi), midY + ScaleForDpi(4, dpi));
    LineTo(hdc, left + ScaleForDpi(11, dpi), midY - ScaleForDpi(5, dpi));
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void DrawFindReplaceToggle(FindReplaceDialogState* state, DRAWITEMSTRUCT* draw) {
    // Native checkbox/radio painting still leaks light pixels in some dark-mode
    // themes, so these toggles are fully owner-drawn.
    if (state == nullptr || draw == nullptr) {
        return;
    }

    const DialogColors colors = DialogColorsForTheme(state->dark);
    HBRUSH background = CreateSolidBrush(colors.background);
    FillRect(draw->hDC, &draw->rcItem, background);
    DeleteObject(background);

    const bool checked = draw->CtlID == kFindMatchCaseId ? state->matchCaseChecked
                                                         : (draw->CtlID == kFindDirectionDownId ? state->downSelected : !state->downSelected);
    const bool focused = (draw->itemState & ODS_FOCUS) != 0;
    const bool disabled = (draw->itemState & ODS_DISABLED) != 0;
    const COLORREF glyphColor = disabled ? colors.disabledText : colors.text;
    const COLORREF borderColor = focused ? colors.focusBorder : colors.border;
    const int glyphSize = ScaleForDpi(16, state->dpi);
    RECT glyphRect{
        draw->rcItem.left,
        draw->rcItem.top + ((draw->rcItem.bottom - draw->rcItem.top - glyphSize) / 2),
        draw->rcItem.left + glyphSize,
        draw->rcItem.top + ((draw->rcItem.bottom - draw->rcItem.top - glyphSize) / 2) + glyphSize,
    };

    HPEN borderPen = CreatePen(PS_SOLID, 1, borderColor);
    HGDIOBJ oldPen = SelectObject(draw->hDC, borderPen);
    HGDIOBJ oldBrush = SelectObject(draw->hDC, GetStockObject(NULL_BRUSH));
    if (draw->CtlID == kFindMatchCaseId) {
        Rectangle(draw->hDC, glyphRect.left, glyphRect.top, glyphRect.right, glyphRect.bottom);
        if (checked) {
            DrawCheckGlyph(draw->hDC, glyphRect, glyphColor, state->dpi);
        }
    } else {
        Ellipse(draw->hDC, glyphRect.left, glyphRect.top, glyphRect.right, glyphRect.bottom);
        if (checked) {
            HBRUSH dot = CreateSolidBrush(colors.focusBorder);
            RECT dotRect = glyphRect;
            InflateRect(&dotRect, -ScaleForDpi(5, state->dpi), -ScaleForDpi(5, state->dpi));
            HGDIOBJ oldDotBrush = SelectObject(draw->hDC, dot);
            HGDIOBJ oldDotPen = SelectObject(draw->hDC, GetStockObject(NULL_PEN));
            Ellipse(draw->hDC, dotRect.left, dotRect.top, dotRect.right, dotRect.bottom);
            SelectObject(draw->hDC, oldDotPen);
            SelectObject(draw->hDC, oldDotBrush);
            DeleteObject(dot);
        }
    }
    SelectObject(draw->hDC, oldBrush);
    SelectObject(draw->hDC, oldPen);
    DeleteObject(borderPen);

    std::wstring text = ControlText(draw->hwndItem);
    HFONT font = state->font != nullptr ? state->font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ oldFont = SelectObject(draw->hDC, font);
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, disabled ? colors.disabledText : colors.text);
    RECT textRect = draw->rcItem;
    textRect.left += glyphSize + ScaleForDpi(6, state->dpi);
    DrawTextW(draw->hDC, text.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(draw->hDC, oldFont);
}

LRESULT CALLBACK FindReplaceDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<FindReplaceDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<FindReplaceDialogState*>(create->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_CREATE: {
        const DialogColors colors = DialogColorsForTheme(state->dark);
        state->backgroundBrush = CreateSolidBrush(colors.background);
        state->controlBrush = CreateSolidBrush(colors.controlBackground);
        state->font = CreateUiFontForDpi(state->dpi);

        state->findLabel = CreateWindowExW(0, L"STATIC", L"Find what:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        state->findEdit = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFindTextEditId)), nullptr, nullptr);
        if (state->replaceMode) {
            state->replaceLabel = CreateWindowExW(0, L"STATIC", L"Replace with:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
            state->replaceEdit = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReplaceTextEditId)), nullptr, nullptr);
        }
        state->matchCase = CreateWindowExW(0, L"BUTTON", L"Match case", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFindMatchCaseId)), nullptr, nullptr);
        state->upRadio = CreateWindowExW(0, L"BUTTON", L"Up", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFindDirectionUpId)), nullptr, nullptr);
        state->downRadio = CreateWindowExW(0, L"BUTTON", L"Down", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFindDirectionDownId)), nullptr, nullptr);
        state->findNext = CreateWindowExW(0, L"BUTTON", L"Find Next", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
        if (state->replaceMode) {
            state->replace = CreateWindowExW(0, L"BUTTON", L"Replace", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFindReplaceButtonId)), nullptr, nullptr);
            state->replaceAll = CreateWindowExW(0, L"BUTTON", L"Replace All", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFindReplaceAllButtonId)), nullptr, nullptr);
        }
        state->cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);

        const std::array<HWND, 11> controls{
            state->findLabel,
            state->findEdit,
            state->replaceLabel,
            state->replaceEdit,
            state->matchCase,
            state->upRadio,
            state->downRadio,
            state->findNext,
            state->replace,
            state->replaceAll,
            state->cancel,
        };
        for (HWND control : controls) {
            SetControlFont(control, state->font);
        }

        SetControlText(state->findEdit, state->initialFindText);
        if (state->replaceMode) {
            SetControlText(state->replaceEdit, state->initialReplaceText);
        }
        state->matchCaseChecked = state->initialMatchCase;
        state->downSelected = state->initialDown;

        ApplyFindReplaceDialogTheme(state);
        LayoutFindReplaceDialog(state);
        UpdateFindReplaceButtons(state);
        SetFocus(state->findEdit);
        SendMessageW(state->findEdit, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_SIZE:
        LayoutFindReplaceDialog(state);
        return 0;
    case WM_PAINT:
        PaintFindReplaceDialog(state);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            if (FindDialogHasFindText(state)) {
                SendFindReplaceAction(state, FindReplaceDialogAction::FindNext);
            }
            return 0;
        case IDCANCEL:
            DestroyWindow(hwnd);
            return 0;
        case kFindMatchCaseId: {
            state->matchCaseChecked = !state->matchCaseChecked;
            InvalidateRect(state->matchCase, nullptr, TRUE);
            return 0;
        }
        case kFindDirectionUpId:
            state->downSelected = false;
            InvalidateRect(state->upRadio, nullptr, TRUE);
            InvalidateRect(state->downRadio, nullptr, TRUE);
            return 0;
        case kFindDirectionDownId:
            state->downSelected = true;
            InvalidateRect(state->upRadio, nullptr, TRUE);
            InvalidateRect(state->downRadio, nullptr, TRUE);
            return 0;
        case kFindReplaceButtonId:
            if (FindDialogHasFindText(state)) {
                SendFindReplaceAction(state, FindReplaceDialogAction::Replace);
            }
            return 0;
        case kFindReplaceAllButtonId:
            if (FindDialogHasFindText(state)) {
                SendFindReplaceAction(state, FindReplaceDialogAction::ReplaceAll);
            }
            return 0;
        case kFindTextEditId:
            if (HIWORD(wParam) == EN_CHANGE) {
                UpdateFindReplaceButtons(state);
            }
            return 0;
        default:
            break;
        }
        break;
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (draw != nullptr && IsFindReplaceToggleId(draw->CtlID)) {
            DrawFindReplaceToggle(state, draw);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return FindReplaceDialogCtlColor(state, message, wParam);
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
        return 1;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        NotifyFindReplaceClosed(state);
        DeleteUiFont(state != nullptr ? state->font : nullptr);
        if (state != nullptr) {
            if (state->backgroundBrush != nullptr) {
                DeleteObject(state->backgroundBrush);
            }
            if (state->controlBrush != nullptr) {
                DeleteObject(state->controlBrush);
            }
            state->font = nullptr;
            state->backgroundBrush = nullptr;
            state->controlBrush = nullptr;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete state;
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND ShowFindReplaceDialog(
    HWND owner,
    HINSTANCE instance,
    UINT dpi,
    bool dark,
    bool replaceMode,
    std::wstring_view findText,
    std::wstring_view replaceText,
    bool matchCase,
    bool down) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &FindReplaceDialogProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kFindReplaceDialogClass;
    AssignWindowClassIcons(wc, instance);
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        MessageBoxW(owner, GetLastErrorText().c_str(), replaceMode ? L"NativePad - Replace" : L"NativePad - Find", MB_ICONERROR | MB_OK);
        return nullptr;
    }

    dpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int width = ScaleForDpi(replaceMode ? 520 : 500, dpi);
    const int height = ScaleForDpi(replaceMode ? 238 : 198, dpi);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left - width) / 2);
    const int y = ownerRect.top + ScaleForDpi(82, dpi);

    auto* state = new FindReplaceDialogState();
    state->owner = owner;
    state->dpi = dpi;
    state->dark = dark;
    state->replaceMode = replaceMode;
    state->initialFindText = std::wstring(findText);
    state->initialReplaceText = std::wstring(replaceText);
    state->initialMatchCase = matchCase;
    state->initialDown = down;

    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CONTROLPARENT,
        kFindReplaceDialogClass,
        replaceMode ? L"Replace" : L"Find",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        x,
        y,
        width,
        height,
        owner,
        nullptr,
        instance,
        state);
    if (dialog == nullptr) {
        MessageBoxW(owner, GetLastErrorText().c_str(), replaceMode ? L"NativePad - Replace" : L"NativePad - Find", MB_ICONERROR | MB_OK);
        return nullptr;
    }
    ApplyWindowIcons(dialog, instance);

    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    return dialog;
}

class AppWindow {
public:
    explicit AppWindow(HINSTANCE instance)
        : instance_(instance),
          dpi_(GetDpiForSystem()),
          darkMode_(IsSystemDarkMode()) {
        LoadPreferences();
    }

    ~AppWindow() {
        if (fileMenu_ != nullptr) {
            DestroyMenu(fileMenu_);
        }
        if (editMenu_ != nullptr) {
            DestroyMenu(editMenu_);
        }
        if (formatMenu_ != nullptr) {
            DestroyMenu(formatMenu_);
        }
        if (viewMenu_ != nullptr) {
            DestroyMenu(viewMenu_);
        }
        if (helpMenu_ != nullptr) {
            DestroyMenu(helpMenu_);
        }
        if (menuBackgroundBrush_ != nullptr) {
            DeleteObject(menuBackgroundBrush_);
        }
        if (uiFont_ != nullptr && uiFont_ != GetStockObject(DEFAULT_GUI_FONT)) {
            DeleteObject(uiFont_);
        }
    }

    bool Create(int showCommand) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &AppWindow::StaticWndProc;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        AssignWindowClassIcons(wc, instance_);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kWindowClass;

        if (RegisterClassExW(&wc) == 0) {
            MessageBoxW(nullptr, GetLastErrorText().c_str(), L"NativePad", MB_ICONERROR | MB_OK);
            return false;
        }

        if (!NativePad::EditorView::Register(instance_)) {
            MessageBoxW(nullptr, GetLastErrorText().c_str(), L"NativePad", MB_ICONERROR | MB_OK);
            return false;
        }

        if (!RegisterChildClasses()) {
            MessageBoxW(nullptr, GetLastErrorText().c_str(), L"NativePad", MB_ICONERROR | MB_OK);
            return false;
        }

        const UINT systemDpi = GetDpiForSystem();
        int x = CW_USEDEFAULT;
        int y = CW_USEDEFAULT;
        int width = ScaleForDpi(1000, systemDpi);
        int height = ScaleForDpi(700, systemDpi);
        if (hasSavedWindowRect_) {
            x = savedWindowRect_.left;
            y = savedWindowRect_.top;
            width = savedWindowRect_.right - savedWindowRect_.left;
            height = savedWindowRect_.bottom - savedWindowRect_.top;
        }

        hwnd_ = CreateWindowExW(
            0,
            kWindowClass,
            L"NativePad",
            WS_OVERLAPPEDWINDOW,
            x,
            y,
            width,
            height,
            nullptr,
            nullptr,
            instance_,
            this);

        if (hwnd_ == nullptr) {
            MessageBoxW(nullptr, GetLastErrorText().c_str(), L"NativePad", MB_ICONERROR | MB_OK);
            return false;
        }

        int actualShowCommand = showCommand;
        if (savedWindowMaximized_ && showCommand != SW_SHOWMINIMIZED && showCommand != SW_SHOWMINNOACTIVE && showCommand != SW_MINIMIZE) {
            actualShowCommand = SW_SHOWMAXIMIZED;
        }
        ShowWindow(hwnd_, actualShowCommand);
        UpdateWindow(hwnd_);
        return true;
    }

    HWND Hwnd() const {
        return hwnd_;
    }

    bool TranslateModelessDialog(MSG* message) const {
        if (findDialog_ == nullptr || message == nullptr) {
            return false;
        }

        if (MessageTargetsWindow(findDialog_, *message) && message->message == WM_KEYDOWN && message->wParam == VK_ESCAPE) {
            DestroyWindow(findDialog_);
            return true;
        }

        return IsDialogMessageW(findDialog_, message);
    }

    void OpenInitialFile(const std::wstring& path) {
        if (!path.empty()) {
            OpenDocument(path);
        }
    }

private:
    void LoadPreferences() {
        // NativePad stores only UI/editor preferences. Document metadata remains
        // per-file state so opening an ANSI file cannot change the default for a
        // later new document.
        if (auto forced = ReadSettingsDword(L"DarkModeForced")) {
            darkModeForced_ = *forced != 0;
        }
        if (darkModeForced_) {
            if (auto dark = ReadSettingsDword(L"DarkMode")) {
                darkMode_ = *dark != 0;
            }
        }

        if (auto wordWrap = ReadSettingsDword(L"WordWrap")) {
            preferredWordWrap_ = *wordWrap != 0;
        }
        if (auto lineNumbers = ReadSettingsDword(L"LineNumbers")) {
            preferredLineNumbers_ = *lineNumbers != 0;
        }
        if (auto visible = ReadSettingsDword(L"StatusBarVisible")) {
            statusBarVisible_ = *visible != 0;
        }

        if (auto family = ReadSettingsString(L"FontFamily"); family && !family->empty()) {
            preferredFont_.family = *family;
        }
        if (auto sizeTenths = ReadSettingsDword(L"FontSizeTenths"); sizeTenths && *sizeTenths >= 60 && *sizeTenths <= 720) {
            preferredFont_.sizeDips = static_cast<float>(*sizeTenths) / 10.0f;
        }
        if (auto weight = ReadSettingsDword(L"FontWeight")) {
            preferredFont_.weight = std::clamp<LONG>(static_cast<LONG>(*weight), FW_THIN, FW_HEAVY);
        }
        if (auto italic = ReadSettingsDword(L"FontItalic")) {
            preferredFont_.italic = *italic != 0;
        }

        const auto marginLeft = ReadSettingsInt(L"MarginLeft");
        const auto marginTop = ReadSettingsInt(L"MarginTop");
        const auto marginRight = ReadSettingsInt(L"MarginRight");
        const auto marginBottom = ReadSettingsInt(L"MarginBottom");
        if (marginLeft && marginTop && marginRight && marginBottom) {
            RECT margins{*marginLeft, *marginTop, *marginRight, *marginBottom};
            if (margins.left >= 0 && margins.top >= 0 && margins.right >= 0 && margins.bottom >= 0 &&
                margins.left <= 10000 && margins.top <= 10000 && margins.right <= 10000 && margins.bottom <= 10000) {
                pageMarginsThousandths_ = margins;
            }
        }

        const auto left = ReadSettingsInt(L"WindowLeft");
        const auto top = ReadSettingsInt(L"WindowTop");
        const auto right = ReadSettingsInt(L"WindowRight");
        const auto bottom = ReadSettingsInt(L"WindowBottom");
        if (left && top && right && bottom) {
            RECT rect{*left, *top, *right, *bottom};
            if (rect.right - rect.left >= 320 && rect.bottom - rect.top >= 240) {
                savedWindowRect_ = rect;
                hasSavedWindowRect_ = true;
            }
        }
        if (auto maximized = ReadSettingsDword(L"WindowMaximized")) {
            savedWindowMaximized_ = *maximized != 0;
        }
    }

    void SavePreferences() const {
        HKEY key = nullptr;
        if (!CreateSettingsKey(key)) {
            return;
        }

        WriteSettingsDword(key, L"DarkModeForced", darkModeForced_ ? 1u : 0u);
        WriteSettingsDword(key, L"DarkMode", darkMode_ ? 1u : 0u);
        WriteSettingsDword(key, L"WordWrap", editorView_.WordWrap() ? 1u : 0u);
        WriteSettingsDword(key, L"LineNumbers", editorView_.ShowLineNumbers() ? 1u : 0u);
        WriteSettingsDword(key, L"StatusBarVisible", statusBarVisible_ ? 1u : 0u);
        WriteSettingsInt(key, L"MarginLeft", pageMarginsThousandths_.left);
        WriteSettingsInt(key, L"MarginTop", pageMarginsThousandths_.top);
        WriteSettingsInt(key, L"MarginRight", pageMarginsThousandths_.right);
        WriteSettingsInt(key, L"MarginBottom", pageMarginsThousandths_.bottom);

        const NativePad::EditorFontSpec& font = editorView_.Font();
        WriteSettingsString(key, L"FontFamily", font.family);
        WriteSettingsDword(key, L"FontSizeTenths", static_cast<DWORD>(std::lround(font.sizeDips * 10.0f)));
        WriteSettingsDword(key, L"FontWeight", static_cast<DWORD>(font.weight));
        WriteSettingsDword(key, L"FontItalic", font.italic ? 1u : 0u);

        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (GetWindowPlacement(hwnd_, &placement)) {
            WriteSettingsInt(key, L"WindowLeft", placement.rcNormalPosition.left);
            WriteSettingsInt(key, L"WindowTop", placement.rcNormalPosition.top);
            WriteSettingsInt(key, L"WindowRight", placement.rcNormalPosition.right);
            WriteSettingsInt(key, L"WindowBottom", placement.rcNormalPosition.bottom);
            WriteSettingsDword(key, L"WindowMaximized", placement.showCmd == SW_SHOWMAXIMIZED ? 1u : 0u);
        }

        RegCloseKey(key);
    }

    bool RegisterChildClasses() const {
        WNDCLASSEXW menuClass{};
        menuClass.cbSize = sizeof(menuClass);
        menuClass.lpfnWndProc = &AppWindow::StaticMenuStripProc;
        menuClass.hInstance = instance_;
        menuClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        menuClass.lpszClassName = kMenuStripClass;

        if (RegisterClassExW(&menuClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        return true;
    }

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        AppWindow* app = nullptr;

        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<AppWindow*>(create->lpCreateParams);
            app->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        } else {
            app = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (app != nullptr) {
            return app->WndProc(message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static LRESULT CALLBACK StaticMenuStripProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        AppWindow* app = nullptr;

        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<AppWindow*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        } else {
            app = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (app != nullptr) {
            return app->MenuStripProc(hwnd, message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }


    LRESULT WndProc(UINT message, WPARAM wParam, LPARAM lParam) {
        // AppWindow owns all top-level UI messages. Child controls forward only
        // high-level notifications so document state changes stay centralized.
        switch (message) {
        case WM_CREATE:
            return OnCreate();
        case WM_SIZE:
            Layout();
            return 0;
        case WM_CONTEXTMENU:
            if (reinterpret_cast<HWND>(wParam) == editor_) {
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ShowEditorContextMenu(point);
                return 0;
            }
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        case WM_DPICHANGED:
            OnDpiChanged(HIWORD(wParam), reinterpret_cast<RECT*>(lParam));
            return 0;
        case NativePad::WM_EDITOR_CHANGED:
            dirty_ = true;
            UpdateTitle();
            UpdateStatus();
            return 0;
        case NativePad::WM_EDITOR_CURSOR_CHANGED:
            UpdateStatus();
            return 0;
        case WM_NATIVEPAD_PRINT_COMPLETE:
            OnPrintComplete(reinterpret_cast<PrintResult*>(lParam));
            return 0;
        case WM_NATIVEPAD_FIND_REPLACE:
            OnFindReplaceRequest(reinterpret_cast<FindReplaceDialogRequest*>(lParam));
            return 0;
        case WM_COMMAND:
            OnCommand(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
            return 0;
        case WM_NOTIFY:
            return OnNotify(reinterpret_cast<NMHDR*>(lParam));
        case WM_INITMENUPOPUP:
            UpdateMenuState(reinterpret_cast<HMENU>(wParam));
            return 0;
        case WM_MEASUREITEM:
            return OnMeasureItem(reinterpret_cast<MEASUREITEMSTRUCT*>(lParam));
        case WM_DRAWITEM:
            return OnDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        case WM_SYSKEYDOWN:
            if (OpenMenuFromKey(wParam)) {
                return 0;
            }
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                const HWND target = reinterpret_cast<HWND>(wParam);
                if (target == hwnd_ || target == status_ || target == menuStrip_) {
                    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                    return TRUE;
                }
            }
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            if (!darkModeForced_) {
                darkMode_ = IsSystemDarkMode();
                ApplyTheme();
            }
            return 0;
        case WM_DROPFILES:
            OnDropFiles(reinterpret_cast<HDROP>(wParam));
            return 0;
        case WM_CLOSE:
            if (ConfirmSaveIfDirty()) {
                SavePreferences();
                DestroyWindow(hwnd_);
            }
            return 0;
        case WM_DESTROY:
            if (findDialog_ != nullptr) {
                DestroyWindow(findDialog_);
                findDialog_ = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        }
    }

    LRESULT OnCreate() {
        // Build the window chrome first, then attach the editor view to the
        // current DocumentBuffer. Large files can later swap in MappedTextDocument.
        dpi_ = GetDpiForWindow(hwnd_);
        RefreshUiFont();
        BuildPopupMenus();

        menuStrip_ = CreateWindowExW(
            0,
            kMenuStripClass,
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0,
            0,
            0,
            0,
            hwnd_,
            nullptr,
            instance_,
            this);

        if (menuStrip_ == nullptr) {
            MessageBoxW(hwnd_, L"Could not create the menu strip.", L"NativePad", MB_ICONERROR | MB_OK);
            return -1;
        }

        if (!editorView_.Create(hwnd_, instance_, &document_)) {
            MessageBoxW(hwnd_, L"Could not create the DirectWrite editor view.", L"NativePad", MB_ICONERROR | MB_OK);
            return -1;
        }
        editor_ = editorView_.Hwnd();

        status_ = CreateWindowExW(
            0,
            L"STATIC",
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_OWNERDRAW,
            0,
            0,
            0,
            0,
            hwnd_,
            nullptr,
            instance_,
            nullptr);

        if (status_ == nullptr) {
            MessageBoxW(hwnd_, L"Could not create the native status bar.", L"NativePad", MB_ICONERROR | MB_OK);
            return -1;
        }

        DragAcceptFiles(hwnd_, TRUE);

        editorView_.OnDpiChanged(dpi_);
        editorView_.SetFont(preferredFont_);
        editorView_.SetShowLineNumbers(preferredLineNumbers_);
        editorView_.SetWordWrap(preferredWordWrap_);
        ShowWindow(status_, statusBarVisible_ ? SW_SHOW : SW_HIDE);
        ApplyTheme();
        UpdateTitle();
        UpdateStatus();
        Layout();
        SetFocus(editor_);

        return 0;
    }

    void AppendMenuCommand(HMENU menu, UINT id) const {
        AppendMenuW(menu, MF_OWNERDRAW, id, reinterpret_cast<LPCWSTR>(static_cast<UINT_PTR>(id)));
    }

    void AppendMenuSeparator(HMENU menu) const {
        AppendMenuW(menu, MF_OWNERDRAW | MF_SEPARATOR, 0, nullptr);
    }

    void BuildPopupMenus() {
        // Menus are owner-drawn so dark mode can control backgrounds, separators,
        // accelerator text, and hover/pressed states consistently.
        fileMenu_ = CreatePopupMenu();
        AppendMenuCommand(fileMenu_, ID_FILE_NEW);
        AppendMenuCommand(fileMenu_, ID_FILE_OPEN);
        AppendMenuCommand(fileMenu_, ID_FILE_SAVE);
        AppendMenuCommand(fileMenu_, ID_FILE_SAVE_AS);
        AppendMenuSeparator(fileMenu_);
        AppendMenuCommand(fileMenu_, ID_FILE_PAGE_SETUP);
        AppendMenuCommand(fileMenu_, ID_FILE_PRINT);
        AppendMenuSeparator(fileMenu_);
        AppendMenuCommand(fileMenu_, ID_FILE_EXIT);

        editMenu_ = CreatePopupMenu();
        AppendMenuCommand(editMenu_, ID_EDIT_UNDO);
        AppendMenuCommand(editMenu_, ID_EDIT_REDO);
        AppendMenuSeparator(editMenu_);
        AppendMenuCommand(editMenu_, ID_EDIT_CUT);
        AppendMenuCommand(editMenu_, ID_EDIT_COPY);
        AppendMenuCommand(editMenu_, ID_EDIT_PASTE);
        AppendMenuCommand(editMenu_, ID_EDIT_DELETE);
        AppendMenuSeparator(editMenu_);
        AppendMenuCommand(editMenu_, ID_EDIT_FIND);
        AppendMenuCommand(editMenu_, ID_EDIT_FIND_NEXT);
        AppendMenuCommand(editMenu_, ID_EDIT_FIND_PREVIOUS);
        AppendMenuCommand(editMenu_, ID_EDIT_REPLACE);
        AppendMenuCommand(editMenu_, ID_EDIT_GO_TO);
        AppendMenuSeparator(editMenu_);
        AppendMenuCommand(editMenu_, ID_EDIT_SELECT_ALL);
        AppendMenuCommand(editMenu_, ID_EDIT_TIME_DATE);

        formatMenu_ = CreatePopupMenu();
        AppendMenuCommand(formatMenu_, ID_FORMAT_WORD_WRAP);
        AppendMenuCommand(formatMenu_, ID_FORMAT_FONT);

        viewMenu_ = CreatePopupMenu();
        AppendMenuCommand(viewMenu_, ID_VIEW_LINE_NUMBERS);
        AppendMenuCommand(viewMenu_, ID_VIEW_STATUS_BAR);
        AppendMenuCommand(viewMenu_, ID_VIEW_DARK_MODE);

        helpMenu_ = CreatePopupMenu();
        AppendMenuCommand(helpMenu_, ID_HELP_ABOUT);

        menuEntries_ = {{
            {L"File", fileMenu_, {}},
            {L"Edit", editMenu_, {}},
            {L"Format", formatMenu_, {}},
            {L"View", viewMenu_, {}},
            {L"Help", helpMenu_, {}},
        }};
    }

    void OnCommand(WORD id, WORD notification, HWND source) {
        UNREFERENCED_PARAMETER(notification);
        if (source == editor_) {
            return;
        }

        switch (id) {
        case ID_FILE_NEW:
            NewDocument();
            break;
        case ID_FILE_OPEN:
            OpenDocumentFromDialog();
            break;
        case ID_FILE_SAVE:
            SaveDocument(false);
            break;
        case ID_FILE_SAVE_AS:
            SaveDocument(true);
            break;
        case ID_FILE_PAGE_SETUP:
            ShowPageSetupDialog();
            break;
        case ID_FILE_PRINT:
            ShowPrintDialog();
            break;
        case ID_FILE_EXIT:
            SendMessageW(hwnd_, WM_CLOSE, 0, 0);
            break;
        case ID_EDIT_UNDO:
            editorView_.Undo();
            break;
        case ID_EDIT_REDO:
            editorView_.Redo();
            break;
        case ID_EDIT_CUT:
            editorView_.Cut();
            break;
        case ID_EDIT_COPY:
            editorView_.Copy();
            break;
        case ID_EDIT_PASTE:
            editorView_.Paste();
            break;
        case ID_EDIT_DELETE:
            editorView_.Delete();
            break;
        case ID_EDIT_FIND:
            ShowFindDialog();
            break;
        case ID_EDIT_FIND_NEXT:
            FindNext(true);
            break;
        case ID_EDIT_FIND_PREVIOUS:
            FindNext(false);
            break;
        case ID_EDIT_REPLACE:
            ShowReplaceDialog();
            break;
        case ID_EDIT_GO_TO:
            ShowGoToDialog();
            break;
        case ID_EDIT_SELECT_ALL:
            editorView_.SelectAll();
            break;
        case ID_EDIT_TIME_DATE:
            editorView_.InsertAtCaret(UserDateTimeText());
            break;
        case ID_FORMAT_WORD_WRAP:
            ToggleWordWrap();
            break;
        case ID_FORMAT_FONT:
            ShowFontChooser();
            break;
        case ID_VIEW_STATUS_BAR:
            statusBarVisible_ = !statusBarVisible_;
            ShowWindow(status_, statusBarVisible_ ? SW_SHOW : SW_HIDE);
            Layout();
            break;
        case ID_VIEW_LINE_NUMBERS:
            editorView_.SetShowLineNumbers(!editorView_.ShowLineNumbers());
            InvalidateRect(menuStrip_, nullptr, FALSE);
            SetFocus(editor_);
            break;
        case ID_VIEW_DARK_MODE:
            darkMode_ = !darkMode_;
            darkModeForced_ = true;
            ApplyTheme();
            break;
        case ID_HELP_ABOUT:
            ShowAboutDialog(hwnd_, instance_, dpi_, darkMode_);
            break;
        default:
            break;
        }
    }

    void ShowRoadmapMessage(const wchar_t* command) const {
        std::wstring message = command;
        message += L" is on the classic Notepad parity roadmap and has not been implemented yet.";
        MessageBoxW(hwnd_, message.c_str(), L"NativePad", MB_ICONINFORMATION | MB_OK);
    }

    void SetFindBuffer(std::wstring_view text) {
        const size_t count = std::min(text.size(), findBuffer_.size() - 1);
        std::copy_n(text.data(), count, findBuffer_.data());
        findBuffer_[count] = L'\0';
    }

    void SetReplaceBuffer(std::wstring_view text) {
        const size_t count = std::min(text.size(), replaceBuffer_.size() - 1);
        std::copy_n(text.data(), count, replaceBuffer_.data());
        replaceBuffer_[count] = L'\0';
    }

    std::wstring SelectedTextForFind() const {
        if (!editorView_.HasSelection()) {
            return {};
        }

        const std::wstring selected = editorView_.SelectedText();
        if (selected.find_first_of(L"\r\n") != std::wstring::npos) {
            return {};
        }

        return selected;
    }

    void SeedFindBufferFromSelectionOrLast() {
        std::wstring seed = SelectedTextForFind();
        if (seed.empty()) {
            seed = lastFindText_;
        }
        if (!seed.empty()) {
            SetFindBuffer(seed);
        }
    }

    void ShowFindDialog() {
        if (findDialog_ != nullptr && !replaceDialogOpen_) {
            SetForegroundWindow(findDialog_);
            SetFocus(findDialog_);
            return;
        }

        if (findDialog_ != nullptr) {
            DestroyWindow(findDialog_);
            findDialog_ = nullptr;
            replaceDialogOpen_ = false;
        }

        SeedFindBufferFromSelectionOrLast();
        findDialog_ = ShowFindReplaceDialog(
            hwnd_,
            instance_,
            dpi_,
            darkMode_,
            false,
            findBuffer_.data(),
            replaceBuffer_.data(),
            lastFindMatchCase_,
            lastFindDown_);
        replaceDialogOpen_ = false;
    }

    void ShowReplaceDialog() {
        if (readOnlyPreview_) {
            return;
        }

        if (findDialog_ != nullptr && replaceDialogOpen_) {
            SetForegroundWindow(findDialog_);
            SetFocus(findDialog_);
            return;
        }

        if (findDialog_ != nullptr) {
            DestroyWindow(findDialog_);
            findDialog_ = nullptr;
            replaceDialogOpen_ = false;
        }

        SeedFindBufferFromSelectionOrLast();
        if (!lastReplaceText_.empty()) {
            SetReplaceBuffer(lastReplaceText_);
        }

        findDialog_ = ShowFindReplaceDialog(
            hwnd_,
            instance_,
            dpi_,
            darkMode_,
            true,
            findBuffer_.data(),
            replaceBuffer_.data(),
            lastFindMatchCase_,
            lastFindDown_);
        replaceDialogOpen_ = findDialog_ != nullptr;
    }

    void OnFindReplaceRequest(FindReplaceDialogRequest* request) {
        if (request == nullptr) {
            return;
        }

        if (request->action == FindReplaceDialogAction::Closed) {
            if (request->dialog == findDialog_) {
                findDialog_ = nullptr;
                replaceDialogOpen_ = false;
            }
            return;
        }

        lastFindText_ = request->findText;
        lastReplaceText_ = request->replaceText;
        lastFindMatchCase_ = request->matchCase;
        lastFindDown_ = request->down;
        SetFindBuffer(lastFindText_);
        SetReplaceBuffer(lastReplaceText_);
        if (lastFindText_.empty()) {
            return;
        }

        if (request->action == FindReplaceDialogAction::ReplaceAll) {
            ReplaceAll();
            return;
        }

        if (request->action == FindReplaceDialogAction::Replace) {
            if (SelectionMatchesFind()) {
                editorView_.InsertAtCaret(lastReplaceText_);
            }
            FindNext(lastFindDown_, false);
            return;
        }

        if (request->action == FindReplaceDialogAction::FindNext) {
            FindNext(lastFindDown_, false);
        }
    }

    void ShowFindNotFound() const {
        std::wstring message = L"Cannot find \"";
        message += lastFindText_;
        message += L"\"";
        MessageBoxW(hwnd_, message.c_str(), L"NativePad", MB_ICONINFORMATION | MB_OK);
    }

    void FindNext(bool down, bool focusEditor = true) {
        // Editable documents can search a materialized string. Mapped documents
        // search the mapped file directly and return backend-native positions.
        lastFindDown_ = down;
        if (lastFindText_.empty()) {
            ShowFindDialog();
            return;
        }

        if (mappedDocument_ != nullptr) {
            const size_t start = down ? editorView_.SelectionEnd() : editorView_.SelectionStart();
            std::optional<NativePad::MappedTextDocument::Match> match = mappedDocument_->Find(lastFindText_, start, down, lastFindMatchCase_);
            if (!match) {
                match = mappedDocument_->Find(lastFindText_, down ? 0 : mappedDocument_->Length(), down, lastFindMatchCase_);
            }

            if (!match) {
                ShowFindNotFound();
                return;
            }

            editorView_.SelectRange(match->position, match->length);
            if (focusEditor) {
                SetFocus(editor_);
            }
            return;
        }

        const std::wstring text = document_.Text();
        if (text.empty()) {
            ShowFindNotFound();
            return;
        }

        const size_t start = down ? editorView_.SelectionEnd() : editorView_.SelectionStart();
        std::optional<size_t> match = down
                                          ? FindForward(text, lastFindText_, start, lastFindMatchCase_)
                                          : FindBackward(text, lastFindText_, start, lastFindMatchCase_);
        if (!match) {
            match = down
                        ? FindForward(text, lastFindText_, 0, lastFindMatchCase_)
                        : FindBackward(text, lastFindText_, text.size(), lastFindMatchCase_);
        }

        if (!match) {
            ShowFindNotFound();
            return;
        }

        editorView_.SelectRange(*match, lastFindText_.size());
        if (focusEditor) {
            SetFocus(editor_);
        }
    }

    bool SelectionMatchesFind() const {
        if (mappedDocument_ != nullptr || lastFindText_.empty() || !editorView_.HasSelection()) {
            return false;
        }

        const size_t start = editorView_.SelectionStart();
        const size_t length = editorView_.SelectionEnd() - start;
        if (length != lastFindText_.size()) {
            return false;
        }

        const std::wstring text = document_.Text();
        return TextMatchesAt(text, lastFindText_, start, lastFindMatchCase_);
    }

    void ReplaceAll() {
        // Replace All is intentionally limited to editable documents because it
        // rebuilds the full string and then resets the piece table.
        if (readOnlyPreview_ || lastFindText_.empty()) {
            return;
        }

        std::wstring text = document_.Text();
        size_t replacements = 0;
        size_t searchStart = 0;
        while (auto match = FindForward(text, lastFindText_, searchStart, lastFindMatchCase_)) {
            text.replace(*match, lastFindText_.size(), lastReplaceText_);
            searchStart = *match + lastReplaceText_.size();
            ++replacements;
        }

        if (replacements == 0) {
            ShowFindNotFound();
            return;
        }

        SetEditorText(text);
        dirty_ = true;
        UpdateTitle();
        UpdateStatus();
        SetFocus(editor_);

        std::wstring message = L"Replaced ";
        message += std::to_wstring(replacements);
        message += replacements == 1 ? L" occurrence." : L" occurrences.";
        MessageBoxW(hwnd_, message.c_str(), L"NativePad", MB_ICONINFORMATION | MB_OK);
    }

    void ShowGoToDialog() {
        if (editorView_.WordWrap()) {
            MessageBoxW(hwnd_, L"Go To is unavailable while Word Wrap is enabled.", L"NativePad", MB_ICONINFORMATION | MB_OK);
            return;
        }

        const size_t lineCount = std::max<size_t>(1, editorView_.LineCount());
        auto line = ShowGoToLineDialog(hwnd_, instance_, dpi_, darkMode_, editorView_.Line() + 1, lineCount);
        if (!line) {
            return;
        }

        editorView_.GoToLine(*line);
        SetFocus(editor_);
        UpdateStatus();
    }

    void ToggleWordWrap() {
        editorView_.SetWordWrap(!editorView_.WordWrap());
        UpdateStatus();
        InvalidateRect(menuStrip_, nullptr, FALSE);
        SetFocus(editor_);
    }

    void ShowFontChooser() {
        auto font = ShowFontDialog(hwnd_, instance_, editorView_.Font(), dpi_, darkMode_);
        if (!font) {
            return;
        }

        editorView_.SetFont(*font);
        UpdateStatus();
        SetFocus(editor_);
    }

    void ShowPageSetupDialog() {
        PAGESETUPDLGW pageSetup{};
        pageSetup.lStructSize = sizeof(pageSetup);
        pageSetup.hwndOwner = hwnd_;
        pageSetup.Flags = PSD_INTHOUSANDTHSOFINCHES | PSD_MARGINS;
        pageSetup.rtMargin = pageMarginsThousandths_;

        if (PageSetupDlgW(&pageSetup)) {
            pageMarginsThousandths_ = pageSetup.rtMargin;
        }
    }

    void ShowPrintDialog() {
        if (readOnlyPreview_) {
            const wchar_t* message = IsMappedLargeFile()
                                         ? L"Printing is disabled for read-only mapped large files."
                                         : L"Printing is disabled for read-only large-file previews.";
            MessageBoxW(hwnd_, message, L"NativePad", MB_ICONINFORMATION | MB_OK);
            return;
        }

        PRINTDLGW print{};
        print.lStructSize = sizeof(print);
        print.hwndOwner = hwnd_;
        print.Flags = PD_RETURNDC | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
        print.nFromPage = 1;
        print.nToPage = 1;
        print.nMinPage = 1;
        print.nMaxPage = 0xFFFF;
        print.nCopies = 1;

        if (!PrintDlgW(&print)) {
            return;
        }

        PrintJob job{};
        job.owner = hwnd_;
        job.printerDc = print.hDC;
        job.documentName = currentPath_.empty() ? L"Untitled - NativePad" : currentPath_;
        job.text = EditorText();
        job.font = LogFontFromEditorFont(editorView_.Font(), static_cast<UINT>(std::max(1, GetDeviceCaps(print.hDC, LOGPIXELSY))));
        job.marginsThousandths = pageMarginsThousandths_;
        job.wordWrap = editorView_.WordWrap();

        if (print.hDevMode != nullptr) {
            GlobalFree(print.hDevMode);
        }
        if (print.hDevNames != nullptr) {
            GlobalFree(print.hDevNames);
        }

        statusText_ = L"Printing in background...";
        InvalidateRect(status_, nullptr, FALSE);
        StartPrintWorker(std::move(job));
    }

    void OnPrintComplete(PrintResult* rawResult) {
        std::unique_ptr<PrintResult> result(rawResult);
        if (!result) {
            return;
        }

        if (!result->success) {
            const std::wstring message = L"Could not print document:\n\n" + result->message;
            MessageBoxW(hwnd_, message.c_str(), L"NativePad", MB_ICONERROR | MB_OK);
        }

        UpdateStatus();
    }

    LRESULT OnNotify(NMHDR* header) {
        UNREFERENCED_PARAMETER(header);
        return 0;
    }

    LRESULT MenuStripProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_PAINT:
            PaintMenuStrip(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE:
            OnMenuMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSELEAVE:
            menuMouseTracking_ = false;
            hotMenu_ = -1;
            InvalidateRect(menuStrip_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            OnMenuClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    HFONT UiFont() const {
        return uiFont_ != nullptr ? uiFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }

    void RefreshUiFont() {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        BOOL loaded = SystemParametersInfoForDpi(
            SPI_GETNONCLIENTMETRICS,
            sizeof(metrics),
            &metrics,
            0,
            dpi_);
        if (!loaded) {
            loaded = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
        }

        HFONT nextFont = loaded ? CreateFontIndirectW(&metrics.lfMenuFont) : nullptr;
        if (nextFont == nullptr) {
            nextFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }

        if (uiFont_ != nullptr && uiFont_ != GetStockObject(DEFAULT_GUI_FONT)) {
            DeleteObject(uiFont_);
        }
        uiFont_ = nextFont;

        if (menuStrip_ != nullptr) {
            SendMessageW(menuStrip_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        }
        if (status_ != nullptr) {
            SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        }
    }

    void OnDpiChanged(UINT dpi, const RECT* suggestedRect) {
        dpi_ = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
        if (suggestedRect != nullptr) {
            SetWindowPos(
                hwnd_,
                nullptr,
                suggestedRect->left,
                suggestedRect->top,
                suggestedRect->right - suggestedRect->left,
                suggestedRect->bottom - suggestedRect->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }

        RefreshUiFont();
        editorView_.OnDpiChanged(dpi_);
        Layout();
        UpdateStatus();
        InvalidateRect(menuStrip_, nullptr, TRUE);
        InvalidateRect(status_, nullptr, TRUE);
    }

    int Scale(int value) const {
        return ScaleForDpi(value, dpi_);
    }

    int MenuStripHeight() const {
        return Scale(24);
    }

    int StatusBarHeight() const {
        return Scale(23);
    }

    int PopupMenuItemHeight() const {
        return Scale(24);
    }

    std::wstring MenuTextFor(UINT id) const {
        switch (id) {
        case ID_FILE_NEW:
            return L"&New\tCtrl+N";
        case ID_FILE_OPEN:
            return L"&Open...\tCtrl+O";
        case ID_FILE_SAVE:
            return L"&Save\tCtrl+S";
        case ID_FILE_SAVE_AS:
            return L"Save &As...\tCtrl+Shift+S";
        case ID_FILE_PAGE_SETUP:
            return L"Page Set&up...";
        case ID_FILE_PRINT:
            return L"&Print...\tCtrl+P";
        case ID_FILE_EXIT:
            return L"E&xit";
        case ID_EDIT_UNDO:
            return L"&Undo\tCtrl+Z";
        case ID_EDIT_REDO:
            return L"&Redo\tCtrl+Y";
        case ID_EDIT_CUT:
            return L"Cu&t\tCtrl+X";
        case ID_EDIT_COPY:
            return L"&Copy\tCtrl+C";
        case ID_EDIT_PASTE:
            return L"&Paste\tCtrl+V";
        case ID_EDIT_DELETE:
            return L"De&lete\tDel";
        case ID_EDIT_FIND:
            return L"&Find...\tCtrl+F";
        case ID_EDIT_FIND_NEXT:
            return L"Find &Next\tF3";
        case ID_EDIT_FIND_PREVIOUS:
            return L"Find Pre&vious\tShift+F3";
        case ID_EDIT_REPLACE:
            return L"&Replace...\tCtrl+H";
        case ID_EDIT_GO_TO:
            return L"&Go To...\tCtrl+G";
        case ID_EDIT_SELECT_ALL:
            return L"Select &All\tCtrl+A";
        case ID_EDIT_TIME_DATE:
            return L"Time/&Date\tF5";
        case ID_FORMAT_WORD_WRAP:
            return L"&Word Wrap";
        case ID_FORMAT_FONT:
            return L"&Font...";
        case ID_VIEW_STATUS_BAR:
            return L"&Status Bar";
        case ID_VIEW_LINE_NUMBERS:
            return L"&Line Numbers";
        case ID_VIEW_DARK_MODE:
            return L"&Dark Mode";
        case ID_HELP_ABOUT:
            return L"&About NativePad";
        default:
            return L"";
        }
    }

    static std::wstring StripMenuMnemonic(std::wstring text) {
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == L'&') {
                if (i + 1 < text.size() && text[i + 1] == L'&') {
                    text.erase(i, 1);
                } else {
                    text.erase(i, 1);
                    --i;
                }
            }
        }

        return text;
    }

    std::pair<std::wstring, std::wstring> SplitMenuText(UINT id) const {
        std::wstring text = StripMenuMnemonic(MenuTextFor(id));
        const size_t tab = text.find(L'\t');
        if (tab == std::wstring::npos) {
            return {text, L""};
        }

        return {text.substr(0, tab), text.substr(tab + 1)};
    }

    int MeasureTextWidth(HDC hdc, const std::wstring& text) const {
        SIZE size{};
        if (!text.empty()) {
            GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
        }
        return size.cx;
    }

    void LayoutMenuEntries(HDC hdc, int width) {
        int left = 0;
        const int horizontalPadding = Scale(12);
        const int height = MenuStripHeight();

        for (auto& entry : menuEntries_) {
            SIZE size{};
            GetTextExtentPoint32W(hdc, entry.text, static_cast<int>(wcslen(entry.text)), &size);
            const int itemWidth = size.cx + horizontalPadding * 2;
            entry.rect = {left, 0, std::min(width, left + itemWidth), height};
            left += itemWidth;
        }
    }

    void PaintMenuStrip(HWND hwnd) {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (hdc == nullptr) {
            return;
        }

        RECT client{};
        GetClientRect(hwnd, &client);
        const ThemeColors colors = ColorsForTheme(darkMode_);

        HBRUSH background = CreateSolidBrush(colors.menuBackground);
        FillRect(hdc, &client, background);
        DeleteObject(background);

        HFONT font = UiFont();

        HGDIOBJ oldFont = SelectObject(hdc, font);
        SetBkMode(hdc, TRANSPARENT);
        LayoutMenuEntries(hdc, client.right - client.left);

        for (size_t i = 0; i < menuEntries_.size(); ++i) {
            const MenuEntry& entry = menuEntries_[i];
            COLORREF itemBackground = colors.menuBackground;
            if (activeMenu_ == static_cast<int>(i)) {
                itemBackground = colors.menuPressed;
            } else if (hotMenu_ == static_cast<int>(i)) {
                itemBackground = colors.menuHot;
            }

            HBRUSH itemBrush = CreateSolidBrush(itemBackground);
            FillRect(hdc, &entry.rect, itemBrush);
            DeleteObject(itemBrush);

            RECT textRect = entry.rect;
            SetTextColor(hdc, colors.menuText);
            DrawTextW(hdc, entry.text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        HPEN borderPen = CreatePen(PS_SOLID, 1, colors.menuBorder);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        MoveToEx(hdc, client.left, client.bottom - 1, nullptr);
        LineTo(hdc, client.right, client.bottom - 1);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        SelectObject(hdc, oldFont);
        EndPaint(hwnd, &ps);
    }

    void DrawStatusBar(HDC hdc, RECT client) {
        const ThemeColors colors = ColorsForTheme(darkMode_);

        HBRUSH background = CreateSolidBrush(colors.statusBackground);
        FillRect(hdc, &client, background);
        DeleteObject(background);

        HPEN borderPen = CreatePen(PS_SOLID, 1, colors.separator);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        MoveToEx(hdc, client.left, client.top, nullptr);
        LineTo(hdc, client.right, client.top);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        HFONT font = UiFont();

        HGDIOBJ oldFont = SelectObject(hdc, font);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, colors.statusText);
        RECT textRect = client;
        textRect.left += Scale(6);
        DrawTextW(hdc, statusText_.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, oldFont);
    }

    int HitTestMenu(int x, int y) const {
        POINT point{x, y};
        for (size_t i = 0; i < menuEntries_.size(); ++i) {
            if (PtInRect(&menuEntries_[i].rect, point)) {
                return static_cast<int>(i);
            }
        }

        return -1;
    }

    void TrackMenuMouseLeave() {
        if (menuMouseTracking_) {
            return;
        }

        TRACKMOUSEEVENT event{};
        event.cbSize = sizeof(event);
        event.dwFlags = TME_LEAVE;
        event.hwndTrack = menuStrip_;
        if (TrackMouseEvent(&event)) {
            menuMouseTracking_ = true;
        }
    }

    void OnMenuMouseMove(int x, int y) {
        TrackMenuMouseLeave();
        const int hit = HitTestMenu(x, y);
        if (hit != hotMenu_) {
            hotMenu_ = hit;
            InvalidateRect(menuStrip_, nullptr, FALSE);
        }
    }

    void OnMenuClick(int x, int y) {
        const int hit = HitTestMenu(x, y);
        if (hit >= 0) {
            ShowPopupMenu(hit);
        }
    }

    bool OpenMenuFromKey(WPARAM key) {
        switch (key) {
        case 'F':
            ShowPopupMenu(0);
            return true;
        case 'E':
            ShowPopupMenu(1);
            return true;
        case 'O':
            ShowPopupMenu(2);
            return true;
        case 'V':
            ShowPopupMenu(3);
            return true;
        case 'H':
            ShowPopupMenu(4);
            return true;
        default:
            return false;
        }
    }

    bool RegisterCustomPopupMenuClass() const {
        auto registerClass = [](const WNDCLASSEXW& wc) {
            return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        };

        WNDCLASSEXW menuClass{};
        menuClass.cbSize = sizeof(menuClass);
        menuClass.lpfnWndProc = &CustomPopupMenuProc;
        menuClass.hInstance = instance_;
        menuClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        menuClass.hbrBackground = nullptr;
        menuClass.lpszClassName = kPopupMenuClass;
        if (!registerClass(menuClass)) {
            return false;
        }

        WNDCLASSEXW shadowClass{};
        shadowClass.cbSize = sizeof(shadowClass);
        shadowClass.lpfnWndProc = &PopupShadowProc;
        shadowClass.hInstance = instance_;
        shadowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        shadowClass.hbrBackground = nullptr;
        shadowClass.lpszClassName = kPopupShadowClass;
        return registerClass(shadowClass);
    }

    std::vector<PopupMenuItem> SnapshotPopupMenuItems(HMENU menu) const {
        std::vector<PopupMenuItem> items;
        const int count = GetMenuItemCount(menu);
        if (count <= 0) {
            return items;
        }

        items.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            MENUITEMINFOW info{};
            info.cbSize = sizeof(info);
            info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE;
            if (!GetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &info)) {
                continue;
            }

            PopupMenuItem item{};
            item.separator = (info.fType & MFT_SEPARATOR) != 0;
            item.id = item.separator ? 0 : info.wID;
            item.enabled = (info.fState & (MFS_DISABLED | MFS_GRAYED)) == 0;
            item.checked = (info.fState & MFS_CHECKED) != 0;
            if (!item.separator) {
                auto [label, shortcut] = SplitMenuText(item.id);
                item.label = std::move(label);
                item.shortcut = std::move(shortcut);
            }
            items.push_back(std::move(item));
        }

        return items;
    }

    SIZE LayoutCustomPopupMenuItems(std::vector<PopupMenuItem>& items) const {
        HDC hdc = GetDC(hwnd_);
        HFONT font = UiFont();
        HGDIOBJ oldFont = hdc != nullptr ? SelectObject(hdc, font) : nullptr;

        int width = Scale(190);
        for (const PopupMenuItem& item : items) {
            if (item.separator) {
                continue;
            }

            const int itemWidth = Scale(44) + (hdc != nullptr ? MeasureTextWidth(hdc, item.label) : 0) +
                                  Scale(36) + (hdc != nullptr ? MeasureTextWidth(hdc, item.shortcut) : 0);
            width = std::max(width, itemWidth);
        }

        if (hdc != nullptr) {
            SelectObject(hdc, oldFont);
            ReleaseDC(hwnd_, hdc);
        }

        const int border = 1;
        int y = border;
        for (PopupMenuItem& item : items) {
            const int height = item.separator ? Scale(9) : PopupMenuItemHeight();
            item.rect = {border, y, width - border, y + height};
            y += height;
        }

        return SIZE{width, y + border};
    }

    POINT ClampPopupMenuPosition(POINT position, SIZE size) const {
        HMONITOR monitor = MonitorFromPoint(position, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) {
            return position;
        }

        position.x = std::min(position.x, info.rcWork.right - size.cx);
        position.y = std::min(position.y, info.rcWork.bottom - size.cy);
        position.x = std::max(position.x, info.rcWork.left);
        position.y = std::max(position.y, info.rcWork.top);
        return position;
    }

    UINT ShowCustomPopupMenu(HMENU menu, int x, int y) {
        if (menu == nullptr || !RegisterCustomPopupMenuClass()) {
            return 0;
        }

        PopupMenuWindowState state{};
        state.owner = hwnd_;
        state.dpi = dpi_;
        state.font = UiFont();
        state.dark = darkMode_;
        state.items = SnapshotPopupMenuItems(menu);
        if (state.items.empty()) {
            return 0;
        }

        const SIZE size = LayoutCustomPopupMenuItems(state.items);
        const POINT position = ClampPopupMenuPosition(POINT{x, y}, size);
        state.shadow = CreatePopupShadowWindow(hwnd_, instance_, position, size, dpi_);

        HWND popup = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            kPopupMenuClass,
            nullptr,
            WS_POPUP | WS_CLIPSIBLINGS,
            position.x,
            position.y,
            size.cx,
            size.cy,
            hwnd_,
            nullptr,
            instance_,
            &state);
        if (popup == nullptr) {
            if (state.shadow != nullptr) {
                DestroyWindow(state.shadow);
            }
            return 0;
        }
        if (state.shadow != nullptr) {
            SetWindowPos(state.shadow, popup, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }

        // A small modal message loop gives the custom popup native menu behavior
        // without using USER32's popup window, whose border cannot be darkened
        // reliably across all menu positions.
        //
        // The popup is deliberately non-activating: native menus keep the owner
        // window active while they are open, so keyboard handling is intercepted
        // in this local loop instead of moving focus to the popup.
        ShowWindow(popup, SW_SHOWNOACTIVATE);
        UpdateWindow(popup);
        SetCapture(popup);

        MSG message{};
        while (IsWindow(popup)) {
            const BOOL result = GetMessageW(&message, nullptr, 0, 0);
            if (result <= 0) {
                if (result == 0) {
                    PostQuitMessage(static_cast<int>(message.wParam));
                }
                break;
            }

            if (RouteCustomPopupKey(popup, message)) {
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        return state.command;
    }

    void ShowPopupMenu(int index) {
        if (index < 0 || index >= static_cast<int>(menuEntries_.size())) {
            return;
        }

        MenuEntry& entry = menuEntries_[static_cast<size_t>(index)];
        if (entry.menu == nullptr) {
            return;
        }

        activeMenu_ = index;
        hotMenu_ = index;
        InvalidateRect(menuStrip_, nullptr, FALSE);
        UpdateWindow(menuStrip_);
        UpdateMenuState(entry.menu);

        RECT rect = entry.rect;
        MapWindowPoints(menuStrip_, HWND_DESKTOP, reinterpret_cast<POINT*>(&rect), 2);
        SetForegroundWindow(hwnd_);

        const UINT command = ShowCustomPopupMenu(entry.menu, rect.left, rect.bottom);

        activeMenu_ = -1;
        hotMenu_ = -1;
        InvalidateRect(menuStrip_, nullptr, FALSE);

        if (command != 0) {
            OnCommand(static_cast<WORD>(command), 0, nullptr);
        }
    }

    void ShowEditorContextMenu(POINT point) {
        if (point.x == -1 && point.y == -1) {
            RECT rect{};
            GetWindowRect(editor_, &rect);
            point.x = rect.left + Scale(24);
            point.y = rect.top + Scale(24);
        }

        SetFocus(editor_);
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr) {
            return;
        }

        AppendMenuCommand(menu, ID_EDIT_UNDO);
        AppendMenuCommand(menu, ID_EDIT_REDO);
        AppendMenuSeparator(menu);
        AppendMenuCommand(menu, ID_EDIT_CUT);
        AppendMenuCommand(menu, ID_EDIT_COPY);
        AppendMenuCommand(menu, ID_EDIT_PASTE);
        AppendMenuCommand(menu, ID_EDIT_DELETE);
        AppendMenuSeparator(menu);
        AppendMenuCommand(menu, ID_EDIT_FIND);
        AppendMenuCommand(menu, ID_EDIT_REPLACE);
        AppendMenuSeparator(menu);
        AppendMenuCommand(menu, ID_EDIT_SELECT_ALL);

        ApplyMenuBackgroundTo(menu);
        UpdateMenuState(menu);
        SetForegroundWindow(hwnd_);
        const UINT command = ShowCustomPopupMenu(menu, point.x, point.y);
        DestroyMenu(menu);

        if (command != 0) {
            OnCommand(static_cast<WORD>(command), 0, nullptr);
        }
    }

    void ApplyMenuBackgroundTo(HMENU menu) const {
        if (menu == nullptr || menuBackgroundBrush_ == nullptr) {
            return;
        }
        MENUINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = MIM_BACKGROUND;
        info.hbrBack = menuBackgroundBrush_;
        SetMenuInfo(menu, &info);
    }

    void ApplyMenuBackground() {
        if (menuBackgroundBrush_ != nullptr) {
            DeleteObject(menuBackgroundBrush_);
            menuBackgroundBrush_ = nullptr;
        }

        const ThemeColors colors = ColorsForTheme(darkMode_);
        menuBackgroundBrush_ = CreateSolidBrush(colors.menuBackground);

        ApplyMenuBackgroundTo(fileMenu_);
        ApplyMenuBackgroundTo(editMenu_);
        ApplyMenuBackgroundTo(formatMenu_);
        ApplyMenuBackgroundTo(viewMenu_);
        ApplyMenuBackgroundTo(helpMenu_);
    }

    LRESULT OnMeasureItem(MEASUREITEMSTRUCT* measure) const {
        if (measure == nullptr || measure->CtlType != ODT_MENU) {
            return FALSE;
        }

        HDC hdc = GetDC(hwnd_);
        HFONT font = UiFont();

        HGDIOBJ oldFont = SelectObject(hdc, font);
        if (measure->itemID == 0) {
            measure->itemWidth = static_cast<UINT>(Scale(190));
            measure->itemHeight = static_cast<UINT>(Scale(9));
            SelectObject(hdc, oldFont);
            ReleaseDC(hwnd_, hdc);
            return TRUE;
        }

        auto [label, shortcut] = SplitMenuText(measure->itemID);
        const int width = Scale(44) + MeasureTextWidth(hdc, label) + Scale(36) + MeasureTextWidth(hdc, shortcut);
        measure->itemWidth = static_cast<UINT>(std::max(width, Scale(190)));
        measure->itemHeight = static_cast<UINT>(PopupMenuItemHeight());
        SelectObject(hdc, oldFont);
        ReleaseDC(hwnd_, hdc);
        return TRUE;
    }

    LRESULT OnDrawItem(DRAWITEMSTRUCT* draw) {
        if (draw == nullptr) {
            return FALSE;
        }

        if (draw->CtlType == ODT_STATIC && draw->hwndItem == status_) {
            DrawStatusBar(draw->hDC, draw->rcItem);
            return TRUE;
        }

        if (draw->CtlType != ODT_MENU) {
            return FALSE;
        }

        const ThemeColors colors = ColorsForTheme(darkMode_);
        const bool selected = (draw->itemState & ODS_SELECTED) != 0;
        const bool disabled = (draw->itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
        const bool checked = (draw->itemState & ODS_CHECKED) != 0;
        const COLORREF backgroundColor = selected ? colors.menuHot : colors.menuBackground;
        HBRUSH background = CreateSolidBrush(backgroundColor);
        FillRect(draw->hDC, &draw->rcItem, background);
        DeleteObject(background);

        if (draw->itemID == 0) {
            const int midY = draw->rcItem.top + ((draw->rcItem.bottom - draw->rcItem.top) / 2);
            HPEN separatorPen = CreatePen(PS_SOLID, 1, colors.separator);
            HGDIOBJ oldPen = SelectObject(draw->hDC, separatorPen);
            MoveToEx(draw->hDC, draw->rcItem.left + Scale(4), midY, nullptr);
            LineTo(draw->hDC, draw->rcItem.right - Scale(4), midY);
            SelectObject(draw->hDC, oldPen);
            DeleteObject(separatorPen);
            return TRUE;
        }

        HFONT font = UiFont();

        HGDIOBJ oldFont = SelectObject(draw->hDC, font);
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, disabled ? colors.menuDisabledText : colors.menuText);

        if (checked) {
            HPEN checkPen = CreatePen(PS_SOLID, Scale(2), disabled ? colors.menuDisabledText : colors.menuText);
            HGDIOBJ oldPen = SelectObject(draw->hDC, checkPen);
            const int left = draw->rcItem.left + Scale(9);
            const int midY = draw->rcItem.top + ((draw->rcItem.bottom - draw->rcItem.top) / 2);
            MoveToEx(draw->hDC, left, midY, nullptr);
            LineTo(draw->hDC, left + Scale(4), midY + Scale(4));
            LineTo(draw->hDC, left + Scale(12), midY - Scale(5));
            SelectObject(draw->hDC, oldPen);
            DeleteObject(checkPen);
        }

        auto [label, shortcut] = SplitMenuText(draw->itemID);
        RECT labelRect = draw->rcItem;
        labelRect.left += Scale(30);
        labelRect.right -= Scale(14);

        RECT shortcutRect = labelRect;
        shortcutRect.left += Scale(40);

        DrawTextW(draw->hDC, label.c_str(), -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (!shortcut.empty()) {
            DrawTextW(draw->hDC, shortcut.c_str(), -1, &shortcutRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        SelectObject(draw->hDC, oldFont);
        return TRUE;
    }

    size_t DocumentLength() const noexcept {
        return mappedDocument_ != nullptr ? mappedDocument_->Length() : document_.Length();
    }

    bool IsMappedLargeFile() const noexcept {
        return mappedDocument_ != nullptr;
    }

    void UpdateMenuState(HMENU menu) const {
        // Menus are updated immediately before display so selection, read-only
        // mapped files, clipboard state, and word-wrap restrictions are current.
        if (menu == nullptr) {
            return;
        }

        const bool canEdit = !readOnlyPreview_;
        const bool hasText = DocumentLength() > 0;
        const bool canDelete = canEdit && (editorView_.HasSelection() || editorView_.CaretPosition() < DocumentLength());

        EnableMenuItem(menu, ID_EDIT_UNDO, MF_BYCOMMAND | (editorView_.CanUndo() ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_REDO, MF_BYCOMMAND | (editorView_.CanRedo() ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_CUT, MF_BYCOMMAND | (canEdit && editorView_.HasSelection() ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_COPY, MF_BYCOMMAND | (editorView_.HasSelection() ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_PASTE, MF_BYCOMMAND | (canEdit && IsClipboardFormatAvailable(CF_UNICODETEXT) ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_DELETE, MF_BYCOMMAND | (canDelete ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_FIND, MF_BYCOMMAND | (hasText ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_FIND_NEXT, MF_BYCOMMAND | (hasText ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_FIND_PREVIOUS, MF_BYCOMMAND | (hasText ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_REPLACE, MF_BYCOMMAND | (canEdit && hasText ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_GO_TO, MF_BYCOMMAND | (hasText && !editorView_.WordWrap() ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_SELECT_ALL, MF_BYCOMMAND | (hasText ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_TIME_DATE, MF_BYCOMMAND | (canEdit ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_SAVE, MF_BYCOMMAND | (canEdit ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_SAVE_AS, MF_BYCOMMAND | (canEdit ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_PRINT, MF_BYCOMMAND | (!readOnlyPreview_ ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(menu, ID_FORMAT_WORD_WRAP, MF_BYCOMMAND | (editorView_.WordWrap() ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menu, ID_VIEW_LINE_NUMBERS, MF_BYCOMMAND | (editorView_.ShowLineNumbers() ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menu, ID_VIEW_STATUS_BAR, MF_BYCOMMAND | (statusBarVisible_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menu, ID_VIEW_DARK_MODE, MF_BYCOMMAND | (darkMode_ ? MF_CHECKED : MF_UNCHECKED));
    }

    void Layout() const {
        // The editor consumes all space between the custom menu strip and status
        // bar. Both heights are DPI-scaled elsewhere.
        if (menuStrip_ == nullptr || editor_ == nullptr || status_ == nullptr) {
            return;
        }

        RECT client{};
        GetClientRect(hwnd_, &client);
        const int width = static_cast<int>(client.right - client.left);
        const int menuHeight = MenuStripHeight();
        const int statusHeight = statusBarVisible_ ? StatusBarHeight() : 0;
        const int editorHeight = std::max(0, static_cast<int>((client.bottom - client.top) - menuHeight - statusHeight));

        MoveWindow(menuStrip_, 0, 0, width, menuHeight, TRUE);
        MoveWindow(editor_, 0, menuHeight, width, editorHeight, TRUE);
        MoveWindow(status_, 0, menuHeight + editorHeight, width, statusHeight, TRUE);
    }

    void ApplyTheme() {
        // Apply theme changes to native windows first, then repaint owner-draw
        // surfaces that do not automatically pick up SetWindowTheme changes.
        const ThemeColors colors = ColorsForTheme(darkMode_);
        ApplyDarkFrame(hwnd_, darkMode_);
        ApplyDarkControlTheme(editor_, darkMode_);
        ApplyDarkControlTheme(status_, darkMode_);

        editorView_.SetTheme({
            colors.editorBackground,
            colors.editorText,
            darkMode_ ? RGB(60, 92, 140) : RGB(173, 214, 255),
            colors.editorText,
            colors.editorLineNumberBackground,
            colors.editorLineNumberText,
            colors.editorLineNumberSeparator,
        });

        ApplyMenuBackground();
        InvalidateRect(menuStrip_, nullptr, TRUE);
        InvalidateRect(editor_, nullptr, TRUE);
        InvalidateRect(status_, nullptr, TRUE);
    }

    void UpdateTitle() const {
        std::wstring title;
        if (dirty_) {
            title += L"*";
        }

        title += currentPath_.empty() ? kUntitled : BaseName(currentPath_);
        if (IsMappedLargeFile()) {
            title += L" [Read-only mapped]";
        } else if (readOnlyPreview_) {
            title += L" [Read-only preview]";
        }
        title += L" - NativePad";
        SetWindowTextW(hwnd_, title.c_str());
    }

    std::wstring EditorText() const {
        if (mappedDocument_ != nullptr) {
            return {};
        }
        return document_.Text();
    }

    void SetEditorText(const std::wstring& text, bool readOnlyPreview = false) {
        mappedDocument_.reset();
        document_.Reset(text);
        readOnlyPreview_ = readOnlyPreview;
        editorView_.SetDocument(&document_);
        editorView_.SetReadOnly(readOnlyPreview_);
    }

    void SetMappedEditorDocument(std::unique_ptr<NativePad::MappedTextDocument> document) {
        // Large files are view/search-only for now. Reset the editable buffer so
        // accidental save/replace paths cannot operate on stale text.
        document_.Reset();
        mappedDocument_ = std::move(document);
        readOnlyPreview_ = true;
        previewDecodedByteCount_ = 0;
        editorView_.SetMappedDocument(mappedDocument_.get());
        editorView_.SetReadOnly(true);
    }

    void UpdateStatus() {
        if (editor_ == nullptr || status_ == nullptr) {
            return;
        }

        const auto line = static_cast<unsigned long long>(editorView_.Line() + 1);
        const auto column = static_cast<unsigned long long>(editorView_.Column() + 1);
        const auto lineCount = static_cast<unsigned long long>(editorView_.LineCount());
        const auto documentLength = static_cast<unsigned long long>(DocumentLength());

        wchar_t status[512]{};
        if (IsMappedLargeFile()) {
            StringCchPrintfW(
                status,
                std::size(status),
                L"Ln %llu, Col %llu    Lines %llu    %s    READ-ONLY MAPPED    %llu MB    %llu chars",
                line,
                column,
                lineCount,
                encodingLabel_.c_str(),
                static_cast<unsigned long long>(fileByteCount_ / (1024u * 1024u)),
                documentLength);
        } else if (readOnlyPreview_) {
            StringCchPrintfW(
                status,
                std::size(status),
                L"Ln %llu, Col %llu    Lines %llu    %s    READ-ONLY PREVIEW    %llu/%llu MB    %llu chars",
                line,
                column,
                lineCount,
                encodingLabel_.c_str(),
                static_cast<unsigned long long>(previewDecodedByteCount_ / (1024u * 1024u)),
                static_cast<unsigned long long>(fileByteCount_ / (1024u * 1024u)),
                documentLength);
        } else {
            StringCchPrintfW(
                status,
                std::size(status),
                L"Ln %llu, Col %llu    Lines %llu    %s    %llu chars",
                line,
                column,
                lineCount,
                encodingLabel_.c_str(),
                documentLength);
        }

        statusText_ = status;
        InvalidateRect(status_, nullptr, FALSE);
    }

    bool ConfirmSaveIfDirty() {
        if (!dirty_) {
            return true;
        }

        const int choice = MessageBoxW(
            hwnd_,
            L"Save changes to this document?",
            L"NativePad",
            MB_ICONQUESTION | MB_YESNOCANCEL | MB_DEFBUTTON1);

        if (choice == IDCANCEL) {
            return false;
        }

        if (choice == IDYES) {
            return SaveDocument(false);
        }

        return true;
    }

    void NewDocument() {
        if (!ConfirmSaveIfDirty()) {
            return;
        }

        currentPath_.clear();
        documentEncoding_ = NativePad::TextEncoding::Utf8;
        documentLineEnding_ = NativePad::LineEnding::CrLf;
        encodingLabel_ = NativePad::EncodingLabel(documentEncoding_);
        fileByteCount_ = 0;
        previewDecodedByteCount_ = 0;
        SetEditorText(L"");
        dirty_ = false;
        UpdateTitle();
        UpdateStatus();
        SetFocus(editor_);
    }

    void OpenDocumentFromDialog() {
        if (!ConfirmSaveIfDirty()) {
            return;
        }

        auto path = ShowOpenDialog(hwnd_);
        if (!path) {
            return;
        }

        OpenDocument(*path);
    }

    void OpenDocument(const std::wstring& path) {
        // File size decides the backend: normal files become editable UTF-16 text,
        // while oversized files stay mapped and read-only.
        std::wstring error;
        if (auto fileByteCount = FileByteCountForPath(path); fileByteCount && *fileByteCount > kReadChunkLimit) {
            auto mapped = std::make_unique<NativePad::MappedTextDocument>();
            if (!mapped->Open(path, error)) {
                std::wstring message = L"Could not open large file:\n\n" + error;
                MessageBoxW(hwnd_, message.c_str(), L"NativePad", MB_ICONERROR | MB_OK);
                return;
            }

            currentPath_ = path;
            encodingLabel_ = mapped->EncodingLabel();
            documentEncoding_ = NativePad::TextEncoding::Utf8;
            documentLineEnding_ = NativePad::LineEnding::CrLf;
            fileByteCount_ = mapped->FileByteCount();
            SetMappedEditorDocument(std::move(mapped));
            dirty_ = false;
            UpdateTitle();
            UpdateStatus();
            SetFocus(editor_);
            return;
        }

        auto file = ReadTextFile(path, error);
        if (!file) {
            std::wstring message = L"Could not open file:\n\n" + error;
            MessageBoxW(hwnd_, message.c_str(), L"NativePad", MB_ICONERROR | MB_OK);
            return;
        }

        currentPath_ = path;
        documentEncoding_ = file->encoding;
        documentLineEnding_ = file->lineEnding;
        encodingLabel_ = NativePad::EncodingLabel(documentEncoding_);
        fileByteCount_ = file->fileByteCount;
        previewDecodedByteCount_ = file->decodedByteCount;
        SetEditorText(file->text, file->readOnlyPreview);
        dirty_ = false;
        UpdateTitle();
        UpdateStatus();
        SetFocus(editor_);
    }

    bool SaveDocument(bool saveAs) {
        if (readOnlyPreview_) {
            const wchar_t* message = IsMappedLargeFile()
                                         ? L"This file is opened through the read-only large-file mapper. Saving is disabled until editable large-file storage is implemented."
                                         : L"This is a read-only large-file preview. NativePad is showing only the initial chunk, so saving is disabled.";
            MessageBoxW(
                hwnd_,
                message,
                L"NativePad",
                MB_ICONINFORMATION | MB_OK);
            return false;
        }

        std::wstring path = currentPath_;
        NativePad::TextEncoding targetEncoding = documentEncoding_;
        if (saveAs || path.empty()) {
            auto selected = ShowSaveDialog(hwnd_, path, targetEncoding);
            if (!selected) {
                return false;
            }
            path = selected->path;
            targetEncoding = selected->encoding;
        }

        const std::wstring text = EditorText();
        std::wstring error;
        if (!WriteTextFile(path, text, targetEncoding, documentLineEnding_, error)) {
            std::wstring message = L"Could not save file:\n\n" + error;
            MessageBoxW(hwnd_, message.c_str(), L"NativePad", MB_ICONERROR | MB_OK);
            return false;
        }

        currentPath_ = path;
        documentEncoding_ = targetEncoding;
        encodingLabel_ = NativePad::EncodingLabel(documentEncoding_);
        dirty_ = false;
        UpdateTitle();
        UpdateStatus();
        return true;
    }

    void OnDropFiles(HDROP drop) {
        if (!ConfirmSaveIfDirty()) {
            DragFinish(drop);
            return;
        }

        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        if (count > 0) {
            std::array<wchar_t, 32768> path{};
            if (DragQueryFileW(drop, 0, path.data(), static_cast<UINT>(path.size())) > 0) {
                OpenDocument(path.data());
            }
        }

        DragFinish(drop);
    }

    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND menuStrip_{};
    HWND editor_{};
    HWND status_{};
    HMENU fileMenu_{};
    HMENU editMenu_{};
    HMENU formatMenu_{};
    HMENU viewMenu_{};
    HMENU helpMenu_{};
    HBRUSH menuBackgroundBrush_{};
    HFONT uiFont_{};
    std::array<MenuEntry, 5> menuEntries_{{
        {L"File", nullptr, {}},
        {L"Edit", nullptr, {}},
        {L"Format", nullptr, {}},
        {L"View", nullptr, {}},
        {L"Help", nullptr, {}},
    }};
    NativePad::EditorFontSpec preferredFont_{};
    NativePad::DocumentBuffer document_;
    std::unique_ptr<NativePad::MappedTextDocument> mappedDocument_;
    NativePad::EditorView editorView_;
    std::wstring currentPath_;
    std::wstring encodingLabel_{L"UTF-8"};
    NativePad::TextEncoding documentEncoding_{NativePad::TextEncoding::Utf8};
    NativePad::LineEnding documentLineEnding_{NativePad::LineEnding::CrLf};
    std::wstring statusText_;
    std::array<wchar_t, 512> findBuffer_{};
    std::array<wchar_t, 512> replaceBuffer_{};
    std::wstring lastFindText_;
    std::wstring lastReplaceText_;
    HWND findDialog_{};
    RECT pageMarginsThousandths_{1000, 1000, 1000, 1000};
    RECT savedWindowRect_{};
    uint64_t fileByteCount_{0};
    size_t previewDecodedByteCount_{0};
    UINT dpi_{USER_DEFAULT_SCREEN_DPI};
    int hotMenu_{-1};
    int activeMenu_{-1};
    bool dirty_{false};
    bool readOnlyPreview_{false};
    bool replaceDialogOpen_{false};
    bool lastFindMatchCase_{false};
    bool lastFindDown_{true};
    bool preferredWordWrap_{false};
    bool preferredLineNumbers_{false};
    bool statusBarVisible_{true};
    bool darkMode_{false};
    bool darkModeForced_{false};
    bool hasSavedWindowRect_{false};
    bool savedWindowMaximized_{false};
    bool menuMouseTracking_{false};
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    EnableProcessDpiAwareness();

    // The Vista+ common file dialogs use COM, and the Save As encoding picker
    // is added through IFileDialogCustomize before the dialog is shown.
    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninitializeCom = SUCCEEDED(comInit);

    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&commonControls);

    AppWindow app(instance);
    if (!app.Create(showCommand)) {
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 1;
    }

    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments != nullptr) {
        if (argumentCount > 1) {
            app.OpenInitialFile(arguments[1]);
        }
        LocalFree(arguments);
    }

    ACCEL accelerators[] = {
        {FVIRTKEY | FCONTROL, 'N', ID_FILE_NEW},
        {FVIRTKEY | FCONTROL, 'O', ID_FILE_OPEN},
        {FVIRTKEY | FCONTROL, 'S', ID_FILE_SAVE},
        {FVIRTKEY | FCONTROL | FSHIFT, 'S', ID_FILE_SAVE_AS},
        {FVIRTKEY | FCONTROL, 'P', ID_FILE_PRINT},
        {FVIRTKEY | FCONTROL, 'Z', ID_EDIT_UNDO},
        {FVIRTKEY | FCONTROL, 'Y', ID_EDIT_REDO},
        {FVIRTKEY | FCONTROL, 'X', ID_EDIT_CUT},
        {FVIRTKEY | FCONTROL, 'C', ID_EDIT_COPY},
        {FVIRTKEY | FCONTROL, 'V', ID_EDIT_PASTE},
        {FVIRTKEY, VK_DELETE, ID_EDIT_DELETE},
        {FVIRTKEY | FCONTROL, 'F', ID_EDIT_FIND},
        {FVIRTKEY, VK_F3, ID_EDIT_FIND_NEXT},
        {FVIRTKEY | FSHIFT, VK_F3, ID_EDIT_FIND_PREVIOUS},
        {FVIRTKEY | FCONTROL, 'H', ID_EDIT_REPLACE},
        {FVIRTKEY | FCONTROL, 'G', ID_EDIT_GO_TO},
        {FVIRTKEY | FCONTROL, 'A', ID_EDIT_SELECT_ALL},
        {FVIRTKEY, VK_F5, ID_EDIT_TIME_DATE},
    };

    HACCEL acceleratorTable = CreateAcceleratorTableW(accelerators, static_cast<int>(std::size(accelerators)));

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (app.TranslateModelessDialog(&message)) {
            continue;
        }
        if (acceleratorTable == nullptr || TranslateAcceleratorW(app.Hwnd(), acceleratorTable, &message) == 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (acceleratorTable != nullptr) {
        DestroyAcceleratorTable(acceleratorTable);
    }

    if (shouldUninitializeCom) {
        CoUninitialize();
    }

    return static_cast<int>(message.wParam);
}
