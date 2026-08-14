#include "TabStrip.h"

#include <commctrl.h>
#include <d2d1.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace NativePad {

namespace {

constexpr wchar_t kTabStripClass[] = L"NativePadTabStrip";

D2D1_COLOR_F ToD2DColor(COLORREF color) {
    return D2D1::ColorF(
        static_cast<float>(GetRValue(color)) / 255.0f,
        static_cast<float>(GetGValue(color)) / 255.0f,
        static_cast<float>(GetBValue(color)) / 255.0f);
}

void FillRectColor(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

} // namespace

struct TabStrip::Impl {
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> roundStroke;
};

TabStrip::TabStrip() : impl_(new Impl()) {}

TabStrip::~TabStrip() {
    delete impl_;
}

bool TabStrip::Register(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TabStrip::StaticWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kTabStripClass;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool TabStrip::Create(HWND parent, HINSTANCE instance) {
    parent_ = parent;
    instance_ = instance;
    hwnd_ = CreateWindowExW(
        0,
        kTabStripClass,
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0,
        0,
        0,
        0,
        parent,
        nullptr,
        instance,
        this);
    if (hwnd_ != nullptr) {
        tooltip_ = CreateWindowExW(
            WS_EX_TOPMOST,
            TOOLTIPS_CLASSW,
            nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            hwnd_,
            nullptr,
            instance_,
            nullptr);
        if (tooltip_ != nullptr) {
            TOOLINFOW tool{};
            tool.cbSize = sizeof(tool);
            tool.uFlags = TTF_SUBCLASS;
            tool.hwnd = hwnd_;
            tool.uId = 1;
            tool.lpszText = LPSTR_TEXTCALLBACKW;
            SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }
    }
    return hwnd_ != nullptr;
}

HWND TabStrip::Hwnd() const noexcept {
    return hwnd_;
}

int TabStrip::PreferredHeight(UINT dpi) const noexcept {
    return MulDiv(30, static_cast<int>(dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi), USER_DEFAULT_SCREEN_DPI);
}

void TabStrip::SetItems(std::vector<TabStripItem> items, TabId activeId) {
    items_ = std::move(items);
    activeId_ = activeId;
    firstVisible_ = std::min(firstVisible_, items_.empty() ? 0u : items_.size() - 1);
    LayoutItems();
    EnsureActiveVisible();
    LayoutItems();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void TabStrip::SetActive(TabId activeId) {
    activeId_ = activeId;
    LayoutItems();
    EnsureActiveVisible();
    LayoutItems();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void TabStrip::SetTheme(TabStripTheme theme) {
    theme_ = theme;
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void TabStrip::SetFont(HFONT font) {
    font_ = font;
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void TabStrip::OnDpiChanged(UINT dpi) {
    dpi_ = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
    LayoutItems();
    EnsureActiveVisible();
    LayoutItems();
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

LRESULT CALLBACK TabStrip::StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    TabStrip* strip = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        strip = static_cast<TabStrip*>(create->lpCreateParams);
        strip->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(strip));
    } else {
        strip = reinterpret_cast<TabStrip*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return strip != nullptr ? strip->WndProc(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT TabStrip::WndProc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT:
        Paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        LayoutItems();
        EnsureActiveVisible();
        LayoutItems();
        return 0;
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header != nullptr && header->hwndFrom == tooltip_ && header->code == TTN_GETDISPINFOW) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            const HitResult hit = HitTest(point);
            if ((hit.part == HitPart::Tab || hit.part == HitPart::Close) && hit.index < items_.size()) {
                tooltipText_ = items_[hit.index].fullPath.empty() ? items_[hit.index].title : items_[hit.index].fullPath;
            } else if (hit.part == HitPart::NewTab) {
                tooltipText_ = L"New tab (Ctrl+N)";
            } else if (hit.part == HitPart::ScrollLeft) {
                tooltipText_ = L"Previous tabs";
            } else if (hit.part == HitPart::ScrollRight) {
                tooltipText_ = L"More tabs";
            } else {
                tooltipText_.clear();
            }
            auto* info = reinterpret_cast<NMTTDISPINFOW*>(lParam);
            info->lpszText = tooltipText_.data();
            return 0;
        }
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
    case WM_MOUSEMOVE: {
        BeginMouseTracking();
        const HitResult next = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        if (next.part != hot_.part || next.index != hot_.index) {
            hot_ = next;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        hot_ = {};
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        const HitResult hit = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        if (hit.part == HitPart::Tab && hit.index < items_.size()) {
            SendMessageW(parent_, WM_TABSTRIP_ACTIVATE, static_cast<WPARAM>(items_[hit.index].id), 0);
        } else if (hit.part == HitPart::Close && hit.index < items_.size()) {
            SendMessageW(parent_, WM_TABSTRIP_CLOSE, static_cast<WPARAM>(items_[hit.index].id), 0);
        } else if (hit.part == HitPart::NewTab) {
            SendMessageW(parent_, WM_TABSTRIP_NEW, 0, 0);
        } else if (hit.part == HitPart::ScrollLeft && firstVisible_ > 0) {
            --firstVisible_;
            LayoutItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (hit.part == HitPart::ScrollRight && firstVisible_ + visibleCount_ < items_.size()) {
            ++firstVisible_;
            LayoutItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MBUTTONUP: {
        const HitResult hit = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        if ((hit.part == HitPart::Tab || hit.part == HitPart::Close) && hit.index < items_.size()) {
            SendMessageW(parent_, WM_TABSTRIP_CLOSE, static_cast<WPARAM>(items_[hit.index].id), 0);
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (GET_WHEEL_DELTA_WPARAM(wParam) > 0 && firstVisible_ > 0) {
            --firstVisible_;
        } else if (GET_WHEEL_DELTA_WPARAM(wParam) < 0 && firstVisible_ + visibleCount_ < items_.size()) {
            ++firstVisible_;
        }
        LayoutItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    default:
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }
}

void TabStrip::Paint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd_, &paint);
    RECT client{};
    GetClientRect(hwnd_, &client);

    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, std::max(1L, client.right), std::max(1L, client.bottom));
    HGDIOBJ oldBitmap = SelectObject(buffer, bitmap);
    FillRectColor(buffer, client, theme_.background);
    SetBkMode(buffer, TRANSPARENT);
    SetTextColor(buffer, theme_.text);
    HGDIOBJ oldFont = SelectObject(buffer, font_ != nullptr ? font_ : GetStockObject(DEFAULT_GUI_FONT));
    PaintTabSurfaces(buffer, client);

    for (std::size_t i = 0; i < items_.size(); ++i) {
        const RECT rect = i < tabRects_.size() ? tabRects_[i] : RECT{};
        if (IsRectEmpty(&rect)) {
            continue;
        }

        RECT textRect = rect;
        textRect.left += Scale(items_[i].dirty ? 22 : 12);
        textRect.right -= Scale(29);
        DrawTextW(buffer, items_[i].title.c_str(), -1, &textRect,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        if (items_[i].dirty) {
            const int radius = Scale(3);
            const int centerX = rect.left + Scale(12);
            const int centerY = (rect.top + rect.bottom) / 2;
            HBRUSH marker = CreateSolidBrush(theme_.dirtyMarker);
            HGDIOBJ oldMarker = SelectObject(buffer, marker);
            HPEN markerPen = CreatePen(PS_SOLID, 1, theme_.dirtyMarker);
            HGDIOBJ oldMarkerPen = SelectObject(buffer, markerPen);
            Ellipse(buffer, centerX - radius, centerY - radius, centerX + radius + 1, centerY + radius + 1);
            SelectObject(buffer, oldMarkerPen);
            SelectObject(buffer, oldMarker);
            DeleteObject(markerPen);
            DeleteObject(marker);
        }

    }

    auto paintButton = [&](const RECT& rect, HitPart part, const wchar_t* glyph) {
        if (IsRectEmpty(&rect)) {
            return;
        }
        if (hot_.part == part) {
            FillRectColor(buffer, rect, theme_.hotTab);
        }
        SetTextColor(buffer, hot_.part == part ? theme_.text : theme_.mutedText);
        RECT label = rect;
        DrawTextW(buffer, glyph, -1, &label, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    };

    paintButton(scrollLeftRect_, HitPart::ScrollLeft, L"\x2039");
    paintButton(scrollRightRect_, HitPart::ScrollRight, L"\x203A");

    PaintAntialiasedControls(buffer, client);

    HPEN bottomPen = CreatePen(PS_SOLID, 1, theme_.border);
    HGDIOBJ oldPen = SelectObject(buffer, bottomPen);
    MoveToEx(buffer, client.left, client.bottom - 1, nullptr);
    LineTo(buffer, client.right, client.bottom - 1);
    SelectObject(buffer, oldPen);
    DeleteObject(bottomPen);

    SelectObject(buffer, oldFont);
    BitBlt(dc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(buffer);
    EndPaint(hwnd_, &paint);
}

bool TabStrip::EnsureControlRenderTarget() {
    if (impl_->factory == nullptr &&
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, impl_->factory.GetAddressOf()))) {
        return false;
    }

    if (impl_->roundStroke == nullptr) {
        const D2D1_STROKE_STYLE_PROPERTIES properties = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND,
            D2D1_CAP_STYLE_ROUND,
            D2D1_CAP_STYLE_ROUND,
            D2D1_LINE_JOIN_ROUND,
            10.0f,
            D2D1_DASH_STYLE_SOLID,
            0.0f);
        if (FAILED(impl_->factory->CreateStrokeStyle(
                properties,
                nullptr,
                0,
                impl_->roundStroke.GetAddressOf()))) {
            return false;
        }
    }

    if (impl_->target == nullptr) {
        const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f,
            96.0f);
        if (FAILED(impl_->factory->CreateDCRenderTarget(&properties, impl_->target.GetAddressOf()))) {
            return false;
        }
    }

    return true;
}

void TabStrip::PaintTabSurfaces(HDC dc, const RECT& client) {
    if (!EnsureControlRenderTarget() || FAILED(impl_->target->BindDC(dc, &client))) {
        return;
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(impl_->target->CreateSolidColorBrush(ToD2DColor(theme_.tab), brush.GetAddressOf()))) {
        return;
    }

    const float scale = static_cast<float>(dpi_) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    const float radius = 6.0f * scale;
    const float inset = std::max(1.0f, scale);

    impl_->target->BeginDraw();
    impl_->target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (i >= tabRects_.size() || IsRectEmpty(&tabRects_[i])) {
            continue;
        }

        const RECT& rect = tabRects_[i];
        COLORREF fill = theme_.tab;
        if (items_[i].id == activeId_) {
            fill = theme_.activeTab;
        } else if (hot_.part != HitPart::None && hot_.index == i &&
                   (hot_.part == HitPart::Tab || hot_.part == HitPart::Close)) {
            fill = theme_.hotTab;
        }

        // Extending below the clipped client area leaves a traditional flat
        // lower edge while keeping the exposed top corners smoothly rounded.
        const D2D1_ROUNDED_RECT surface = D2D1::RoundedRect(
            D2D1::RectF(
                static_cast<float>(rect.left) + inset,
                static_cast<float>(rect.top) + inset,
                static_cast<float>(rect.right) - inset,
                static_cast<float>(rect.bottom) + radius),
            radius,
            radius);
        brush->SetColor(ToD2DColor(fill));
        impl_->target->FillRoundedRectangle(surface, brush.Get());
        brush->SetColor(ToD2DColor(theme_.border));
        impl_->target->DrawRoundedRectangle(surface, brush.Get(), std::max(1.0f, scale));

        if (items_[i].id == activeId_) {
            brush->SetColor(ToD2DColor(theme_.accent));
            const float accentY = static_cast<float>(rect.top) + inset + (1.0f * scale);
            impl_->target->DrawLine(
                D2D1::Point2F(static_cast<float>(rect.left) + (10.0f * scale), accentY),
                D2D1::Point2F(static_cast<float>(rect.right) - (10.0f * scale), accentY),
                brush.Get(),
                2.0f * scale,
                impl_->roundStroke.Get());
        }
    }

    const HRESULT result = impl_->target->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        impl_->target.Reset();
    }
}

void TabStrip::PaintAntialiasedControls(HDC dc, const RECT& client) {
    if (!EnsureControlRenderTarget() || FAILED(impl_->target->BindDC(dc, &client))) {
        return;
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(impl_->target->CreateSolidColorBrush(ToD2DColor(theme_.mutedText), brush.GetAddressOf()))) {
        return;
    }

    const float scale = static_cast<float>(dpi_) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    const float strokeWidth = 1.25f * scale;
    const float closeArm = 3.75f * scale;
    const float plusArm = 4.5f * scale;

    auto hoverSurface = [&](const RECT& bounds) {
        const float centerX = static_cast<float>(bounds.left + bounds.right) / 2.0f;
        const float centerY = static_cast<float>(bounds.top + bounds.bottom) / 2.0f;
        const float halfSize = 10.0f * scale;
        return D2D1::RoundedRect(
            D2D1::RectF(centerX - halfSize, centerY - halfSize, centerX + halfSize, centerY + halfSize),
            5.0f * scale,
            5.0f * scale);
    };

    impl_->target->BeginDraw();
    impl_->target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (i >= closeRects_.size() || IsRectEmpty(&closeRects_[i])) {
            continue;
        }
        const bool closeHot = hot_.part == HitPart::Close && hot_.index == i;
        const bool showClose = items_[i].id == activeId_ ||
                               (hot_.index == i && (hot_.part == HitPart::Tab || hot_.part == HitPart::Close));
        if (!showClose) {
            continue;
        }

        const RECT& close = closeRects_[i];
        const float centerX = static_cast<float>(close.left + close.right) / 2.0f;
        const float centerY = static_cast<float>(close.top + close.bottom) / 2.0f;
        if (closeHot) {
            brush->SetColor(ToD2DColor(theme_.hotTab));
            impl_->target->FillRoundedRectangle(hoverSurface(close), brush.Get());
        }

        brush->SetColor(ToD2DColor(closeHot ? theme_.text : theme_.mutedText));
        impl_->target->DrawLine(
            D2D1::Point2F(centerX - closeArm, centerY - closeArm),
            D2D1::Point2F(centerX + closeArm, centerY + closeArm),
            brush.Get(),
            strokeWidth,
            impl_->roundStroke.Get());
        impl_->target->DrawLine(
            D2D1::Point2F(centerX + closeArm, centerY - closeArm),
            D2D1::Point2F(centerX - closeArm, centerY + closeArm),
            brush.Get(),
            strokeWidth,
            impl_->roundStroke.Get());
    }

    if (!IsRectEmpty(&newRect_)) {
        const bool newHot = hot_.part == HitPart::NewTab;
        const float centerX = static_cast<float>(newRect_.left + newRect_.right) / 2.0f;
        const float centerY = static_cast<float>(newRect_.top + newRect_.bottom) / 2.0f;
        if (newHot) {
            brush->SetColor(ToD2DColor(theme_.hotTab));
            impl_->target->FillRoundedRectangle(hoverSurface(newRect_), brush.Get());
        }

        brush->SetColor(ToD2DColor(newHot ? theme_.accent : theme_.text));
        impl_->target->DrawLine(
            D2D1::Point2F(centerX - plusArm, centerY),
            D2D1::Point2F(centerX + plusArm, centerY),
            brush.Get(),
            strokeWidth,
            impl_->roundStroke.Get());
        impl_->target->DrawLine(
            D2D1::Point2F(centerX, centerY - plusArm),
            D2D1::Point2F(centerX, centerY + plusArm),
            brush.Get(),
            strokeWidth,
            impl_->roundStroke.Get());
    }

    const HRESULT result = impl_->target->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        impl_->target.Reset();
    }
}

void TabStrip::LayoutItems() {
    RECT client{};
    if (hwnd_ == nullptr || !GetClientRect(hwnd_, &client)) {
        return;
    }

    tabRects_.assign(items_.size(), RECT{});
    closeRects_.assign(items_.size(), RECT{});
    newRect_ = {};
    scrollLeftRect_ = {};
    scrollRightRect_ = {};
    visibleCount_ = 0;

    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const int buttonWidth = Scale(31);
    const int minTabWidth = Scale(104);
    const int maxTabWidth = Scale(220);
    int available = std::max(0, width - buttonWidth);
    overflow_ = !items_.empty() && static_cast<int>(items_.size()) * minTabWidth > available;
    if (overflow_) {
        available = std::max(0, available - buttonWidth * 2);
    } else {
        firstVisible_ = 0;
    }

    const std::size_t capacity = items_.empty()
                                     ? 0
                                     : std::max<std::size_t>(1, static_cast<std::size_t>(available / std::max(1, minTabWidth)));
    visibleCount_ = std::min(capacity, items_.size() - std::min(firstVisible_, items_.size()));
    if (visibleCount_ == 0 && !items_.empty()) {
        firstVisible_ = items_.size() - 1;
        visibleCount_ = 1;
    }

    int tabWidth = visibleCount_ == 0 ? 0 : available / static_cast<int>(visibleCount_);
    tabWidth = std::clamp(tabWidth, minTabWidth, maxTabWidth);
    int x = 0;
    for (std::size_t offset = 0; offset < visibleCount_; ++offset) {
        const std::size_t index = firstVisible_ + offset;
        tabRects_[index] = {x, 0, std::min(width, x + tabWidth), height};
        closeRects_[index] = {tabRects_[index].right - Scale(27), Scale(2), tabRects_[index].right - Scale(3), height - Scale(2)};
        x += tabWidth;
    }

    if (overflow_) {
        scrollLeftRect_ = {width - buttonWidth * 3, 0, width - buttonWidth * 2, height};
        scrollRightRect_ = {width - buttonWidth * 2, 0, width - buttonWidth, height};
        newRect_ = {width - buttonWidth, 0, width, height};
    } else {
        newRect_ = {std::min(x, width), 0, std::min(width, x + buttonWidth), height};
    }

    if (tooltip_ != nullptr) {
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.hwnd = hwnd_;
        tool.uId = 1;
        tool.rect = client;
        SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
}

void TabStrip::EnsureActiveVisible() {
    const std::size_t active = ActiveIndex();
    if (active == std::numeric_limits<std::size_t>::max()) {
        return;
    }
    if (active < firstVisible_) {
        firstVisible_ = active;
    } else if (visibleCount_ > 0 && active >= firstVisible_ + visibleCount_) {
        firstVisible_ = active - visibleCount_ + 1;
    }
}

void TabStrip::BeginMouseTracking() {
    if (trackingMouse_) {
        return;
    }
    TRACKMOUSEEVENT tracking{};
    tracking.cbSize = sizeof(tracking);
    tracking.dwFlags = TME_LEAVE;
    tracking.hwndTrack = hwnd_;
    if (TrackMouseEvent(&tracking)) {
        trackingMouse_ = true;
    }
}

TabStrip::HitResult TabStrip::HitTest(POINT point) const {
    if (PtInRect(&newRect_, point)) {
        return {HitPart::NewTab, 0};
    }
    if (PtInRect(&scrollLeftRect_, point)) {
        return {HitPart::ScrollLeft, 0};
    }
    if (PtInRect(&scrollRightRect_, point)) {
        return {HitPart::ScrollRight, 0};
    }
    for (std::size_t i = 0; i < tabRects_.size(); ++i) {
        if (PtInRect(&closeRects_[i], point)) {
            return {HitPart::Close, i};
        }
        if (PtInRect(&tabRects_[i], point)) {
            return {HitPart::Tab, i};
        }
    }
    return {};
}

int TabStrip::Scale(int value) const noexcept {
    return MulDiv(value, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
}

std::size_t TabStrip::ActiveIndex() const noexcept {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].id == activeId_) {
            return i;
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

} // namespace NativePad
