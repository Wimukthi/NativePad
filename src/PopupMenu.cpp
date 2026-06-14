#include "PopupMenu.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "UiSupport.h"

namespace NativePad {

namespace {
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
} // namespace
LRESULT CALLBACK PopupShadowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }
    if (message == WM_MOUSEACTIVATE) {
        return MA_NOACTIVATE;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
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
    case WM_SETCURSOR:
        // The editor window normally owns an I-beam cursor. Popup menus are
        // no-activate windows, so force the arrow while the cursor is over them.
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
    case WM_PAINT:
        PaintCustomPopupMenu(state);
        return 0;
    case WM_MOUSEMOVE: {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
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
} // namespace NativePad
