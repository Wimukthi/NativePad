#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "DocumentSession.h"

namespace NativePad {

constexpr UINT WM_TABSTRIP_ACTIVATE = WM_APP + 110;
constexpr UINT WM_TABSTRIP_CLOSE = WM_APP + 111;
constexpr UINT WM_TABSTRIP_NEW = WM_APP + 112;

struct TabStripItem {
    TabId id{};
    std::wstring title;
    std::wstring fullPath;
    bool dirty{false};
};

struct TabStripTheme {
    COLORREF background{};
    COLORREF tab{};
    COLORREF activeTab{};
    COLORREF hotTab{};
    COLORREF text{};
    COLORREF mutedText{};
    COLORREF border{};
    COLORREF accent{};
    COLORREF dirtyMarker{};
};

// A single lightweight HWND paints every tab and button. No editor, DirectWrite
// target, or child button is created per document.
class TabStrip {
public:
    TabStrip();
    ~TabStrip();

    TabStrip(const TabStrip&) = delete;
    TabStrip& operator=(const TabStrip&) = delete;

    static bool Register(HINSTANCE instance);
    bool Create(HWND parent, HINSTANCE instance);

    [[nodiscard]] HWND Hwnd() const noexcept;
    [[nodiscard]] int PreferredHeight(UINT dpi) const noexcept;

    void SetItems(std::vector<TabStripItem> items, TabId activeId);
    void SetActive(TabId activeId);
    void SetTheme(TabStripTheme theme);
    void SetFont(HFONT font);
    void OnDpiChanged(UINT dpi);

private:
    enum class HitPart {
        None,
        Tab,
        Close,
        NewTab,
        ScrollLeft,
        ScrollRight,
    };

    struct HitResult {
        HitPart part{HitPart::None};
        std::size_t index{};
    };

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(UINT message, WPARAM wParam, LPARAM lParam);

    void Paint();
    void PaintTabSurfaces(HDC dc, const RECT& client);
    void PaintAntialiasedControls(HDC dc, const RECT& client);
    bool EnsureControlRenderTarget();
    void LayoutItems();
    void EnsureActiveVisible();
    void BeginMouseTracking();
    [[nodiscard]] HitResult HitTest(POINT point) const;
    [[nodiscard]] int Scale(int value) const noexcept;
    [[nodiscard]] std::size_t ActiveIndex() const noexcept;

    HWND hwnd_{};
    HWND tooltip_{};
    HWND parent_{};
    HINSTANCE instance_{};
    HFONT font_{}; // Borrowed from AppWindow.
    UINT dpi_{USER_DEFAULT_SCREEN_DPI};
    TabStripTheme theme_{};
    std::vector<TabStripItem> items_;
    std::vector<RECT> tabRects_;
    std::vector<RECT> closeRects_;
    RECT newRect_{};
    RECT scrollLeftRect_{};
    RECT scrollRightRect_{};
    TabId activeId_{};
    std::size_t firstVisible_{0};
    std::size_t visibleCount_{0};
    HitResult hot_{};
    bool trackingMouse_{false};
    bool overflow_{false};
    std::wstring tooltipText_;

    struct Impl;
    Impl* impl_{};
};

} // namespace NativePad
