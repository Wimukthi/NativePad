#include "EditorView.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cwctype>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <utility>

#include "MappedTextDocument.h"

namespace NativePad {

namespace {

constexpr wchar_t kEditorClass[] = L"NativePadEditorView";
constexpr int kLeftPadding = 6;
constexpr int kTopPadding = 4;
constexpr int kLineNumberHorizontalPadding = 8;
constexpr int kLineNumberTextGap = 8;
constexpr int kTabSize = 4;
constexpr UINT_PTR kCaretBlinkTimerId = 1;
constexpr std::size_t kMaxUndoActions = 512;

float ColorComponent(BYTE value) {
    return static_cast<float>(value) / 255.0f;
}

D2D1_COLOR_F ToD2DColor(COLORREF color) {
    return D2D1::ColorF(ColorComponent(GetRValue(color)), ColorComponent(GetGValue(color)), ColorComponent(GetBValue(color)), 1.0f);
}

bool IsShiftDown() {
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

bool IsControlDown() {
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

bool IsTopLevelMenuMnemonic(WPARAM key) noexcept {
    switch (static_cast<wchar_t>(std::towupper(static_cast<wint_t>(key)))) {
    case L'F':
    case L'E':
    case L'O':
    case L'V':
    case L'H':
        return true;
    default:
        return false;
    }
}

bool IsLineBreak(wchar_t value) noexcept {
    return value == L'\r' || value == L'\n';
}

bool IsWordTokenChar(wchar_t value) noexcept {
    return value == L'_' || iswalnum(value) != 0;
}

bool IsSelectablePunctuation(wchar_t value) noexcept {
    return !IsLineBreak(value) && !iswspace(value) && !IsWordTokenChar(value);
}

UINT CaretBlinkIntervalMs() noexcept {
    const UINT blinkTime = GetCaretBlinkTime();
    if (blinkTime == 0 || blinkTime == INFINITE) {
        return 0;
    }
    return std::max<UINT>(200, blinkTime);
}

} // namespace

struct EditorView::Impl {
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> lineNumberFormat;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selectionBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> caretBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lineNumberBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lineNumberBackgroundBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lineNumberSeparatorBrush;
};

EditorView::EditorView() : impl_(new Impl()) {
    theme_ = {
        RGB(30, 30, 30),
        RGB(238, 238, 238),
        RGB(60, 92, 140),
        RGB(245, 245, 245),
        RGB(28, 28, 28),
        RGB(150, 150, 150),
        RGB(54, 54, 54),
    };
}

EditorView::~EditorView() {
    delete impl_;
}

bool EditorView::Register(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &EditorView::StaticWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wc.lpszClassName = kEditorClass;

    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    return true;
}

bool EditorView::Create(HWND parent, HINSTANCE instance, DocumentBuffer* document) {
    parent_ = parent;
    instance_ = instance;
    document_ = document;
    mappedDocument_ = nullptr;
    RebuildLineIndex();

    hwnd_ = CreateWindowExW(
        0,
        kEditorClass,
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP | WS_CLIPSIBLINGS,
        0,
        0,
        0,
        0,
        parent,
        nullptr,
        instance,
        this);

    return hwnd_ != nullptr;
}

HWND EditorView::Hwnd() const noexcept {
    return hwnd_;
}

void EditorView::SetDocument(DocumentBuffer* document) {
    document_ = document;
    mappedDocument_ = nullptr;
    ResetView();
}

void EditorView::SetMappedDocument(MappedTextDocument* document) {
    mappedDocument_ = document;
    document_ = nullptr;
    ResetView();
}

void EditorView::ResetView() {
    caret_ = 0;
    anchor_ = 0;
    firstLine_ = 0;
    firstVisualRow_ = 0;
    horizontalColumn_ = 0;
    desiredColumn_ = 0;
    lastDoubleClickTick_ = 0;
    undoStack_.clear();
    redoStack_.clear();
    RebuildLineIndex();
    UpdateScrollbars();
    ResetCaretBlink();
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyCursorChanged();
}

void EditorView::RefreshDocumentMetrics() {
    // The active document changed size outside the editing paths (for example a
    // mapped file grew on disk). Recompute derived state without resetting the
    // caret, selection, scroll position, or undo history.
    const std::size_t length = DocumentLength();
    caret_ = std::min(caret_, length);
    anchor_ = std::min(anchor_, length);
    InvalidateVisualRowCache();
    UpdateScrollbars();
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyCursorChanged();
}

void EditorView::MoveCaretToDocumentEnd() {
    SetCaret(DocumentLength(), false);
}

void EditorView::SetTheme(EditorTheme theme) {
    theme_ = theme;
    if (impl_->textBrush) {
        impl_->textBrush->SetColor(ToD2DColor(theme_.text));
    }
    if (impl_->selectionBrush) {
        impl_->selectionBrush->SetColor(ToD2DColor(theme_.selectionBackground));
    }
    if (impl_->caretBrush) {
        impl_->caretBrush->SetColor(ToD2DColor(theme_.caret));
    }
    if (impl_->lineNumberBrush) {
        impl_->lineNumberBrush->SetColor(ToD2DColor(theme_.lineNumberText));
    }
    if (impl_->lineNumberBackgroundBrush) {
        impl_->lineNumberBackgroundBrush->SetColor(ToD2DColor(theme_.lineNumberBackground));
    }
    if (impl_->lineNumberSeparatorBrush) {
        impl_->lineNumberSeparatorBrush->SetColor(ToD2DColor(theme_.lineNumberSeparator));
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void EditorView::SetFont(EditorFontSpec font) {
    if (font.family.empty()) {
        font.family = L"Consolas";
    }
    font.sizeDips = std::max(6.0f, font.sizeDips);
    font.weight = std::clamp<LONG>(font.weight, FW_THIN, FW_HEAVY);

    font_ = std::move(font);
    RecreateTextFormat();
    InvalidateVisualRowCache();
    ScrollToCaret();
    UpdateScrollbars();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

const EditorFontSpec& EditorView::Font() const noexcept {
    return font_;
}

void EditorView::SetWordWrap(bool enabled) {
    if (wordWrap_ == enabled) {
        return;
    }

    wordWrap_ = enabled;
    InvalidateVisualRowCache();
    horizontalColumn_ = 0;
    firstVisualRow_ = VisualRowIndexForPosition(caret_);
    if (wordWrap_) {
        ShowScrollBar(hwnd_, SB_HORZ, FALSE);
    }
    ScrollToCaret();
    UpdateScrollbars();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

bool EditorView::WordWrap() const noexcept {
    return wordWrap_;
}

void EditorView::SetShowLineNumbers(bool enabled) {
    if (showLineNumbers_ == enabled) {
        return;
    }

    showLineNumbers_ = enabled;
    // The gutter changes usable text width, so wrapped visual rows and
    // horizontal scroll ranges need the same invalidation path as font changes.
    InvalidateVisualRowCache();
    ScrollToCaret();
    UpdateScrollbars();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

bool EditorView::ShowLineNumbers() const noexcept {
    return showLineNumbers_;
}

void EditorView::SetReadOnly(bool readOnly) noexcept {
    readOnly_ = readOnly;
}

void EditorView::SelectRange(std::size_t start, std::size_t length) {
    if (!HasDocument()) {
        return;
    }

    const std::size_t documentLength = DocumentLength();
    anchor_ = std::min(start, documentLength);
    caret_ = std::min(anchor_ + length, documentLength);
    desiredColumn_ = Column();
    ScrollToCaret();
    ResetCaretBlink();
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyCursorChanged();
}

bool EditorView::CanUndo() const noexcept {
    return !readOnly_ && !undoStack_.empty();
}

bool EditorView::CanRedo() const noexcept {
    return !readOnly_ && !redoStack_.empty();
}

bool EditorView::IsReadOnly() const noexcept {
    return readOnly_;
}

bool EditorView::HasSelection() const noexcept {
    return caret_ != anchor_;
}

std::size_t EditorView::CaretPosition() const noexcept {
    return caret_;
}

std::size_t EditorView::SelectionStart() const noexcept {
    return std::min(caret_, anchor_);
}

std::size_t EditorView::SelectionEnd() const noexcept {
    return std::max(caret_, anchor_);
}

std::size_t EditorView::Line() const {
    return LineFromPosition(caret_);
}

std::size_t EditorView::Column() const {
    const std::size_t line = Line();
    return caret_ - LineStart(line);
}

std::size_t EditorView::LineCount() const noexcept {
    return IndexedLineCount();
}

LRESULT CALLBACK EditorView::StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    EditorView* view = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        view = static_cast<EditorView*>(create->lpCreateParams);
        view->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
    } else {
        view = reinterpret_cast<EditorView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (view != nullptr) {
        return view->WndProc(hwnd, message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT EditorView::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        dpi_ = GetDpiForWindow(hwnd_);
        RecreateTextFormat();
        UpdateScrollbars();
        return 0;
    case WM_SIZE:
        if (impl_->target) {
            RECT rect{};
            GetClientRect(hwnd_, &rect);
            impl_->target->Resize(D2D1::SizeU(static_cast<UINT32>(rect.right - rect.left), static_cast<UINT32>(rect.bottom - rect.top)));
        }
        InvalidateVisualRowCache();
        UpdateScrollbars();
        UpdateScrollPositions();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED:
        OnDpiChanged(HIWORD(wParam));
        return 0;
    case WM_PAINT:
        Paint();
        ValidateRect(hwnd, nullptr);
        return 0;
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(hwnd_, &client);
        HBRUSH background = CreateSolidBrush(theme_.background);
        FillRect(reinterpret_cast<HDC>(wParam), &client, background);
        DeleteObject(background);
        return 1;
    }
    case WM_SETFOCUS:
        StartCaretBlink();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_KILLFOCUS:
        StopCaretBlink();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (wParam == kCaretBlinkTimerId) {
            caretVisible_ = !caretVisible_;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, CursorIsInLineNumberGutter() ? IDC_ARROW : IDC_IBEAM));
            return TRUE;
        }
        if (LOWORD(lParam) == HTHSCROLL || LOWORD(lParam) == HTVSCROLL) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_SYSKEYDOWN:
        if (parent_ != nullptr && (wParam == VK_MENU || wParam == VK_F10 || IsTopLevelMenuMnemonic(wParam))) {
            SendMessageW(parent_, message, wParam, lParam);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_SYSKEYUP:
        if (parent_ != nullptr && wParam == VK_MENU) {
            SendMessageW(parent_, message, wParam, lParam);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_CHAR:
        OnCharacter(static_cast<wchar_t>(wParam));
        return 0;
    case WM_KEYDOWN:
        if (parent_ != nullptr && wParam == VK_F10) {
            SendMessageW(parent_, message, wParam, lParam);
            return 0;
        }
        OnKeyDown(wParam);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd_);
        OnMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), IsShiftDown());
        return 0;
    case WM_LBUTTONDBLCLK:
        SetFocus(hwnd_);
        OnMouseDoubleClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        if (dragging_) {
            OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        }
        return 0;
    case WM_LBUTTONUP:
        ReleaseMouseDrag();
        return 0;
    case WM_MOUSEWHEEL:
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    case WM_CONTEXTMENU:
        if (parent_ != nullptr) {
            SendMessageW(parent_, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(hwnd_), lParam);
        }
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = SIF_ALL;
        GetScrollInfo(hwnd_, SB_VERT, &info);

        std::size_t next = wordWrap_ ? firstVisualRow_ : firstLine_;
        switch (LOWORD(wParam)) {
        case SB_LINEUP:
            next = next > 0 ? next - 1 : 0;
            break;
        case SB_LINEDOWN:
            ++next;
            break;
        case SB_PAGEUP:
            next = next > static_cast<std::size_t>(info.nPage) ? next - info.nPage : 0;
            break;
        case SB_PAGEDOWN:
            next += info.nPage;
            break;
        case SB_THUMBTRACK:
            next = static_cast<std::size_t>(info.nTrackPos);
            break;
        default:
            break;
        }

        if (wordWrap_) {
            const std::size_t visibleLines = std::max<std::size_t>(1, static_cast<std::size_t>(ClientHeightDips() / std::max(1.0f, lineHeight_)));
            firstVisualRow_ = std::min(next, MaxFirstVisualRow(visibleLines));
        } else {
            firstLine_ = std::min(next, IndexedLineCount() - 1);
        }
        UpdateScrollPositions();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    case WM_HSCROLL: {
        std::size_t next = horizontalColumn_;
        switch (LOWORD(wParam)) {
        case SB_LINELEFT:
            next = next > 0 ? next - 1 : 0;
            break;
        case SB_LINERIGHT:
            ++next;
            break;
        case SB_PAGELEFT:
            next = next > 8 ? next - 8 : 0;
            break;
        case SB_PAGERIGHT:
            next += 8;
            break;
        case SB_THUMBTRACK: {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd_, SB_HORZ, &info);
            next = static_cast<std::size_t>(info.nTrackPos);
            break;
        }
        default:
            break;
        }

        horizontalColumn_ = next;
        UpdateScrollPositions();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

void EditorView::Paint() {
    // Direct2D owns the editor surface. Only visible rows are materialized from
    // the document backend, which keeps mapped large files responsive.
    if (!EnsureDeviceResources()) {
        return;
    }

    const float topPadding = static_cast<float>(kTopPadding);
    const float width = ClientWidthDips();
    const float height = ClientHeightDips();
    const float textLeft = TextLeftDips();
    const float gutterWidth = LineNumberGutterWidthDips();
    const D2D1_RECT_F textClip = D2D1::RectF(textLeft, 0.0f, width, height);

    impl_->target->BeginDraw();
    impl_->target->Clear(ToD2DColor(theme_.background));

    const std::size_t visibleRows = static_cast<std::size_t>(std::ceil(height / lineHeight_)) + 1;
    const std::size_t firstRow = wordWrap_ ? firstVisualRow_ : firstLine_;
    const std::size_t totalRows = wordWrap_ ? TotalVisualRows() : IndexedLineCount();
    const std::size_t lastRow = std::min(totalRows, firstRow + visibleRows);
    const std::size_t selectionStart = SelectionStart();
    const std::size_t selectionEnd = SelectionEnd();
    const float xOrigin = wordWrap_ ? textLeft : textLeft - (static_cast<float>(horizontalColumn_) * charWidth_);
    const std::size_t wrapColumns = WrapColumnCount();

    if (showLineNumbers_ && gutterWidth > 0.0f && impl_->lineNumberBackgroundBrush && impl_->lineNumberSeparatorBrush) {
        impl_->target->FillRectangle(D2D1::RectF(0.0f, 0.0f, gutterWidth, height), impl_->lineNumberBackgroundBrush.Get());
        impl_->target->DrawLine(
            D2D1::Point2F(gutterWidth - 0.5f, 0.0f),
            D2D1::Point2F(gutterWidth - 0.5f, height),
            impl_->lineNumberSeparatorBrush.Get(),
            1.0f);
    }

    const bool hasTextClip = textClip.left < textClip.right && textClip.top < textClip.bottom;

    if (showLineNumbers_ && impl_->lineNumberFormat && impl_->lineNumberBrush) {
        for (std::size_t row = firstRow; row < lastRow; ++row) {
            const VisualRow visual = wordWrap_ ? VisualRowFromIndex(row) : VisualRow{row, 0, LineLength(row)};
            if (wordWrap_ && visual.columnStart != 0) {
                continue;
            }

            const float y = topPadding + (static_cast<float>(row - firstRow) * lineHeight_);
            const std::wstring lineNumber = std::to_wstring(visual.line + 1);
            impl_->target->DrawTextW(
                lineNumber.c_str(),
                static_cast<UINT32>(lineNumber.size()),
                impl_->lineNumberFormat.Get(),
                D2D1::RectF(0.0f, y, std::max(0.0f, gutterWidth - static_cast<float>(kLineNumberTextGap)), y + lineHeight_),
                impl_->lineNumberBrush.Get(),
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    // Text, selection, and the caret are clipped separately from the gutter so
    // horizontally scrolled long lines cannot paint back under line numbers.
    if (hasTextClip) {
        impl_->target->PushAxisAlignedClip(textClip, D2D1_ANTIALIAS_MODE_ALIASED);
    }

    for (std::size_t row = firstRow; row < lastRow; ++row) {
        const VisualRow visual = wordWrap_ ? VisualRowFromIndex(row) : VisualRow{row, 0, LineLength(row)};
        const float y = topPadding + (static_cast<float>(row - firstRow) * lineHeight_);
        const std::size_t lineStart = LineStart(visual.line);
        const std::size_t lineEnd = LineEnd(visual.line);
        const std::size_t segmentStart = std::min(lineStart + visual.columnStart, lineEnd);
        const std::size_t segmentEnd = wordWrap_
                                           ? std::min(lineStart + visual.columnStart + wrapColumns, lineEnd)
                                           : lineEnd;

        if (hasTextClip && selectionStart != selectionEnd) {
            const std::size_t selectedStart = std::max(selectionStart, segmentStart);
            const std::size_t selectedEnd = std::min(selectionEnd, segmentEnd);
            if (selectedStart < selectedEnd) {
                const float sx = xOrigin + (static_cast<float>(selectedStart - segmentStart) * charWidth_);
                const float ex = xOrigin + (static_cast<float>(selectedEnd - segmentStart) * charWidth_);
                impl_->target->FillRectangle(D2D1::RectF(sx, y, ex, y + lineHeight_), impl_->selectionBrush.Get());
            }
        }

        const std::wstring text = DocumentTextRange(segmentStart, segmentEnd - segmentStart);
        if (hasTextClip && !text.empty()) {
            impl_->target->DrawTextW(
                text.c_str(),
                static_cast<UINT32>(text.size()),
                impl_->textFormat.Get(),
                D2D1::RectF(xOrigin, y, width + std::abs(xOrigin) + 2048.0f, y + lineHeight_),
                impl_->textBrush.Get(),
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    if (hasTextClip && GetFocus() == hwnd_ && caretVisible_) {
        const std::size_t caretRow = wordWrap_ ? VisualRowIndexForPosition(caret_) : LineFromPosition(caret_);
        if (caretRow >= firstRow && caretRow < lastRow) {
            const VisualRow visual = wordWrap_ ? VisualRowFromIndex(caretRow) : VisualRow{caretRow, 0, LineLength(caretRow)};
            const std::size_t segmentStart = LineStart(visual.line) + visual.columnStart;
            const float x = xOrigin + (static_cast<float>(caret_ - segmentStart) * charWidth_);
            const float y = topPadding + (static_cast<float>(caretRow - firstRow) * lineHeight_);
            impl_->target->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x, y + lineHeight_), impl_->caretBrush.Get(), 1.0f);
        }
    }

    if (hasTextClip) {
        impl_->target->PopAxisAlignedClip();
    }

    const HRESULT hr = impl_->target->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void EditorView::DiscardDeviceResources() {
    impl_->target.Reset();
    impl_->textBrush.Reset();
    impl_->selectionBrush.Reset();
    impl_->caretBrush.Reset();
    impl_->lineNumberBrush.Reset();
    impl_->lineNumberBackgroundBrush.Reset();
    impl_->lineNumberSeparatorBrush.Reset();
}

void EditorView::ResetDeviceResources() {
    // Sleep/resume and display-driver changes can leave an HWND render target
    // with stale back-buffer contents until a full recreate is forced.
    DiscardDeviceResources();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

bool EditorView::EnsureDeviceResources() {
    if (!impl_->d2dFactory) {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, impl_->d2dFactory.GetAddressOf()))) {
            return false;
        }
    }

    if (!impl_->dwriteFactory) {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(impl_->dwriteFactory.GetAddressOf())))) {
            return false;
        }
    }

    if (!impl_->textFormat) {
        RecreateTextFormat();
    }

    if (!impl_->target) {
        RECT rect{};
        GetClientRect(hwnd_, &rect);
        const D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(std::max(1L, rect.right - rect.left)),
                                             static_cast<UINT32>(std::max(1L, rect.bottom - rect.top)));
        if (FAILED(impl_->d2dFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
                                                             D2D1::HwndRenderTargetProperties(hwnd_, size),
                                                             impl_->target.GetAddressOf()))) {
            return false;
        }

        impl_->target->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        impl_->target->CreateSolidColorBrush(ToD2DColor(theme_.text), impl_->textBrush.GetAddressOf());
        impl_->target->CreateSolidColorBrush(ToD2DColor(theme_.selectionBackground), impl_->selectionBrush.GetAddressOf());
        impl_->target->CreateSolidColorBrush(ToD2DColor(theme_.caret), impl_->caretBrush.GetAddressOf());
        impl_->target->CreateSolidColorBrush(ToD2DColor(theme_.lineNumberText), impl_->lineNumberBrush.GetAddressOf());
        impl_->target->CreateSolidColorBrush(ToD2DColor(theme_.lineNumberBackground), impl_->lineNumberBackgroundBrush.GetAddressOf());
        impl_->target->CreateSolidColorBrush(ToD2DColor(theme_.lineNumberSeparator), impl_->lineNumberSeparatorBrush.GetAddressOf());
    }

    return impl_->textFormat != nullptr && impl_->target != nullptr && impl_->textBrush != nullptr;
}

void EditorView::RecreateTextFormat() {
    if (!impl_->dwriteFactory) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(impl_->dwriteFactory.GetAddressOf()));
    }

    if (!impl_->dwriteFactory) {
        return;
    }

    impl_->textFormat.Reset();
    impl_->lineNumberFormat.Reset();
    if (SUCCEEDED(impl_->dwriteFactory->CreateTextFormat(
            font_.family.c_str(),
            nullptr,
            static_cast<DWRITE_FONT_WEIGHT>(font_.weight),
            font_.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            font_.sizeDips,
            L"",
            impl_->textFormat.GetAddressOf()))) {
        impl_->textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if (SUCCEEDED(impl_->dwriteFactory->CreateTextFormat(
                font_.family.c_str(),
                nullptr,
                static_cast<DWRITE_FONT_WEIGHT>(font_.weight),
                font_.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                font_.sizeDips,
                L"",
                impl_->lineNumberFormat.GetAddressOf()))) {
            impl_->lineNumberFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            impl_->lineNumberFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
        MeasureTextMetrics();
    }
}

void EditorView::MeasureTextMetrics() {
    if (!impl_->dwriteFactory || !impl_->textFormat) {
        return;
    }

    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (SUCCEEDED(impl_->dwriteFactory->CreateTextLayout(L"M", 1, impl_->textFormat.Get(), 200.0f, 200.0f, layout.GetAddressOf()))) {
        DWRITE_TEXT_METRICS metrics{};
        if (SUCCEEDED(layout->GetMetrics(&metrics))) {
            charWidth_ = std::max(1.0f, metrics.widthIncludingTrailingWhitespace);
            lineHeight_ = std::max(1.0f, metrics.height + 2.0f);
        }
    }
}

void EditorView::OnDpiChanged(UINT dpi) {
    dpi_ = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
    if (impl_->target) {
        impl_->target->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        RECT rect{};
        GetClientRect(hwnd_, &rect);
        impl_->target->Resize(D2D1::SizeU(static_cast<UINT32>(std::max(1L, rect.right - rect.left)),
                                          static_cast<UINT32>(std::max(1L, rect.bottom - rect.top))));
    }

    MeasureTextMetrics();
    ScrollToCaret();
    UpdateScrollbars();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void EditorView::RebuildLineIndex() {
    // Mapped documents carry their own line index. Editable documents use the
    // local index because edits can update it incrementally.
    lineIndex_.Reset(document_ != nullptr ? document_->Text() : L"");
    InvalidateVisualRowCache();
}

void EditorView::UpdateLineIndexForEdit(std::size_t position, std::wstring_view erased, std::wstring_view inserted) {
    lineIndex_.ApplyEdit(position, erased, inserted);
    InvalidateVisualRowCache();
}

void EditorView::InvalidateVisualRowCache() noexcept {
    visualRowStarts_.clear();
    visualRowCacheColumns_ = 0;
    totalVisualRows_ = 1;
    visualRowCacheValid_ = false;
}

void EditorView::EnsureVisualRowCache() const {
    // Word wrap turns one logical line into N visual rows. The prefix table lets
    // scroll thumbs, hit testing, and caret movement avoid rescanning all lines.
    if (!wordWrap_) {
        return;
    }

    const std::size_t columns = WrapColumnCount();
    const std::size_t lineCount = IndexedLineCount();
    if (visualRowCacheValid_ && visualRowCacheColumns_ == columns && visualRowStarts_.size() == lineCount + 1) {
        return;
    }

    visualRowStarts_.clear();
    visualRowStarts_.reserve(lineCount + 1);
    visualRowStarts_.push_back(0);

    std::size_t rows = 0;
    for (std::size_t line = 0; line < lineCount; ++line) {
        rows += VisualRowCountForLine(line, columns);
        visualRowStarts_.push_back(rows);
    }

    visualRowCacheColumns_ = columns;
    totalVisualRows_ = std::max<std::size_t>(1, rows);
    visualRowCacheValid_ = true;
}

void EditorView::UpdateScrollbars() {
    if (hwnd_ == nullptr) {
        return;
    }

    const int visibleLines = std::max(1, static_cast<int>(ClientHeightDips() / std::max(1.0f, lineHeight_)));
    const std::size_t totalRows = wordWrap_ ? TotalVisualRows() : IndexedLineCount();
    const int totalLines = std::max(1, static_cast<int>(std::min<std::size_t>(totalRows, static_cast<std::size_t>(INT_MAX))));

    SCROLLINFO vertical{};
    vertical.cbSize = sizeof(vertical);
    vertical.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    vertical.nMin = 0;
    vertical.nMax = totalLines - 1;
    vertical.nPage = static_cast<UINT>(visibleLines);
    vertical.nPos = static_cast<int>(std::min(wordWrap_ ? firstVisualRow_ : firstLine_, totalRows - 1));
    SetScrollInfo(hwnd_, SB_VERT, &vertical, TRUE);

    if (wordWrap_) {
        ShowScrollBar(hwnd_, SB_HORZ, FALSE);
        return;
    }

    const std::size_t maxLineLength = IndexedMaxLineLength();
    const int visibleColumns = std::max(1, static_cast<int>(TextViewportWidthDips() / std::max(1.0f, charWidth_)));
    if (maxLineLength <= static_cast<std::size_t>(visibleColumns)) {
        horizontalColumn_ = 0;
        ShowScrollBar(hwnd_, SB_HORZ, FALSE);
        return;
    }

    ShowScrollBar(hwnd_, SB_HORZ, TRUE);
    const std::size_t maxScrollPosition = maxLineLength - static_cast<std::size_t>(visibleColumns) + 1;
    horizontalColumn_ = std::min(horizontalColumn_, maxScrollPosition);

    SCROLLINFO horizontal{};
    horizontal.cbSize = sizeof(horizontal);
    horizontal.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    horizontal.nMin = 0;
    horizontal.nMax = static_cast<int>(std::min<std::size_t>(maxLineLength, INT_MAX));
    horizontal.nPage = static_cast<UINT>(visibleColumns);
    horizontal.nPos = static_cast<int>(horizontalColumn_);
    SetScrollInfo(hwnd_, SB_HORZ, &horizontal, TRUE);
}

void EditorView::UpdateScrollPositions() {
    if (hwnd_ == nullptr) {
        return;
    }

    const std::size_t verticalPosition = wordWrap_ ? firstVisualRow_ : firstLine_;
    SetScrollPos(hwnd_, SB_VERT, static_cast<int>(std::min<std::size_t>(verticalPosition, static_cast<std::size_t>(INT_MAX))), TRUE);
    if (!wordWrap_) {
        SetScrollPos(hwnd_, SB_HORZ, static_cast<int>(std::min<std::size_t>(horizontalColumn_, static_cast<std::size_t>(INT_MAX))), TRUE);
    }
}

void EditorView::StartCaretBlink() {
    caretVisible_ = true;
    if (hwnd_ == nullptr || GetFocus() != hwnd_) {
        return;
    }

    KillTimer(hwnd_, kCaretBlinkTimerId);
    const UINT blinkInterval = CaretBlinkIntervalMs();
    if (blinkInterval != 0) {
        SetTimer(hwnd_, kCaretBlinkTimerId, blinkInterval, nullptr);
    }
}

void EditorView::StopCaretBlink() {
    if (hwnd_ != nullptr) {
        KillTimer(hwnd_, kCaretBlinkTimerId);
    }
    caretVisible_ = false;
}

void EditorView::ResetCaretBlink() {
    // The caret should reappear immediately after movement/editing, then resume
    // the system blink cadence instead of waiting for the next timer tick.
    StartCaretBlink();
}

void EditorView::ScrollToCaret() {
    const std::size_t caretLine = LineFromPosition(caret_);
    const std::size_t visibleLines = std::max<std::size_t>(1, static_cast<std::size_t>(ClientHeightDips() / std::max(1.0f, lineHeight_)));

    if (wordWrap_) {
        const std::size_t caretRow = VisualRowIndexForPosition(caret_);
        if (caretRow < firstVisualRow_) {
            firstVisualRow_ = caretRow;
        } else if (caretRow >= firstVisualRow_ + visibleLines) {
            firstVisualRow_ = caretRow - visibleLines + 1;
        }
        firstVisualRow_ = std::min(firstVisualRow_, MaxFirstVisualRow(visibleLines));
        UpdateScrollbars();
        return;
    }

    if (caretLine < firstLine_) {
        firstLine_ = caretLine;
    } else if (caretLine >= firstLine_ + visibleLines) {
        firstLine_ = caretLine - visibleLines + 1;
    }

    const std::size_t column = caret_ - LineStart(caretLine);
    const std::size_t visibleColumns = std::max<std::size_t>(1, static_cast<std::size_t>(TextViewportWidthDips() / std::max(1.0f, charWidth_)));
    if (column < horizontalColumn_) {
        horizontalColumn_ = column;
    } else if (column >= horizontalColumn_ + visibleColumns) {
        horizontalColumn_ = column - visibleColumns + 1;
    }

    UpdateScrollbars();
}

void EditorView::SetCaret(std::size_t position, bool extendSelection) {
    const std::size_t length = DocumentLength();
    caret_ = std::min(position, length);
    if (!extendSelection) {
        anchor_ = caret_;
    }
    desiredColumn_ = CaretDisplayColumn();
    ScrollToCaret();
    ResetCaretBlink();
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyCursorChanged();
}

void EditorView::MoveCaretHorizontal(int delta, bool extendSelection) {
    if (!extendSelection && HasSelection()) {
        SetCaret(delta < 0 ? SelectionStart() : SelectionEnd(), false);
        return;
    }

    if (delta < 0 && caret_ > 0) {
        SetCaret(caret_ - 1, extendSelection);
    } else if (delta > 0 && HasDocument() && caret_ < DocumentLength()) {
        SetCaret(caret_ + 1, extendSelection);
    }
}

void EditorView::MoveCaretVertical(int delta, bool extendSelection) {
    if (wordWrap_) {
        const std::size_t currentRow = VisualRowIndexForPosition(caret_);
        const std::size_t totalRows = TotalVisualRows();
        const std::size_t nextRow = delta < 0
                                        ? (currentRow > 0 ? currentRow - 1 : 0)
                                        : std::min(currentRow + 1, totalRows - 1);
        const VisualRow visual = VisualRowFromIndex(nextRow);
        const std::size_t column = std::min(desiredColumn_, visual.length);
        caret_ = LineStart(visual.line) + visual.columnStart + column;
        if (!extendSelection) {
            anchor_ = caret_;
        }
        ScrollToCaret();
        ResetCaretBlink();
        InvalidateRect(hwnd_, nullptr, FALSE);
        NotifyCursorChanged();
        return;
    }

    const std::size_t line = Line();
    const std::size_t nextLine = delta < 0
                                     ? (line > 0 ? line - 1 : 0)
                                     : std::min(line + 1, IndexedLineCount() - 1);
    const std::size_t column = std::min(desiredColumn_, LineLength(nextLine));
    const std::size_t next = PositionFromLineColumn(nextLine, column);
    caret_ = next;
    if (!extendSelection) {
        anchor_ = caret_;
    }
    ScrollToCaret();
    ResetCaretBlink();
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyCursorChanged();
}

void EditorView::MoveCaretToLineBoundary(bool end, bool extendSelection) {
    const std::size_t line = Line();
    SetCaret(end ? LineEnd(line) : LineStart(line), extendSelection);
}

void EditorView::DeleteSelectionOrRange(bool backspace) {
    if (readOnly_) {
        return;
    }

    if (HasSelection()) {
        ApplyEdit(SelectionStart(), SelectionEnd() - SelectionStart(), L"", true);
        return;
    }

    if (document_ == nullptr) {
        return;
    }

    if (backspace) {
        if (caret_ > 0) {
            std::size_t position = caret_ - 1;
            std::size_t length = 1;
            if (document_->CharAt(position) == L'\n' && position > 0 && document_->CharAt(position - 1) == L'\r') {
                --position;
                length = 2;
            }
            ApplyEdit(position, length, L"", true);
        }
    } else if (caret_ < document_->Length()) {
        std::size_t length = 1;
        if (document_->CharAt(caret_) == L'\r' && caret_ + 1 < document_->Length() && document_->CharAt(caret_ + 1) == L'\n') {
            length = 2;
        }
        ApplyEdit(caret_, length, L"", true);
    }
}

void EditorView::InsertText(std::wstring text) {
    if (readOnly_) {
        return;
    }

    if (text.empty()) {
        return;
    }

    const std::size_t start = SelectionStart();
    const std::size_t eraseLength = SelectionEnd() - start;
    ApplyEdit(start, eraseLength, std::move(text), true);
}

void EditorView::ApplyEdit(std::size_t position, std::size_t eraseLength, std::wstring insertText, bool recordUndo) {
    // All editing funnels through this method so undo, line index updates, caret
    // placement, and notifications stay consistent.
    if (readOnly_ || document_ == nullptr) {
        return;
    }

    const std::size_t caretBefore = caret_;
    const std::wstring erased = document_->TextRange(position, eraseLength);
    document_->Replace(position, eraseLength, insertText);
    caret_ = position + insertText.size();
    anchor_ = caret_;
    UpdateLineIndexForEdit(position, erased, insertText);
    desiredColumn_ = CaretDisplayColumn();
    ScrollToCaret();
    UpdateScrollbars();
    ResetCaretBlink();

    if (recordUndo) {
        PushUndo({position, erased, std::move(insertText), caretBefore, caret_});
        redoStack_.clear();
        NotifyChanged();
    }

    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyCursorChanged();
}

void EditorView::PushUndo(EditAction action) {
    undoStack_.push_back(std::move(action));
    if (undoStack_.size() > kMaxUndoActions) {
        undoStack_.erase(undoStack_.begin());
    }
}

void EditorView::NotifyChanged() {
    if (parent_ != nullptr) {
        SendMessageW(parent_, WM_EDITOR_CHANGED, 0, 0);
    }
}

void EditorView::NotifyCursorChanged() const {
    if (parent_ != nullptr) {
        SendMessageW(parent_, WM_EDITOR_CURSOR_CHANGED, 0, 0);
    }
}

void EditorView::OnCharacter(wchar_t value) {
    if (value == L'\b' || value == 0x7F || value == L'\t') {
        return;
    }

    if (value == L'\r') {
        InsertText(L"\r\n");
        return;
    }

    if (value >= L' ') {
        InsertText(std::wstring(1, value));
    }
}

void EditorView::OnKeyDown(WPARAM key) {
    const bool shift = IsShiftDown();
    const bool control = IsControlDown();

    if (control) {
        switch (key) {
        case 'A':
            SelectAll();
            return;
        case 'C':
            Copy();
            return;
        case 'V':
            if (!readOnly_) {
                Paste();
            }
            return;
        case 'X':
            if (!readOnly_) {
                Cut();
            }
            return;
        case 'Z':
            Undo();
            return;
        case 'Y':
            Redo();
            return;
        default:
            break;
        }
    }

    switch (key) {
    case VK_LEFT:
        MoveCaretHorizontal(-1, shift);
        break;
    case VK_RIGHT:
        MoveCaretHorizontal(1, shift);
        break;
    case VK_UP:
        MoveCaretVertical(-1, shift);
        break;
    case VK_DOWN:
        MoveCaretVertical(1, shift);
        break;
    case VK_HOME:
        MoveCaretToLineBoundary(false, shift);
        break;
    case VK_END:
        MoveCaretToLineBoundary(true, shift);
        break;
    case VK_PRIOR:
        for (int i = 0; i < 20; ++i) {
            MoveCaretVertical(-1, shift);
        }
        break;
    case VK_NEXT:
        for (int i = 0; i < 20; ++i) {
            MoveCaretVertical(1, shift);
        }
        break;
    case VK_BACK:
        if (!readOnly_) {
            DeleteSelectionOrRange(true);
        }
        break;
    case VK_DELETE:
        if (!readOnly_) {
            DeleteSelectionOrRange(false);
        }
        break;
    case VK_TAB:
        if (!readOnly_) {
            InsertText(std::wstring(kTabSize, L' '));
        }
        break;
    default:
        break;
    }
}

void EditorView::OnMouseDown(int x, int y, bool extendSelection) {
    const DWORD now = GetTickCount();
    if (!extendSelection && IsTripleClick(x, y, now)) {
        SelectLineAtPosition(HitTest(x, y));
        lastDoubleClickTick_ = 0;
        ReleaseMouseDrag();
        return;
    }

    if (!extendSelection) {
        lastDoubleClickTick_ = 0;
    }
    SetCaret(HitTest(x, y), extendSelection);
    CaptureMouseDrag();
}

void EditorView::OnMouseDoubleClick(int x, int y) {
    const DWORD now = GetTickCount();
    if (IsTripleClick(x, y, now)) {
        SelectLineAtPosition(HitTest(x, y));
        lastDoubleClickTick_ = 0;
        ReleaseMouseDrag();
        return;
    }

    SelectWordAt(HitTest(x, y));
    RememberDoubleClick(x, y, now);
    ReleaseMouseDrag();
}

void EditorView::OnMouseMove(int x, int y) {
    SetCaret(HitTest(x, y), true);
}

void EditorView::OnMouseWheel(short delta) {
    const int lines = std::max(1, std::abs(delta) / WHEEL_DELTA * 3);
    if (wordWrap_) {
        const std::size_t visibleLines = std::max<std::size_t>(1, static_cast<std::size_t>(ClientHeightDips() / std::max(1.0f, lineHeight_)));
        if (delta > 0) {
            firstVisualRow_ = firstVisualRow_ > static_cast<std::size_t>(lines) ? firstVisualRow_ - static_cast<std::size_t>(lines) : 0;
        } else {
            firstVisualRow_ = std::min(firstVisualRow_ + static_cast<std::size_t>(lines), MaxFirstVisualRow(visibleLines));
        }
    } else {
        if (delta > 0) {
            firstLine_ = firstLine_ > static_cast<std::size_t>(lines) ? firstLine_ - static_cast<std::size_t>(lines) : 0;
        } else {
            firstLine_ = std::min(firstLine_ + static_cast<std::size_t>(lines), IndexedLineCount() - 1);
        }
    }
    UpdateScrollPositions();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void EditorView::CaptureMouseDrag() {
    if (!dragging_) {
        SetCapture(hwnd_);
        dragging_ = true;
    }
}

void EditorView::ReleaseMouseDrag() {
    if (dragging_) {
        ReleaseCapture();
        dragging_ = false;
    }
}

bool EditorView::HasDocument() const noexcept {
    return document_ != nullptr || mappedDocument_ != nullptr;
}

std::size_t EditorView::DocumentLength() const noexcept {
    if (mappedDocument_ != nullptr) {
        return mappedDocument_->Length();
    }
    return document_ != nullptr ? document_->Length() : 0;
}

wchar_t EditorView::DocumentCharAt(std::size_t position) const {
    if (mappedDocument_ != nullptr) {
        return mappedDocument_->CharAt(position);
    }
    if (document_ != nullptr) {
        return document_->CharAt(position);
    }
    throw std::out_of_range("EditorView::DocumentCharAt");
}

std::wstring EditorView::DocumentTextRange(std::size_t position, std::size_t length) const {
    if (mappedDocument_ != nullptr) {
        return mappedDocument_->TextRange(position, length);
    }
    return document_ != nullptr ? document_->TextRange(position, length) : std::wstring();
}

std::size_t EditorView::IndexedLineCount() const noexcept {
    return mappedDocument_ != nullptr ? mappedDocument_->LineCount() : lineIndex_.LineCount();
}

std::size_t EditorView::IndexedMaxLineLength() const noexcept {
    return mappedDocument_ != nullptr ? mappedDocument_->MaxLineLength() : lineIndex_.MaxLineLength();
}

std::size_t EditorView::HitTest(int x, int y) const {
    const float xDip = PixelsToDips(x);
    const float yDip = PixelsToDips(y);
    const int lineOffset = std::max(0, static_cast<int>((yDip - static_cast<float>(kTopPadding)) / lineHeight_));
    if (wordWrap_) {
        const VisualRow visual = VisualRowFromIndex(firstVisualRow_ + static_cast<std::size_t>(lineOffset));
        const int rawColumn = static_cast<int>(std::round((xDip - TextLeftDips()) / charWidth_));
        const std::size_t column = visual.columnStart + static_cast<std::size_t>(std::max(0, rawColumn));
        return PositionFromLineColumn(visual.line, std::min(column, visual.columnStart + visual.length));
    }

    const std::size_t line = std::min(firstLine_ + static_cast<std::size_t>(lineOffset), IndexedLineCount() - 1);
    const int rawColumn = static_cast<int>(std::round((xDip - TextLeftDips()) / charWidth_));
    const std::size_t column = static_cast<std::size_t>(std::max(0, rawColumn)) + horizontalColumn_;
    return PositionFromLineColumn(line, std::min(column, LineLength(line)));
}

std::size_t EditorView::LineStart(std::size_t line) const {
    return mappedDocument_ != nullptr ? mappedDocument_->LineStart(line) : lineIndex_.LineStart(line);
}

std::size_t EditorView::LineEnd(std::size_t line) const {
    const std::size_t lineCount = IndexedLineCount();
    if (!HasDocument() || lineCount == 0) {
        return 0;
    }

    const std::size_t start = LineStart(line);
    std::size_t end = line + 1 < lineCount ? LineStart(line + 1) : DocumentLength();
    while (end > start) {
        const wchar_t ch = DocumentCharAt(end - 1);
        if (ch == L'\n' || ch == L'\r') {
            --end;
        } else {
            break;
        }
    }
    return end;
}

std::size_t EditorView::LineLength(std::size_t line) const {
    return LineEnd(line) - LineStart(line);
}

std::size_t EditorView::LineFromPosition(std::size_t position) const {
    return mappedDocument_ != nullptr ? mappedDocument_->LineFromPosition(position) : lineIndex_.LineFromPosition(position);
}

std::size_t EditorView::PositionFromLineColumn(std::size_t line, std::size_t column) const {
    return LineStart(line) + std::min(column, LineLength(line));
}

void EditorView::SelectWordAt(std::size_t position) {
    if (!HasDocument() || DocumentLength() == 0) {
        return;
    }

    const std::size_t line = LineFromPosition(std::min(position, DocumentLength()));
    const std::size_t lineStart = LineStart(line);
    const std::size_t lineEnd = LineEnd(line);
    if (lineStart >= lineEnd) {
        SetCaret(lineStart, false);
        return;
    }

    std::size_t cursor = position >= lineEnd ? lineEnd - 1 : std::max(lineStart, position);
    wchar_t current = DocumentCharAt(cursor);
    if ((iswspace(current) != 0 || IsLineBreak(current)) && cursor > lineStart) {
        const wchar_t previous = DocumentCharAt(cursor - 1);
        if (iswspace(previous) == 0 && !IsLineBreak(previous)) {
            --cursor;
            current = previous;
        }
    }

    if (iswspace(current) != 0 || IsLineBreak(current)) {
        SetCaret(position, false);
        return;
    }

    const bool wordToken = IsWordTokenChar(current);
    const bool punctuationToken = IsSelectablePunctuation(current);
    if (!wordToken && !punctuationToken) {
        SetCaret(position, false);
        return;
    }

    auto sameTokenClass = [&](wchar_t value) {
        return wordToken ? IsWordTokenChar(value) : IsSelectablePunctuation(value);
    };

    std::size_t start = cursor;
    while (start > lineStart && sameTokenClass(DocumentCharAt(start - 1))) {
        --start;
    }

    std::size_t end = cursor + 1;
    while (end < lineEnd && sameTokenClass(DocumentCharAt(end))) {
        ++end;
    }

    SelectRange(start, end - start);
}

void EditorView::SelectLineAtPosition(std::size_t position) {
    if (!HasDocument()) {
        return;
    }

    const std::size_t lineCount = IndexedLineCount();
    if (lineCount == 0) {
        SetCaret(0, false);
        return;
    }

    const std::size_t line = LineFromPosition(std::min(position, DocumentLength()));
    const std::size_t start = LineStart(line);
    const std::size_t end = line + 1 < lineCount ? LineStart(line + 1) : DocumentLength();
    SelectRange(start, end - start);
}

bool EditorView::IsTripleClick(int x, int y, DWORD now) const noexcept {
    if (lastDoubleClickTick_ == 0 || now - lastDoubleClickTick_ > GetDoubleClickTime()) {
        return false;
    }

    const int maxX = std::max(1, GetSystemMetrics(SM_CXDOUBLECLK) / 2);
    const int maxY = std::max(1, GetSystemMetrics(SM_CYDOUBLECLK) / 2);
    return std::abs(x - lastDoubleClickPoint_.x) <= maxX && std::abs(y - lastDoubleClickPoint_.y) <= maxY;
}

void EditorView::RememberDoubleClick(int x, int y, DWORD now) noexcept {
    lastDoubleClickPoint_ = {x, y};
    lastDoubleClickTick_ = now;
}

std::size_t EditorView::WrapColumnCount() const {
    if (!wordWrap_) {
        return static_cast<std::size_t>(INT_MAX);
    }

    return std::max<std::size_t>(1, static_cast<std::size_t>(TextViewportWidthDips() / std::max(1.0f, charWidth_)));
}

std::size_t EditorView::VisualRowCountForLine(std::size_t line) const {
    return VisualRowCountForLine(line, WrapColumnCount());
}

std::size_t EditorView::VisualRowCountForLine(std::size_t line, std::size_t columns) const {
    if (!wordWrap_) {
        return 1;
    }

    const std::size_t length = LineLength(line);
    return std::max<std::size_t>(1, (length + columns - 1) / columns);
}

std::size_t EditorView::TotalVisualRows() const {
    if (!wordWrap_) {
        return IndexedLineCount();
    }

    EnsureVisualRowCache();
    return totalVisualRows_;
}

std::size_t EditorView::VisualRowIndexForPosition(std::size_t position) const {
    const std::size_t line = LineFromPosition(position);
    if (!wordWrap_) {
        return line;
    }

    EnsureVisualRowCache();
    const std::size_t column = position - LineStart(line);
    const std::size_t baseRow = line < visualRowStarts_.size() ? visualRowStarts_[line] : totalVisualRows_ - 1;
    return baseRow + (column / visualRowCacheColumns_);
}

EditorView::VisualRow EditorView::VisualRowFromIndex(std::size_t visualRow) const {
    const std::size_t lineCount = IndexedLineCount();
    if (lineCount == 0) {
        return {};
    }

    if (!wordWrap_) {
        const std::size_t line = std::min(visualRow, lineCount - 1);
        return {line, 0, LineLength(line)};
    }

    EnsureVisualRowCache();
    const std::size_t clampedRow = std::min(visualRow, totalVisualRows_ - 1);
    auto lineIt = std::upper_bound(visualRowStarts_.begin(), visualRowStarts_.end(), clampedRow);
    std::size_t line = 0;
    if (lineIt != visualRowStarts_.begin()) {
        line = static_cast<std::size_t>((lineIt - visualRowStarts_.begin()) - 1);
    }
    line = std::min(line, lineCount - 1);

    const std::size_t rowInLine = clampedRow - visualRowStarts_[line];
    const std::size_t length = LineLength(line);
    const std::size_t columnStart = std::min(rowInLine * visualRowCacheColumns_, length);
    return {line, columnStart, std::min(visualRowCacheColumns_, length - columnStart)};
}

std::size_t EditorView::MaxFirstVisualRow(std::size_t visibleRows) const {
    const std::size_t totalRows = TotalVisualRows();
    return totalRows > visibleRows ? totalRows - visibleRows : 0;
}

std::size_t EditorView::CaretDisplayColumn() const {
    const std::size_t column = Column();
    return wordWrap_ ? column % WrapColumnCount() : column;
}

std::size_t EditorView::LineNumberDigitCount() const noexcept {
    std::size_t value = std::max<std::size_t>(1, IndexedLineCount());
    std::size_t digits = 1;
    while (value >= 10) {
        value /= 10;
        ++digits;
    }
    return digits;
}

float EditorView::LineNumberGutterWidthDips() const {
    if (!showLineNumbers_) {
        return 0.0f;
    }

    const float digitWidth = std::max(1.0f, charWidth_);
    const float digits = static_cast<float>(LineNumberDigitCount());
    return std::ceil((digits * digitWidth) +
                     static_cast<float>((kLineNumberHorizontalPadding * 2) + kLineNumberTextGap));
}

float EditorView::TextLeftDips() const {
    return static_cast<float>(kLeftPadding) + LineNumberGutterWidthDips();
}

float EditorView::TextViewportWidthDips() const {
    return std::max(1.0f, ClientWidthDips() - TextLeftDips() - static_cast<float>(kLeftPadding));
}

bool EditorView::CursorIsInLineNumberGutter() const {
    if (!showLineNumbers_) {
        return false;
    }

    POINT point{};
    if (!GetCursorPos(&point)) {
        return false;
    }

    ScreenToClient(hwnd_, &point);
    return PixelsToDips(point.x) < LineNumberGutterWidthDips();
}

std::wstring EditorView::SelectedText() const {
    if (!HasSelection() || !HasDocument()) {
        return {};
    }
    return DocumentTextRange(SelectionStart(), SelectionEnd() - SelectionStart());
}

bool EditorView::SetClipboardText(const std::wstring& text) const {
    if (!OpenClipboard(hwnd_)) {
        return false;
    }

    EmptyClipboard();
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr) {
        CloseClipboard();
        return false;
    }

    void* locked = GlobalLock(memory);
    if (locked == nullptr) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }

    memcpy(locked, text.c_str(), bytes);
    GlobalUnlock(memory);
    SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    return true;
}

std::wstring EditorView::ClipboardText() const {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(hwnd_)) {
        return {};
    }

    HGLOBAL memory = GetClipboardData(CF_UNICODETEXT);
    if (memory == nullptr) {
        CloseClipboard();
        return {};
    }

    const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(memory));
    std::wstring result = text != nullptr ? std::wstring(text) : std::wstring();
    if (text != nullptr) {
        GlobalUnlock(memory);
    }
    CloseClipboard();
    return result;
}

float EditorView::DpiScale() const noexcept {
    return static_cast<float>(dpi_) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
}

float EditorView::PixelsToDips(int pixels) const noexcept {
    return static_cast<float>(pixels) / DpiScale();
}

float EditorView::ClientWidthDips() const {
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    return PixelsToDips(static_cast<int>(rect.right - rect.left));
}

float EditorView::ClientHeightDips() const {
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    return PixelsToDips(static_cast<int>(rect.bottom - rect.top));
}

void EditorView::Undo() {
    if (readOnly_ || !CanUndo() || document_ == nullptr) {
        return;
    }

    EditAction action = std::move(undoStack_.back());
    undoStack_.pop_back();
    document_->Replace(action.position, action.inserted.size(), action.erased);
    caret_ = action.caretBefore;
    anchor_ = caret_;
    UpdateLineIndexForEdit(action.position, action.inserted, action.erased);
    ScrollToCaret();
    ResetCaretBlink();
    redoStack_.push_back(action);
    NotifyChanged();
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyCursorChanged();
}

void EditorView::Redo() {
    if (readOnly_ || !CanRedo() || document_ == nullptr) {
        return;
    }

    EditAction action = std::move(redoStack_.back());
    redoStack_.pop_back();
    document_->Replace(action.position, action.erased.size(), action.inserted);
    caret_ = action.caretAfter;
    anchor_ = caret_;
    UpdateLineIndexForEdit(action.position, action.erased, action.inserted);
    ScrollToCaret();
    ResetCaretBlink();
    undoStack_.push_back(action);
    NotifyChanged();
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyCursorChanged();
}

void EditorView::Cut() {
    if (readOnly_ || !HasSelection()) {
        return;
    }
    Copy();
    ApplyEdit(SelectionStart(), SelectionEnd() - SelectionStart(), L"", true);
}

void EditorView::Copy() const {
    const std::wstring text = SelectedText();
    if (!text.empty()) {
        SetClipboardText(text);
    }
}

void EditorView::Paste() {
    if (readOnly_) {
        return;
    }

    InsertText(ClipboardText());
}

void EditorView::Delete() {
    if (!readOnly_) {
        DeleteSelectionOrRange(false);
    }
}

void EditorView::InsertAtCaret(std::wstring text) {
    InsertText(std::move(text));
}

void EditorView::GoToLine(std::size_t line) {
    const std::size_t lineCount = IndexedLineCount();
    if (lineCount == 0) {
        SetCaret(0, false);
        return;
    }

    const std::size_t targetLine = std::min(line, lineCount - 1);
    SetCaret(PositionFromLineColumn(targetLine, 0), false);
}

void EditorView::SelectAll() {
    if (!HasDocument()) {
        return;
    }

    anchor_ = 0;
    caret_ = DocumentLength();
    ScrollToCaret();
    ResetCaretBlink();
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyCursorChanged();
}

} // namespace NativePad
