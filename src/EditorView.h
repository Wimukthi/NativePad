#pragma once

#include <windows.h>
#include <d2d1.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "DocumentBuffer.h"
#include "LineIndex.h"
#include "SyntaxHighlighter.h"

namespace NativePad {

class MappedTextDocument;
class LargeTextDocument;

constexpr UINT WM_EDITOR_CHANGED = WM_APP + 101;
constexpr UINT WM_EDITOR_CURSOR_CHANGED = WM_APP + 102;
constexpr UINT WM_EDITOR_ZOOM_CHANGED = WM_APP + 103;
// Sent when the user presses the classic edit-control reading-order shortcut
// (Ctrl+Right Shift for right-to-left, Ctrl+Left Shift for left-to-right).
// WPARAM is 1 for right-to-left and 0 for left-to-right.
constexpr UINT WM_EDITOR_READING_ORDER_REQUEST = WM_APP + 104;

struct EditorTheme {
    COLORREF background{};
    COLORREF text{};
    COLORREF selectionBackground{};
    COLORREF caret{};
    COLORREF lineNumberBackground{};
    COLORREF lineNumberText{};
    COLORREF lineNumberSeparator{};
};

struct EditorSyntaxTheme {
    COLORREF keyword{};
    COLORREF string{};
    COLORREF number{};
    COLORREF comment{};
    COLORREF punctuation{};
};

struct EditorFontSpec {
    std::wstring family{L"Consolas"};
    float sizeDips{15.0f};
    LONG weight{FW_NORMAL};
    bool italic{false};
};

// Document-specific interaction state. NativePad keeps one EditorView HWND and
// moves this compact state object between tabs, so inactive tabs retain undo,
// selection, and scroll position without owning DirectWrite/Direct2D resources.
class EditorViewState {
public:
    EditorViewState() = default;
    EditorViewState(EditorViewState&&) noexcept = default;
    EditorViewState& operator=(EditorViewState&&) noexcept = default;

    EditorViewState(const EditorViewState&) = delete;
    EditorViewState& operator=(const EditorViewState&) = delete;

private:
    struct EditAction {
        std::size_t position{};
        std::wstring erased;
        std::wstring inserted;
        std::size_t erasedUnits{};
        std::size_t insertedUnits{};
        std::size_t caretBefore{};
        std::size_t caretAfter{};
    };

    friend class EditorView;

    LineIndex lineIndex_;
    std::vector<EditAction> undoStack_;
    std::vector<EditAction> redoStack_;
    std::size_t caret_{0};
    std::size_t anchor_{0};
    std::size_t firstLine_{0};
    std::size_t firstVisualRow_{0};
    std::size_t horizontalColumn_{0};
    std::size_t desiredColumn_{0};
    bool initialized_{false};
};

class EditorView {
public:
    EditorView();
    ~EditorView();

    static bool Register(HINSTANCE instance);

    bool Create(HWND parent, HINSTANCE instance, DocumentBuffer* document);
    [[nodiscard]] HWND Hwnd() const noexcept;

    void SetDocument(DocumentBuffer* document);
    void SetMappedDocument(MappedTextDocument* document);
    void SetLargeDocument(LargeTextDocument* document);
    void SetDocument(DocumentBuffer* document, EditorViewState state);
    void SetMappedDocument(MappedTextDocument* document, EditorViewState state);
    void SetLargeDocument(LargeTextDocument* document, EditorViewState state);
    [[nodiscard]] EditorViewState TakeState() noexcept;
    void ResetView();
    void RefreshDocumentMetrics();
    void MoveCaretToDocumentEnd();
    void SetTheme(EditorTheme theme);
    void SetSyntaxTheme(EditorSyntaxTheme theme);
    void SetSyntaxLanguage(SyntaxLanguage language);
    [[nodiscard]] SyntaxLanguage SyntaxLanguageForDocument() const noexcept;
    void SetFont(EditorFontSpec font);
    [[nodiscard]] const EditorFontSpec& Font() const noexcept;
    void SetZoomPercent(int percent);
    [[nodiscard]] int ZoomPercent() const noexcept;
    void SetWordWrap(bool enabled);
    [[nodiscard]] bool WordWrap() const noexcept;
    void SetShowLineNumbers(bool enabled);
    [[nodiscard]] bool ShowLineNumbers() const noexcept;
    // Right-to-left reading order: paragraphs start at the right edge and use
    // an RTL base direction, as in classic Notepad's "Right to left Reading
    // order" option. Mixed-direction text is laid out by DirectWrite either way.
    void SetRightToLeft(bool enabled);
    [[nodiscard]] bool RightToLeft() const noexcept;
    void CancelReadingOrderShortcut() noexcept;
    void SetReadOnly(bool readOnly) noexcept;
    void OnDpiChanged(UINT dpi);
    void ResetDeviceResources();

    void Undo();
    void Redo();
    void Cut();
    void Copy() const;
    void Paste();
    void Delete();
    void InsertAtCaret(std::wstring text);
    void DuplicateLine();
    void DeleteLine();
    void MoveLineUp();
    void MoveLineDown();
    void SelectRange(std::size_t start, std::size_t length);
    void GoToLine(std::size_t line);
    void SelectAll();
    [[nodiscard]] std::wstring SelectedText() const;

    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;
    [[nodiscard]] bool IsReadOnly() const noexcept;
    [[nodiscard]] bool HasSelection() const noexcept;
    [[nodiscard]] std::size_t CaretPosition() const noexcept;
    [[nodiscard]] std::size_t SelectionStart() const noexcept;
    [[nodiscard]] std::size_t SelectionEnd() const noexcept;
    [[nodiscard]] std::size_t Line() const;
    [[nodiscard]] std::size_t Column() const;
    [[nodiscard]] std::size_t LineCount() const noexcept;

private:
    // Paint and input paths use the same coordinate system: a document position
    // is either a wchar offset in the editable buffer/UTF-16 mapped file, or a
    // byte offset in a byte-backed mapped file.
    using EditAction = EditorViewState::EditAction;

    // Result of a low-level backend replace, in document units.
    struct BackendEdit {
        std::size_t position{};
        std::wstring erased;
        std::size_t erasedUnits{};
        std::size_t insertedUnits{};
    };

    struct VisualRow {
        std::size_t line{0};
        std::size_t columnStart{0};
        std::size_t length{0};
    };

    // Horizontal geometry of one visual row. Plain ASCII left-to-right rows use
    // the fast monospace grid; rows with complex scripts (Arabic, Hebrew,
    // combining marks, CJK...), syntax highlighting, or right-to-left reading
    // order go through a DirectWrite layout so caret, selection, and hit
    // testing follow the shaped and bidi-reordered glyphs. Defined in the .cpp
    // because it holds a DirectWrite layout.
    struct RowGeometry;

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void Paint();
    void DiscardDeviceResources();
    bool EnsureDeviceResources();
    void RecreateTextFormat();
    void MeasureTextMetrics();
    void RebuildLineIndex();
    void UpdateLineIndexForEdit(std::size_t position, std::wstring_view erased, std::wstring_view inserted);
    void InvalidateVisualRowCache() noexcept;
    void EnsureVisualRowCache() const;
    void UpdateScrollbars();
    void UpdateScrollPositions();
    void ScrollToCaret();
    void StartCaretBlink();
    void StopCaretBlink();
    void ResetCaretBlink();
    void SetCaret(std::size_t position, bool extendSelection);
    void MoveCaretHorizontal(int delta, bool extendSelection);
    void MoveCaretVertical(int delta, bool extendSelection);
    void MoveCaretToLineBoundary(bool end, bool extendSelection);
    void DeleteSelectionOrRange(bool backspace);
    void InsertText(std::wstring text);
    void ApplyEdit(std::size_t position, std::size_t eraseLength, std::wstring insertText, bool recordUndo);
    [[nodiscard]] bool IsEditable() const noexcept;
    BackendEdit BackendReplace(std::size_t position, std::size_t eraseLength, const std::wstring& insertText);
    void PushUndo(EditAction action);
    void RestoreState(EditorViewState state);
    void NotifyChanged();
    void NotifyCursorChanged() const;
    void OnCharacter(wchar_t value);
    void OnKeyDown(WPARAM key);
    void OnMouseDown(int x, int y, bool extendSelection);
    void OnMouseDoubleClick(int x, int y);
    void OnMouseMove(int x, int y);
    void OnMouseWheel(short delta);
    void OnDragScrollTimer();
    void UpdateDragSelection(int x, int y);
    void CaptureMouseDrag();
    void ReleaseMouseDrag();
    // The following helpers isolate the two backends so rendering and navigation
    // do not need to know whether the active document is editable or mapped.
    [[nodiscard]] bool HasDocument() const noexcept;
    [[nodiscard]] std::size_t DocumentLength() const noexcept;
    [[nodiscard]] wchar_t DocumentCharAt(std::size_t position) const;
    [[nodiscard]] std::wstring DocumentTextRange(std::size_t position, std::size_t length) const;
    [[nodiscard]] std::size_t IndexedLineCount() const noexcept;
    [[nodiscard]] std::size_t IndexedMaxLineLength() const noexcept;
    std::size_t HitTest(int x, int y) const;
    void BuildRowGeometry(const VisualRow& visual, RowGeometry& geometry) const;
    [[nodiscard]] std::size_t RowUnitToIndex(const RowGeometry& geometry, std::size_t unitOffset) const noexcept;
    [[nodiscard]] std::size_t RowIndexToUnit(const RowGeometry& geometry, std::size_t index) const noexcept;
    [[nodiscard]] float RowCaretOffsetDips(const RowGeometry& geometry, std::size_t unitOffset) const;
    [[nodiscard]] float RowCaretX(const RowGeometry& geometry, std::size_t unitOffset) const;
    [[nodiscard]] std::size_t RowHitTest(const RowGeometry& geometry, float xDip) const;
    [[nodiscard]] std::size_t CaretVisualRowIndex() const;
    std::size_t LineStart(std::size_t line) const;
    std::size_t LineEnd(std::size_t line) const;
    std::size_t LineLength(std::size_t line) const;
    std::size_t LineFromPosition(std::size_t position) const;
    std::size_t PositionFromLineColumn(std::size_t line, std::size_t column) const;
    std::wstring LineBreakForLine(std::size_t line) const;
    void SwapAdjacentLines(std::size_t upperLine);
    void SelectWordAt(std::size_t position);
    void SelectLineAtPosition(std::size_t position);
    bool IsTripleClick(int x, int y, DWORD now) const noexcept;
    void RememberDoubleClick(int x, int y, DWORD now) noexcept;
    std::size_t WrapColumnCount() const;
    std::size_t VisualRowCountForLine(std::size_t line) const;
    std::size_t VisualRowCountForLine(std::size_t line, std::size_t columns) const;
    std::size_t TotalVisualRows() const;
    std::size_t VisualRowIndexForPosition(std::size_t position) const;
    VisualRow VisualRowFromIndex(std::size_t visualRow) const;
    std::size_t VisibleRowCount() const;
    std::size_t MaxFirstLogicalLine(std::size_t visibleRows) const;
    std::size_t MaxFirstVisualRow(std::size_t visibleRows) const;
    std::size_t MaxHorizontalScrollColumn() const;
    std::size_t CaretDisplayColumn() const;
    std::size_t LineNumberDigitCount() const noexcept;
    float LineNumberGutterWidthDips() const;
    float TextLeftDips() const;
    float TextViewportWidthDips() const;
    bool CursorIsInLineNumberGutter() const;
    bool SetClipboardText(const std::wstring& text) const;
    std::wstring ClipboardText() const;
    float DpiScale() const noexcept;
    float PixelsToDips(int pixels) const noexcept;
    float ClientWidthDips() const;
    float ClientHeightDips() const;
    [[nodiscard]] ID2D1SolidColorBrush* SyntaxBrush(SyntaxColor color) const noexcept;

    HWND hwnd_{};
    HWND parent_{};
    HINSTANCE instance_{};
    DocumentBuffer* document_{};
    MappedTextDocument* mappedDocument_{};
    LargeTextDocument* largeDocument_{};
    EditorTheme theme_{};
    EditorSyntaxTheme syntaxTheme_{};
    EditorFontSpec font_{};
    SyntaxLanguage syntaxLanguage_{SyntaxLanguage::PlainText};
    SyntaxHighlighter syntaxHighlighter_{};

    struct Impl;
    Impl* impl_{};

    EditorViewState state_;
    // When word wrap is on, this prefix table maps visual rows back to logical
    // lines. It is rebuilt lazily because changing font/window width invalidates it.
    mutable std::vector<std::size_t> visualRowStarts_;
    POINT dragPoint_{};
    POINT lastDoubleClickPoint_{};
    DWORD lastDoubleClickTick_{0};
    UINT dpi_{USER_DEFAULT_SCREEN_DPI};
    int zoomPercent_{100};
    float lineHeight_{18.0f};
    float charWidth_{8.0f};
    mutable std::size_t visualRowCacheColumns_{0};
    mutable std::size_t totalVisualRows_{1};
    mutable bool visualRowCacheValid_{false};
    // Widest laid-out row seen since the last layout invalidation, in columns.
    // Shaped text can be wider than its character count suggests (CJK, wide
    // fallback fonts), so horizontal scrolling must cover it.
    mutable std::size_t measuredMaxColumns_{0};
    // 0 = none, 1 = Ctrl+Right Shift pending (RTL), 2 = Ctrl+Left Shift (LTR).
    int pendingReadingOrder_{0};
    bool rightToLeft_{false};
    bool dragging_{false};
    bool readOnly_{false};
    bool wordWrap_{false};
    bool showLineNumbers_{false};
    bool caretVisible_{true};
};

} // namespace NativePad
