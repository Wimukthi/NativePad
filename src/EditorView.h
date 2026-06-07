#pragma once

#include <windows.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "DocumentBuffer.h"
#include "LineIndex.h"

namespace NativePad {

class MappedTextDocument;

constexpr UINT WM_EDITOR_CHANGED = WM_APP + 101;
constexpr UINT WM_EDITOR_CURSOR_CHANGED = WM_APP + 102;

struct EditorTheme {
    COLORREF background{};
    COLORREF text{};
    COLORREF selectionBackground{};
    COLORREF caret{};
    COLORREF lineNumberBackground{};
    COLORREF lineNumberText{};
    COLORREF lineNumberSeparator{};
};

struct EditorFontSpec {
    std::wstring family{L"Consolas"};
    float sizeDips{15.0f};
    LONG weight{FW_NORMAL};
    bool italic{false};
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
    void ResetView();
    void SetTheme(EditorTheme theme);
    void SetFont(EditorFontSpec font);
    [[nodiscard]] const EditorFontSpec& Font() const noexcept;
    void SetWordWrap(bool enabled);
    [[nodiscard]] bool WordWrap() const noexcept;
    void SetShowLineNumbers(bool enabled);
    [[nodiscard]] bool ShowLineNumbers() const noexcept;
    void SetReadOnly(bool readOnly) noexcept;
    void OnDpiChanged(UINT dpi);

    void Undo();
    void Redo();
    void Cut();
    void Copy() const;
    void Paste();
    void Delete();
    void InsertAtCaret(std::wstring text);
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
    struct EditAction {
        std::size_t position;
        std::wstring erased;
        std::wstring inserted;
        std::size_t caretBefore;
        std::size_t caretAfter;
    };

    struct VisualRow {
        std::size_t line{0};
        std::size_t columnStart{0};
        std::size_t length{0};
    };

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
    void PushUndo(EditAction action);
    void NotifyChanged();
    void NotifyCursorChanged() const;
    void OnCharacter(wchar_t value);
    void OnKeyDown(WPARAM key);
    void OnMouseDown(int x, int y, bool extendSelection);
    void OnMouseDoubleClick(int x, int y);
    void OnMouseMove(int x, int y);
    void OnMouseWheel(short delta);
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
    std::size_t LineStart(std::size_t line) const;
    std::size_t LineEnd(std::size_t line) const;
    std::size_t LineLength(std::size_t line) const;
    std::size_t LineFromPosition(std::size_t position) const;
    std::size_t PositionFromLineColumn(std::size_t line, std::size_t column) const;
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
    std::size_t MaxFirstVisualRow(std::size_t visibleRows) const;
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

    HWND hwnd_{};
    HWND parent_{};
    HINSTANCE instance_{};
    DocumentBuffer* document_{};
    MappedTextDocument* mappedDocument_{};
    EditorTheme theme_{};
    EditorFontSpec font_{};

    struct Impl;
    Impl* impl_{};

    LineIndex lineIndex_;
    std::vector<EditAction> undoStack_;
    std::vector<EditAction> redoStack_;
    // When word wrap is on, this prefix table maps visual rows back to logical
    // lines. It is rebuilt lazily because changing font/window width invalidates it.
    mutable std::vector<std::size_t> visualRowStarts_;
    std::size_t caret_{0};
    std::size_t anchor_{0};
    std::size_t firstLine_{0};
    std::size_t firstVisualRow_{0};
    std::size_t horizontalColumn_{0};
    std::size_t desiredColumn_{0};
    POINT lastDoubleClickPoint_{};
    DWORD lastDoubleClickTick_{0};
    UINT dpi_{USER_DEFAULT_SCREEN_DPI};
    float lineHeight_{18.0f};
    float charWidth_{8.0f};
    mutable std::size_t visualRowCacheColumns_{0};
    mutable std::size_t totalVisualRows_{1};
    mutable bool visualRowCacheValid_{false};
    bool dragging_{false};
    bool readOnly_{false};
    bool wordWrap_{false};
    bool showLineNumbers_{false};
    bool caretVisible_{true};
};

} // namespace NativePad
