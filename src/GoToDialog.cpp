#include "GoToDialog.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <cwctype>
#include <string>

#include "MessageDialog.h"
#include "UiSupport.h"

namespace NativePad {

namespace {

constexpr wchar_t kGoToDialogClass[] = L"NativePadGoToDialog";
constexpr int kGoToEditId = 50001;

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
    HINSTANCE instance{};
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

bool TryReadGoToLine(GoToDialogState* state, size_t& line) {
    // UI input is one-based to match Notepad. EditorView consumes zero-based
    // line indices, so convert only after validation succeeds.
    if (state == nullptr) {
        return false;
    }

    std::array<wchar_t, 64> buffer{};
    GetWindowTextW(state->edit, buffer.data(), static_cast<int>(buffer.size()));

    wchar_t* end = nullptr;
    const unsigned long long value = wcstoull(buffer.data(), &end, 10);
    while (end != nullptr && std::iswspace(*end) != 0) {
        ++end;
    }

    if (buffer[0] == L'\0' || end == buffer.data() || (end != nullptr && *end != L'\0') || value == 0 || value > state->maxLine) {
        std::wstring message = L"Line number must be between 1 and ";
        message += std::to_wstring(state->maxLine);
        message += L".";
        ShowMessageDialog(
            state->hwnd,
            state->instance,
            state->dpi,
            state->dark,
            L"NativePad - Go To Line",
            message,
            MessageDialogIcon::Information);
        SetFocus(state->edit);
        SendMessageW(state->edit, EM_SETSEL, 0, -1);
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
            if (TryReadGoToLine(state, state->selectedLine)) {
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

} // namespace

std::optional<std::size_t> ShowGoToLineDialog(HWND owner, HINSTANCE instance, UINT dpi, bool dark, std::size_t currentLine, std::size_t maxLine) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &GoToDialogProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kGoToDialogClass;
    AssignWindowClassIcons(wc, instance);
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        ShowMessageDialog(owner, instance, dpi, dark, L"NativePad - Go To Line", GetLastErrorText(), MessageDialogIcon::Error);
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
    state.instance = instance;
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
        ShowMessageDialog(owner, instance, dpi, dark, L"NativePad - Go To Line", GetLastErrorText(), MessageDialogIcon::Error);
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

} // namespace NativePad
