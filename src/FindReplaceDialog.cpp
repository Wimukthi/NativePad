#include "FindReplaceDialog.h"

#include <array>
#include <string>

#include "MessageDialog.h"
#include "UiSupport.h"

namespace NativePad {

namespace {

constexpr wchar_t kFindReplaceDialogClass[] = L"NativePadFindReplaceDialog";
constexpr int kFindTextEditId = 50101;
constexpr int kReplaceTextEditId = 50102;
constexpr int kFindMatchCaseId = 50103;
constexpr int kFindDirectionUpId = 50104;
constexpr int kFindDirectionDownId = 50105;
constexpr int kFindReplaceButtonId = 50106;
constexpr int kFindReplaceAllButtonId = 50107;
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
    HWND directionGroup{};
    HWND upRadio{};
    HWND downRadio{};
    HWND findNext{};
    HWND replace{};
    HWND replaceAll{};
    HWND cancel{};
    HFONT font{};
    HBRUSH backgroundBrush{};
    UINT dpi{USER_DEFAULT_SCREEN_DPI};
    bool dark{false};
    bool replaceMode{false};
    bool closedNotified{false};
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
    request.matchCase = SendMessageW(state->matchCase, BM_GETCHECK, 0, 0) == BST_CHECKED;
    request.down = SendMessageW(state->downRadio, BM_GETCHECK, 0, 0) == BST_CHECKED;
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
    // the themed radio/check controls from overlapping the command buttons.
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
    MoveWindow(state->findEdit, editLeft, top, editWidth, rowHeight, TRUE);
    MoveWindow(state->findNext, rightPaneLeft, top, buttonWidth, buttonHeight, TRUE);

    int nextY = top + rowHeight + gap;
    if (state->replaceMode) {
        MoveWindow(state->replaceLabel, margin, nextY + ScaleForDpi(3, state->dpi), labelWidth, labelHeight, TRUE);
        MoveWindow(state->replaceEdit, editLeft, nextY, editWidth, rowHeight, TRUE);
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
    MoveWindow(state->directionGroup, directionLeft, groupTop, groupWidth, groupHeight, TRUE);
    const int matchWidth = std::max(ScaleForDpi(96, state->dpi), directionLeft - margin - gap);
    const int optionTop = groupTop + ScaleForDpi(30, state->dpi);
    const int radioLeft = directionLeft + ScaleForDpi(16, state->dpi);
    const int radioGap = ScaleForDpi(18, state->dpi);
    const int radioRight = directionLeft + groupWidth - ScaleForDpi(12, state->dpi);
    // Keep radio labels inside the group box across DPI
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

void ApplyFindReplaceDialogTheme(FindReplaceDialogState* state) {
    if (state == nullptr) {
        return;
    }

    state->dark = IsNativeThemeDark();
    if (state->backgroundBrush != nullptr) {
        DeleteObject(state->backgroundBrush);
    }
    state->backgroundBrush = CreateSolidBrush(DialogColorsForTheme(state->dark).background);
    RefreshThemedDialog(state->hwnd);
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

    FillRect(hdc, &ps.rcPaint, state->backgroundBrush);
    EndPaint(state->hwnd, &ps);
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
        state->font = CreateUiFontForDpi(state->dpi);

        state->findLabel = CreateWindowExW(0, L"STATIC", L"Find what:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        state->findEdit = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFindTextEditId)), nullptr, nullptr);
        if (state->replaceMode) {
            state->replaceLabel = CreateWindowExW(0, L"STATIC", L"Replace with:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
            state->replaceEdit = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReplaceTextEditId)), nullptr, nullptr);
        }
        state->matchCase = CreateWindowExW(0, L"BUTTON", L"Match case", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFindMatchCaseId)), nullptr, nullptr);
        state->directionGroup = CreateWindowExW(0, L"BUTTON", L"Direction", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        state->upRadio = CreateWindowExW(0, L"BUTTON", L"Up", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFindDirectionUpId)), nullptr, nullptr);
        state->downRadio = CreateWindowExW(0, L"BUTTON", L"Down", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFindDirectionDownId)), nullptr, nullptr);
        state->findNext = CreateWindowExW(0, L"BUTTON", L"Find Next", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
        if (state->replaceMode) {
            state->replace = CreateWindowExW(0, L"BUTTON", L"Replace", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFindReplaceButtonId)), nullptr, nullptr);
            state->replaceAll = CreateWindowExW(0, L"BUTTON", L"Replace All", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFindReplaceAllButtonId)), nullptr, nullptr);
        }
        state->cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);

        const std::array<HWND, 12> controls{
            state->findLabel,
            state->findEdit,
            state->replaceLabel,
            state->replaceEdit,
            state->matchCase,
            state->directionGroup,
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
        SendMessageW(state->matchCase, BM_SETCHECK, state->initialMatchCase ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(state->upRadio, BM_SETCHECK, state->initialDown ? BST_UNCHECKED : BST_CHECKED, 0);
        SendMessageW(state->downRadio, BM_SETCHECK, state->initialDown ? BST_CHECKED : BST_UNCHECKED, 0);

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
    case WM_SETTINGCHANGE:
        HandleThemeSettingChange(lParam);
        ApplyFindReplaceDialogTheme(state);
        return 0;
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        ApplyFindReplaceDialogTheme(state);
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
        case kFindMatchCaseId:
        case kFindDirectionUpId:
        case kFindDirectionDownId:
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
            state->font = nullptr;
            state->backgroundBrush = nullptr;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete state;
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace
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
        ShowMessageDialog(
            owner,
            instance,
            dpi,
            dark,
            replaceMode ? L"NativePad - Replace" : L"NativePad - Find",
            GetLastErrorText(),
            MessageDialogIcon::Error);
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
        ShowMessageDialog(
            owner,
            instance,
            dpi,
            dark,
            replaceMode ? L"NativePad - Replace" : L"NativePad - Find",
            GetLastErrorText(),
            MessageDialogIcon::Error);
        return nullptr;
    }
    ApplyWindowIcons(dialog, instance);

    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    return dialog;
}
} // namespace NativePad
