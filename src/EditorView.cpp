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

#include "LargeTextDocument.h"
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
constexpr UINT_PTR kDragScrollTimerId = 2;
constexpr UINT kDragScrollIntervalMs = 50;
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
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> syntaxKeywordBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> syntaxStringBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> syntaxNumberBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> syntaxCommentBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> syntaxPunctuationBrush;
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
    syntaxTheme_ = {
        RGB(86, 156, 214),
        RGB(206, 145, 120),
        RGB(181, 206, 168),
        RGB(106, 153, 85),
        RGB(180, 180, 180),
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
    largeDocument_ = nullptr;
    ResetView();
}

void EditorView::SetMappedDocument(MappedTextDocument* document) {
    mappedDocument_ = document;
    document_ = nullptr;
    largeDocument_ = nullptr;
    ResetView();
}

void EditorView::SetLargeDocument(LargeTextDocument* document) {
    largeDocument_ = document;
    document_ = nullptr;
    mappedDocument_ = nullptr;
    ResetView();
}

void EditorView::SetDocument(DocumentBuffer* document, EditorViewState state) {
    document_ = document;
    mappedDocument_ = nullptr;
    largeDocument_ = nullptr;
    RestoreState(std::move(state));
}

void EditorView::SetMappedDocument(MappedTextDocument* document, EditorViewState state) {
    mappedDocument_ = document;
    document_ = nullptr;
    largeDocument_ = nullptr;
    RestoreState(std::move(state));
}

void EditorView::SetLargeDocument(LargeTextDocument* document, EditorViewState state) {
    largeDocument_ = document;
    document_ = nullptr;
    mappedDocument_ = nullptr;
    RestoreState(std::move(state));
}

EditorViewState EditorView::TakeState() noexcept {
    state_.initialized_ = true;
    EditorViewState result = std::move(state_);
    state_ = EditorViewState{};
    document_ = nullptr;
    mappedDocument_ = nullptr;
    largeDocument_ = nullptr;
    return result;
}

void EditorView::RestoreState(EditorViewState state) {
    if (!state.initialized_) {
        ResetView();
        return;
    }

    state_ = std::move(state);
    const std::size_t length = DocumentLength();
    state_.caret_ = std::min(state_.caret_, length);
    state_.anchor_ = std::min(state_.anchor_, length);
    lastDoubleClickTick_ = 0;
    InvalidateVisualRowCache();
    UpdateScrollbars();
    ResetCaretBlink();
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyCursorChanged();
}

void EditorView::ResetView() {
    state_.caret_ = 0;
    state_.anchor_ = 0;
    state_.firstLine_ = 0;
    state_.firstVisualRow_ = 0;
    state_.horizontalColumn_ = 0;
    state_.desiredColumn_ = 0;
    lastDoubleClickTick_ = 0;
    state_.undoStack_.clear();
    state_.redoStack_.clear();
    state_.initialized_ = true;
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
    state_.caret_ = std::min(state_.caret_, length);
    state_.anchor_ = std::min(state_.anchor_, length);
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

void EditorView::SetSyntaxTheme(EditorSyntaxTheme theme) {
    syntaxTheme_ = theme;
    if (impl_->syntaxKeywordBrush) {
        impl_->syntaxKeywordBrush->SetColor(ToD2DColor(syntaxTheme_.keyword));
    }
    if (impl_->syntaxStringBrush) {
        impl_->syntaxStringBrush->SetColor(ToD2DColor(syntaxTheme_.string));
    }
    if (impl_->syntaxNumberBrush) {
        impl_->syntaxNumberBrush->SetColor(ToD2DColor(syntaxTheme_.number));
    }
    if (impl_->syntaxCommentBrush) {
        impl_->syntaxCommentBrush->SetColor(ToD2DColor(syntaxTheme_.comment));
    }
    if (impl_->syntaxPunctuationBrush) {
        impl_->syntaxPunctuationBrush->SetColor(ToD2DColor(syntaxTheme_.punctuation));
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void EditorView::SetSyntaxLanguage(SyntaxLanguage language) {
    if (syntaxLanguage_ == language) {
        return;
    }

    syntaxLanguage_ = language;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

SyntaxLanguage EditorView::SyntaxLanguageForDocument() const noexcept {
    return syntaxLanguage_;
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

void EditorView::SetZoomPercent(int percent) {
    // Zoom scales the rendered text only; font_ keeps the user's chosen size so
    // the Font dialog and persisted preferences never see a zoomed value.
    percent = std::clamp(percent, 10, 500);
    if (percent == zoomPercent_) {
        return;
    }

    zoomPercent_ = percent;
    RecreateTextFormat();
    InvalidateVisualRowCache();
    ScrollToCaret();
    UpdateScrollbars();
    InvalidateRect(hwnd_, nullptr, TRUE);
    if (parent_ != nullptr) {
        SendMessageW(parent_, WM_EDITOR_ZOOM_CHANGED, 0, 0);
    }
}

int EditorView::ZoomPercent() const noexcept {
    return zoomPercent_;
}

void EditorView::SetWordWrap(bool enabled) {
    if (wordWrap_ == enabled) {
        return;
    }

    wordWrap_ = enabled;
    InvalidateVisualRowCache();
    state_.horizontalColumn_ = 0;
    state_.firstVisualRow_ = VisualRowIndexForPosition(state_.caret_);
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
    state_.anchor_ = std::min(start, documentLength);
    state_.caret_ = std::min(state_.anchor_ + length, documentLength);
    state_.desiredColumn_ = Column();
    ScrollToCaret();
    ResetCaretBlink();
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyCursorChanged();
}

bool EditorView::CanUndo() const noexcept {
    return !readOnly_ && !state_.undoStack_.empty();
}

bool EditorView::CanRedo() const noexcept {
    return !readOnly_ && !state_.redoStack_.empty();
}

bool EditorView::IsReadOnly() const noexcept {
    return readOnly_;
}

bool EditorView::HasSelection() const noexcept {
    return state_.caret_ != state_.anchor_;
}

std::size_t EditorView::CaretPosition() const noexcept {
    return state_.caret_;
}

std::size_t EditorView::SelectionStart() const noexcept {
    return std::min(state_.caret_, state_.anchor_);
}

std::size_t EditorView::SelectionEnd() const noexcept {
    return std::max(state_.caret_, state_.anchor_);
}

std::size_t EditorView::Line() const {
    return LineFromPosition(state_.caret_);
}

std::size_t EditorView::Column() const {
    const std::size_t line = Line();
    return state_.caret_ - LineStart(line);
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
        if (wParam == kDragScrollTimerId) {
            OnDragScrollTimer();
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
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != hwnd_) {
            KillTimer(hwnd_, kDragScrollTimerId);
            dragging_ = false;
        }
        return 0;
    case WM_CANCELMODE:
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

        std::size_t next = wordWrap_ ? state_.firstVisualRow_ : state_.firstLine_;
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

        const std::size_t visibleRows = VisibleRowCount();
        if (wordWrap_) {
            state_.firstVisualRow_ = std::min(next, MaxFirstVisualRow(visibleRows));
        } else {
            state_.firstLine_ = std::min(next, MaxFirstLogicalLine(visibleRows));
        }
        UpdateScrollPositions();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    case WM_HSCROLL: {
        std::size_t next = state_.horizontalColumn_;
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

        state_.horizontalColumn_ = std::min(next, MaxHorizontalScrollColumn());
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
    const std::size_t firstRow = wordWrap_ ? state_.firstVisualRow_ : state_.firstLine_;
    const std::size_t totalRows = wordWrap_ ? TotalVisualRows() : IndexedLineCount();
    const std::size_t lastRow = std::min(totalRows, firstRow + visibleRows);
    const std::size_t selectionStart = SelectionStart();
    const std::size_t selectionEnd = SelectionEnd();
    const float xOrigin = wordWrap_ ? textLeft : textLeft - (static_cast<float>(state_.horizontalColumn_) * charWidth_);
    const std::size_t wrapColumns = WrapColumnCount();
    bool haveHighlightedLine = false;
    std::size_t highlightedLine = 0;
    std::wstring highlightedLineText;
    std::vector<SyntaxSpan> highlightedSpans;

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
            if (syntaxLanguage_ == SyntaxLanguage::PlainText) {
                impl_->target->DrawTextW(
                    text.c_str(),
                    static_cast<UINT32>(text.size()),
                    impl_->textFormat.Get(),
                    D2D1::RectF(xOrigin, y, width + std::abs(xOrigin) + 2048.0f, y + lineHeight_),
                    impl_->textBrush.Get(),
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
            } else {
                Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
                const float layoutWidth = width + std::abs(xOrigin) + 2048.0f;
                if (SUCCEEDED(impl_->dwriteFactory->CreateTextLayout(
                        text.c_str(),
                        static_cast<UINT32>(text.size()),
                        impl_->textFormat.Get(),
                        layoutWidth,
                        lineHeight_,
                        layout.GetAddressOf()))) {
                    if (!haveHighlightedLine || highlightedLine != visual.line) {
                        highlightedLine = visual.line;
                        highlightedLineText = DocumentTextRange(lineStart, lineEnd - lineStart);
                        highlightedSpans = syntaxHighlighter_.HighlightLine(syntaxLanguage_, highlightedLineText);
                        haveHighlightedLine = true;
                    }
                    const std::size_t segmentOffset = segmentStart - lineStart;
                    for (const SyntaxSpan& span : highlightedSpans) {
                        const std::size_t spanStart = std::max(span.start, segmentOffset);
                        const std::size_t spanEnd = std::min(span.start + span.length, segmentOffset + text.size());
                        if (spanStart >= spanEnd || spanStart - segmentOffset > UINT32_MAX || spanEnd - spanStart > UINT32_MAX) {
                            continue;
                        }

                        ID2D1SolidColorBrush* brush = SyntaxBrush(span.color);
                        if (brush == nullptr) {
                            continue;
                        }

                        const DWRITE_TEXT_RANGE range{
                            static_cast<UINT32>(spanStart - segmentOffset),
                            static_cast<UINT32>(spanEnd - spanStart)};
                        layout->SetDrawingEffect(brush, range);
                    }
                    impl_->target->DrawTextLayout(
                        D2D1::Point2F(xOrigin, y),
                        layout.Get(),
                        impl_->textBrush.Get(),
                        D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
            }
        }
    }

    if (hasTextClip && GetFocus() == hwnd_ && caretVisible_) {
        const std::size_t caretRow = wordWrap_ ? VisualRowIndexForPosition(state_.caret_) : LineFromPosition(state_.caret_);
        if (caretRow >= firstRow && caretRow < lastRow) {
            const VisualRow visual = wordWrap_ ? VisualRowFromIndex(caretRow) : VisualRow{caretRow, 0, LineLength(caretRow)};
            const std::size_t segmentStart = LineStart(visual.line) + visual.columnStart;
            const float x = xOrigin + (static_cast<float>(state_.caret_ - segmentStart) * charWidth_);
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
    impl_->syntaxKeywordBrush.Reset();
    impl_->syntaxStringBrush.Reset();
    impl_->syntaxNumberBrush.Reset();
    impl_->syntaxCommentBrush.Reset();
    impl_->syntaxPunctuationBrush.Reset();
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
        impl_->target->CreateSolidColorBrush(ToD2DColor(syntaxTheme_.keyword), impl_->syntaxKeywordBrush.GetAddressOf());
        impl_->target->CreateSolidColorBrush(ToD2DColor(syntaxTheme_.string), impl_->syntaxStringBrush.GetAddressOf());
        impl_->target->CreateSolidColorBrush(ToD2DColor(syntaxTheme_.number), impl_->syntaxNumberBrush.GetAddressOf());
        impl_->target->CreateSolidColorBrush(ToD2DColor(syntaxTheme_.comment), impl_->syntaxCommentBrush.GetAddressOf());
        impl_->target->CreateSolidColorBrush(ToD2DColor(syntaxTheme_.punctuation), impl_->syntaxPunctuationBrush.GetAddressOf());
    }

    return impl_->textFormat != nullptr && impl_->target != nullptr && impl_->textBrush != nullptr;
}

ID2D1SolidColorBrush* EditorView::SyntaxBrush(SyntaxColor color) const noexcept {
    switch (color) {
    case SyntaxColor::Keyword:
        return impl_->syntaxKeywordBrush.Get();
    case SyntaxColor::String:
        return impl_->syntaxStringBrush.Get();
    case SyntaxColor::Number:
        return impl_->syntaxNumberBrush.Get();
    case SyntaxColor::Comment:
        return impl_->syntaxCommentBrush.Get();
    case SyntaxColor::Punctuation:
        return impl_->syntaxPunctuationBrush.Get();
    case SyntaxColor::PlainText:
    default:
        return nullptr;
    }
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
    const float renderedSize = std::max(1.0f, font_.sizeDips * static_cast<float>(zoomPercent_) / 100.0f);
    if (SUCCEEDED(impl_->dwriteFactory->CreateTextFormat(
            font_.family.c_str(),
            nullptr,
            static_cast<DWRITE_FONT_WEIGHT>(font_.weight),
            font_.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            renderedSize,
            L"",
            impl_->textFormat.GetAddressOf()))) {
        impl_->textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if (SUCCEEDED(impl_->dwriteFactory->CreateTextFormat(
                font_.family.c_str(),
                nullptr,
                static_cast<DWRITE_FONT_WEIGHT>(font_.weight),
                font_.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                renderedSize,
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
    // Mapped and large documents carry their own line index. Only the editable
    // UTF-16 buffer uses this local index, updated incrementally on each edit.
    state_.lineIndex_.Reset(document_ != nullptr ? document_->Text() : L"");
    InvalidateVisualRowCache();
}

void EditorView::UpdateLineIndexForEdit(std::size_t position, std::wstring_view erased, std::wstring_view inserted) {
    state_.lineIndex_.ApplyEdit(position, erased, inserted);
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

    const std::size_t visibleRowCount = VisibleRowCount();
    const int visibleLines = static_cast<int>(std::min<std::size_t>(visibleRowCount, static_cast<std::size_t>(INT_MAX)));
    const std::size_t totalRows = wordWrap_ ? TotalVisualRows() : IndexedLineCount();
    const int totalLines = std::max(1, static_cast<int>(std::min<std::size_t>(totalRows, static_cast<std::size_t>(INT_MAX))));

    if (wordWrap_) {
        state_.firstVisualRow_ = std::min(state_.firstVisualRow_, MaxFirstVisualRow(visibleRowCount));
    } else {
        state_.firstLine_ = std::min(state_.firstLine_, MaxFirstLogicalLine(visibleRowCount));
    }

    SCROLLINFO vertical{};
    vertical.cbSize = sizeof(vertical);
    vertical.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    vertical.nMin = 0;
    vertical.nMax = totalLines - 1;
    vertical.nPage = static_cast<UINT>(visibleLines);
    vertical.nPos = static_cast<int>(std::min(wordWrap_ ? state_.firstVisualRow_ : state_.firstLine_, totalRows - 1));
    SetScrollInfo(hwnd_, SB_VERT, &vertical, TRUE);

    if (wordWrap_) {
        ShowScrollBar(hwnd_, SB_HORZ, FALSE);
        return;
    }

    const std::size_t maxLineLength = IndexedMaxLineLength();
    const int visibleColumns = std::max(1, static_cast<int>(TextViewportWidthDips() / std::max(1.0f, charWidth_)));
    const std::size_t maxScrollPosition = MaxHorizontalScrollColumn();
    if (maxScrollPosition == 0) {
        state_.horizontalColumn_ = 0;
        ShowScrollBar(hwnd_, SB_HORZ, FALSE);
        return;
    }

    ShowScrollBar(hwnd_, SB_HORZ, TRUE);
    state_.horizontalColumn_ = std::min(state_.horizontalColumn_, maxScrollPosition);

    SCROLLINFO horizontal{};
    horizontal.cbSize = sizeof(horizontal);
    horizontal.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    horizontal.nMin = 0;
    horizontal.nMax = static_cast<int>(std::min<std::size_t>(maxLineLength, INT_MAX));
    horizontal.nPage = static_cast<UINT>(visibleColumns);
    horizontal.nPos = static_cast<int>(state_.horizontalColumn_);
    SetScrollInfo(hwnd_, SB_HORZ, &horizontal, TRUE);
}

void EditorView::UpdateScrollPositions() {
    if (hwnd_ == nullptr) {
        return;
    }

    const std::size_t verticalPosition = wordWrap_ ? state_.firstVisualRow_ : state_.firstLine_;
    SetScrollPos(hwnd_, SB_VERT, static_cast<int>(std::min<std::size_t>(verticalPosition, static_cast<std::size_t>(INT_MAX))), TRUE);
    if (!wordWrap_) {
        SetScrollPos(hwnd_, SB_HORZ, static_cast<int>(std::min<std::size_t>(state_.horizontalColumn_, static_cast<std::size_t>(INT_MAX))), TRUE);
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
    const std::size_t caretLine = LineFromPosition(state_.caret_);
    const std::size_t visibleLines = VisibleRowCount();

    if (wordWrap_) {
        const std::size_t caretRow = VisualRowIndexForPosition(state_.caret_);
        if (caretRow < state_.firstVisualRow_) {
            state_.firstVisualRow_ = caretRow;
        } else if (caretRow >= state_.firstVisualRow_ + visibleLines) {
            state_.firstVisualRow_ = caretRow - visibleLines + 1;
        }
        state_.firstVisualRow_ = std::min(state_.firstVisualRow_, MaxFirstVisualRow(visibleLines));
        UpdateScrollbars();
        return;
    }

    if (caretLine < state_.firstLine_) {
        state_.firstLine_ = caretLine;
    } else if (caretLine >= state_.firstLine_ + visibleLines) {
        state_.firstLine_ = caretLine - visibleLines + 1;
    }

    const std::size_t column = state_.caret_ - LineStart(caretLine);
    const std::size_t visibleColumns = std::max<std::size_t>(1, static_cast<std::size_t>(TextViewportWidthDips() / std::max(1.0f, charWidth_)));
    if (column < state_.horizontalColumn_) {
        state_.horizontalColumn_ = column;
    } else if (column >= state_.horizontalColumn_ + visibleColumns) {
        state_.horizontalColumn_ = column - visibleColumns + 1;
    }

    UpdateScrollbars();
}

void EditorView::SetCaret(std::size_t position, bool extendSelection) {
    const std::size_t length = DocumentLength();
    state_.caret_ = std::min(position, length);
    if (!extendSelection) {
        state_.anchor_ = state_.caret_;
    }
    state_.desiredColumn_ = CaretDisplayColumn();
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

    if (delta < 0 && state_.caret_ > 0) {
        SetCaret(state_.caret_ - 1, extendSelection);
    } else if (delta > 0 && HasDocument() && state_.caret_ < DocumentLength()) {
        SetCaret(state_.caret_ + 1, extendSelection);
    }
}

void EditorView::MoveCaretVertical(int delta, bool extendSelection) {
    if (wordWrap_) {
        const std::size_t currentRow = VisualRowIndexForPosition(state_.caret_);
        const std::size_t totalRows = TotalVisualRows();
        const std::size_t nextRow = delta < 0
                                        ? (currentRow > 0 ? currentRow - 1 : 0)
                                        : std::min(currentRow + 1, totalRows - 1);
        const VisualRow visual = VisualRowFromIndex(nextRow);
        const std::size_t column = std::min(state_.desiredColumn_, visual.length);
        state_.caret_ = LineStart(visual.line) + visual.columnStart + column;
        if (!extendSelection) {
            state_.anchor_ = state_.caret_;
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
    const std::size_t column = std::min(state_.desiredColumn_, LineLength(nextLine));
    const std::size_t next = PositionFromLineColumn(nextLine, column);
    state_.caret_ = next;
    if (!extendSelection) {
        state_.anchor_ = state_.caret_;
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

    if (!IsEditable()) {
        return;
    }

    const std::size_t documentLength = DocumentLength();
    if (backspace) {
        if (state_.caret_ > 0) {
            std::size_t position = state_.caret_ - 1;
            std::size_t length = 1;
            if (DocumentCharAt(position) == L'\n' && position > 0 && DocumentCharAt(position - 1) == L'\r') {
                --position;
                length = 2;
            }
            ApplyEdit(position, length, L"", true);
        }
    } else if (state_.caret_ < documentLength) {
        std::size_t length = 1;
        if (DocumentCharAt(state_.caret_) == L'\r' && state_.caret_ + 1 < documentLength && DocumentCharAt(state_.caret_ + 1) == L'\n') {
            length = 2;
        }
        ApplyEdit(state_.caret_, length, L"", true);
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
    // placement, and notifications stay consistent across both editable backends.
    if (readOnly_ || !IsEditable()) {
        return;
    }

    const std::size_t caretBefore = state_.caret_;
    BackendEdit edit = BackendReplace(position, eraseLength, insertText);
    state_.caret_ = edit.position + edit.insertedUnits;
    state_.anchor_ = state_.caret_;
    state_.desiredColumn_ = CaretDisplayColumn();
    ScrollToCaret();
    UpdateScrollbars();
    ResetCaretBlink();

    if (recordUndo) {
        EditAction action;
        action.position = edit.position;
        action.erased = std::move(edit.erased);
        action.inserted = std::move(insertText);
        action.erasedUnits = edit.erasedUnits;
        action.insertedUnits = edit.insertedUnits;
        action.caretBefore = caretBefore;
        action.caretAfter = state_.caret_;
        PushUndo(std::move(action));
        state_.redoStack_.clear();
        NotifyChanged();
    }

    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyCursorChanged();
}

EditorView::BackendEdit EditorView::BackendReplace(
    std::size_t position, std::size_t eraseLength, const std::wstring& insertText) {
    // Low-level storage mutation shared by ApplyEdit, Undo, and Redo. It does not
    // touch the undo stacks or send change notifications.
    BackendEdit edit;
    if (largeDocument_ != nullptr) {
        LargeTextDocument::EditResult result = largeDocument_->Replace(position, eraseLength, insertText);
        edit.position = result.position;
        edit.erased = std::move(result.erased);
        edit.erasedUnits = result.erasedUnits;
        edit.insertedUnits = result.insertedUnits;
        return edit;
    }

    // The editable UTF-16 buffer measures everything in code units, so document
    // units equal the string sizes.
    edit.position = position;
    edit.erased = document_->TextRange(position, eraseLength);
    document_->Replace(position, eraseLength, insertText);
    UpdateLineIndexForEdit(position, edit.erased, insertText);
    edit.erasedUnits = edit.erased.size();
    edit.insertedUnits = insertText.size();
    return edit;
}

void EditorView::PushUndo(EditAction action) {
    state_.undoStack_.push_back(std::move(action));
    if (state_.undoStack_.size() > kMaxUndoActions) {
        state_.undoStack_.erase(state_.undoStack_.begin());
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
    dragPoint_ = {x, y};
    RECT client{};
    GetClientRect(hwnd_, &client);
    const bool outside = x < client.left || x >= client.right || y < client.top || y >= client.bottom;
    if (outside) {
        SetTimer(hwnd_, kDragScrollTimerId, kDragScrollIntervalMs, nullptr);
    } else {
        KillTimer(hwnd_, kDragScrollTimerId);
    }
    UpdateDragSelection(x, y);
}

void EditorView::OnMouseWheel(short delta) {
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        SetZoomPercent(zoomPercent_ + (delta > 0 ? 10 : -10));
        return;
    }

    const int lines = std::max(1, std::abs(delta) / WHEEL_DELTA * 3);
    if (wordWrap_) {
        const std::size_t visibleLines = VisibleRowCount();
        if (delta > 0) {
            state_.firstVisualRow_ = state_.firstVisualRow_ > static_cast<std::size_t>(lines) ? state_.firstVisualRow_ - static_cast<std::size_t>(lines) : 0;
        } else {
            state_.firstVisualRow_ = std::min(state_.firstVisualRow_ + static_cast<std::size_t>(lines), MaxFirstVisualRow(visibleLines));
        }
    } else {
        const std::size_t maximum = MaxFirstLogicalLine(VisibleRowCount());
        if (delta > 0) {
            state_.firstLine_ = state_.firstLine_ > static_cast<std::size_t>(lines) ? state_.firstLine_ - static_cast<std::size_t>(lines) : 0;
        } else {
            state_.firstLine_ = std::min(state_.firstLine_ + static_cast<std::size_t>(lines), maximum);
        }
    }
    UpdateScrollPositions();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void EditorView::OnDragScrollTimer() {
    if (!dragging_ || GetCapture() != hwnd_) {
        KillTimer(hwnd_, kDragScrollTimerId);
        return;
    }
    UpdateDragSelection(dragPoint_.x, dragPoint_.y);
}

void EditorView::UpdateDragSelection(int x, int y) {
    RECT client{};
    if (!GetClientRect(hwnd_, &client) || client.right <= client.left || client.bottom <= client.top) {
        return;
    }

    const int rowPixels = std::max(1, static_cast<int>(std::lround(lineHeight_ * DpiScale())));
    const int columnPixels = std::max(1, static_cast<int>(std::lround(charWidth_ * DpiScale())));
    auto scrollAmount = [](int distance, int unit) {
        return static_cast<std::size_t>(std::clamp(1 + distance / std::max(1, unit), 1, 8));
    };

    const std::size_t visibleRows = VisibleRowCount();
    std::size_t& verticalPosition = wordWrap_ ? state_.firstVisualRow_ : state_.firstLine_;
    const std::size_t maximumVertical =
        wordWrap_ ? MaxFirstVisualRow(visibleRows) : MaxFirstLogicalLine(visibleRows);
    if (y < client.top) {
        const std::size_t amount = scrollAmount(client.top - y, rowPixels);
        verticalPosition = verticalPosition > amount ? verticalPosition - amount : 0;
    } else if (y >= client.bottom) {
        const std::size_t amount = scrollAmount(y - client.bottom + 1, rowPixels);
        verticalPosition = std::min(verticalPosition + amount, maximumVertical);
    }

    if (!wordWrap_) {
        const std::size_t maximumHorizontal = MaxHorizontalScrollColumn();
        if (x < client.left) {
            const std::size_t amount = scrollAmount(client.left - x, columnPixels);
            state_.horizontalColumn_ = state_.horizontalColumn_ > amount ? state_.horizontalColumn_ - amount : 0;
        } else if (x >= client.right) {
            const std::size_t amount = scrollAmount(x - client.right + 1, columnPixels);
            state_.horizontalColumn_ = std::min(state_.horizontalColumn_ + amount, maximumHorizontal);
        }
    }

    UpdateScrollbars();
    UpdateScrollPositions();

    const int clientRight = static_cast<int>(client.right);
    const int clientBottom = static_cast<int>(client.bottom);
    const int textLeft = static_cast<int>(std::ceil(TextLeftDips() * DpiScale()));
    const int hitX = std::clamp(x, std::min(textLeft, clientRight - 1), clientRight - 1);
    const int topPadding = static_cast<int>(std::lround(static_cast<float>(kTopPadding) * DpiScale()));
    const int hitY = std::clamp(y, std::min(topPadding, clientBottom - 1), clientBottom - 1);
    SetCaret(HitTest(hitX, hitY), true);
}

void EditorView::CaptureMouseDrag() {
    if (!dragging_) {
        dragPoint_ = {};
        SetCapture(hwnd_);
        dragging_ = true;
    }
}

void EditorView::ReleaseMouseDrag() {
    KillTimer(hwnd_, kDragScrollTimerId);
    if (dragging_) {
        dragging_ = false;
        if (GetCapture() == hwnd_) {
            ReleaseCapture();
        }
    }
}

bool EditorView::HasDocument() const noexcept {
    return document_ != nullptr || mappedDocument_ != nullptr || largeDocument_ != nullptr;
}

bool EditorView::IsEditable() const noexcept {
    return document_ != nullptr || largeDocument_ != nullptr;
}

std::size_t EditorView::DocumentLength() const noexcept {
    if (mappedDocument_ != nullptr) {
        return mappedDocument_->Length();
    }
    if (largeDocument_ != nullptr) {
        return largeDocument_->Length();
    }
    return document_ != nullptr ? document_->Length() : 0;
}

wchar_t EditorView::DocumentCharAt(std::size_t position) const {
    if (mappedDocument_ != nullptr) {
        return mappedDocument_->CharAt(position);
    }
    if (largeDocument_ != nullptr) {
        return largeDocument_->CharAt(position);
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
    if (largeDocument_ != nullptr) {
        return largeDocument_->TextRange(position, length);
    }
    return document_ != nullptr ? document_->TextRange(position, length) : std::wstring();
}

std::size_t EditorView::IndexedLineCount() const noexcept {
    if (mappedDocument_ != nullptr) {
        return mappedDocument_->LineCount();
    }
    if (largeDocument_ != nullptr) {
        return largeDocument_->LineCount();
    }
    return state_.lineIndex_.LineCount();
}

std::size_t EditorView::IndexedMaxLineLength() const noexcept {
    if (mappedDocument_ != nullptr) {
        return mappedDocument_->MaxLineLength();
    }
    if (largeDocument_ != nullptr) {
        return largeDocument_->MaxLineLength();
    }
    return state_.lineIndex_.MaxLineLength();
}

std::size_t EditorView::HitTest(int x, int y) const {
    const float xDip = PixelsToDips(x);
    const float yDip = PixelsToDips(y);
    const int lineOffset = std::max(0, static_cast<int>((yDip - static_cast<float>(kTopPadding)) / lineHeight_));
    if (wordWrap_) {
        const VisualRow visual = VisualRowFromIndex(state_.firstVisualRow_ + static_cast<std::size_t>(lineOffset));
        const int rawColumn = static_cast<int>(std::round((xDip - TextLeftDips()) / charWidth_));
        const std::size_t column = visual.columnStart + static_cast<std::size_t>(std::max(0, rawColumn));
        return PositionFromLineColumn(visual.line, std::min(column, visual.columnStart + visual.length));
    }

    const std::size_t line = std::min(state_.firstLine_ + static_cast<std::size_t>(lineOffset), IndexedLineCount() - 1);
    const int rawColumn = static_cast<int>(std::round((xDip - TextLeftDips()) / charWidth_));
    const std::size_t column = static_cast<std::size_t>(std::max(0, rawColumn)) + state_.horizontalColumn_;
    return PositionFromLineColumn(line, std::min(column, LineLength(line)));
}

std::size_t EditorView::LineStart(std::size_t line) const {
    if (mappedDocument_ != nullptr) {
        return mappedDocument_->LineStart(line);
    }
    if (largeDocument_ != nullptr) {
        return largeDocument_->LineStart(line);
    }
    return state_.lineIndex_.LineStart(line);
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
    if (mappedDocument_ != nullptr) {
        return mappedDocument_->LineFromPosition(position);
    }
    if (largeDocument_ != nullptr) {
        return largeDocument_->LineFromPosition(position);
    }
    return state_.lineIndex_.LineFromPosition(position);
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

std::size_t EditorView::VisibleRowCount() const {
    const float availableHeight = std::max(0.0f, ClientHeightDips() - static_cast<float>(kTopPadding));
    return std::max<std::size_t>(
        1,
        static_cast<std::size_t>(availableHeight / std::max(1.0f, lineHeight_)));
}

std::size_t EditorView::MaxFirstLogicalLine(std::size_t visibleRows) const {
    const std::size_t lineCount = IndexedLineCount();
    return lineCount > visibleRows ? lineCount - visibleRows : 0;
}

std::size_t EditorView::MaxHorizontalScrollColumn() const {
    if (wordWrap_) {
        return 0;
    }
    const std::size_t maxLineLength = IndexedMaxLineLength();
    const std::size_t visibleColumns = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(TextViewportWidthDips() / std::max(1.0f, charWidth_)));
    return maxLineLength > visibleColumns ? maxLineLength - visibleColumns + 1 : 0;
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
    if (readOnly_ || !CanUndo() || !IsEditable()) {
        return;
    }

    EditAction action = std::move(state_.undoStack_.back());
    state_.undoStack_.pop_back();
    // Reverse the edit: remove the inserted span and restore the erased text.
    BackendReplace(action.position, action.insertedUnits, action.erased);
    state_.caret_ = action.caretBefore;
    state_.anchor_ = state_.caret_;
    ScrollToCaret();
    ResetCaretBlink();
    state_.redoStack_.push_back(std::move(action));
    NotifyChanged();
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyCursorChanged();
}

void EditorView::Redo() {
    if (readOnly_ || !CanRedo() || !IsEditable()) {
        return;
    }

    EditAction action = std::move(state_.redoStack_.back());
    state_.redoStack_.pop_back();
    // Reapply the edit: remove the erased span and insert the recorded text.
    BackendReplace(action.position, action.erasedUnits, action.inserted);
    state_.caret_ = action.caretAfter;
    state_.anchor_ = state_.caret_;
    ScrollToCaret();
    ResetCaretBlink();
    state_.undoStack_.push_back(std::move(action));
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

std::wstring EditorView::LineBreakForLine(std::size_t line) const {
    // Reuse the break the document already uses next to this line so line
    // operations never introduce a foreign line-ending style.
    const std::size_t lineCount = IndexedLineCount();
    if (line + 1 < lineCount) {
        const std::size_t end = LineEnd(line);
        return DocumentTextRange(end, LineStart(line + 1) - end);
    }
    if (line > 0) {
        const std::size_t end = LineEnd(line - 1);
        return DocumentTextRange(end, LineStart(line) - end);
    }
    return L"\r\n";
}

void EditorView::DuplicateLine() {
    if (readOnly_ || !IsEditable()) {
        return;
    }

    const std::size_t line = LineFromPosition(state_.caret_);
    const std::size_t start = LineStart(line);
    const std::size_t column = state_.caret_ - start;
    std::wstring text = DocumentTextRange(start, LineEnd(line) - start);

    // Insert the copy above so the caret stays on the original line's text.
    ApplyEdit(start, 0, text + LineBreakForLine(line), true);
    SetCaret(state_.caret_ + column, false);
}

void EditorView::DeleteLine() {
    if (readOnly_ || !IsEditable()) {
        return;
    }

    const std::size_t lineCount = IndexedLineCount();
    const std::size_t line = LineFromPosition(state_.caret_);
    const std::size_t start = LineStart(line);
    const std::size_t column = state_.caret_ - start;

    std::size_t eraseStart = start;
    std::size_t eraseEnd;
    if (line + 1 < lineCount) {
        eraseEnd = LineStart(line + 1);
    } else {
        // The last line has no trailing break, so remove the preceding one too.
        eraseEnd = DocumentLength();
        if (line > 0) {
            eraseStart = LineEnd(line - 1);
        }
    }

    if (eraseEnd == eraseStart) {
        return;
    }

    ApplyEdit(eraseStart, eraseEnd - eraseStart, L"", true);

    const std::size_t newLine = LineFromPosition(state_.caret_);
    SetCaret(std::min(LineStart(newLine) + column, LineEnd(newLine)), false);
}

void EditorView::MoveLineUp() {
    if (readOnly_ || !IsEditable()) {
        return;
    }

    const std::size_t line = LineFromPosition(state_.caret_);
    if (line == 0) {
        return;
    }

    const std::size_t column = state_.caret_ - LineStart(line);
    SwapAdjacentLines(line - 1);
    SetCaret(std::min(LineStart(line - 1) + column, LineEnd(line - 1)), false);
}

void EditorView::MoveLineDown() {
    if (readOnly_ || !IsEditable()) {
        return;
    }

    const std::size_t line = LineFromPosition(state_.caret_);
    if (line + 1 >= IndexedLineCount()) {
        return;
    }

    const std::size_t column = state_.caret_ - LineStart(line);
    SwapAdjacentLines(line);
    SetCaret(std::min(LineStart(line + 1) + column, LineEnd(line + 1)), false);
}

void EditorView::SwapAdjacentLines(std::size_t upperLine) {
    // Swap the two line texts while keeping every break character where it is,
    // so the document's final missing-newline shape is preserved when the last
    // line is involved.
    const std::size_t lowerLine = upperLine + 1;
    const std::size_t regionStart = LineStart(upperLine);
    const std::size_t upperEnd = LineEnd(upperLine);
    const std::size_t lowerStart = LineStart(lowerLine);
    const std::size_t lowerEnd = LineEnd(lowerLine);
    const std::size_t regionEnd = lowerLine + 1 < IndexedLineCount() ? LineStart(lowerLine + 1) : DocumentLength();

    const std::wstring upperText = DocumentTextRange(regionStart, upperEnd - regionStart);
    const std::wstring upperBreak = DocumentTextRange(upperEnd, lowerStart - upperEnd);
    const std::wstring lowerText = DocumentTextRange(lowerStart, lowerEnd - lowerStart);
    const std::wstring lowerBreak = DocumentTextRange(lowerEnd, regionEnd - lowerEnd);

    ApplyEdit(regionStart, regionEnd - regionStart, lowerText + upperBreak + upperText + lowerBreak, true);
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

    state_.anchor_ = 0;
    state_.caret_ = DocumentLength();
    ScrollToCaret();
    ResetCaretBlink();
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyCursorChanged();
}

} // namespace NativePad
