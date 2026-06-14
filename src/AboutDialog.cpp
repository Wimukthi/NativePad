#include "AboutDialog.h"

#include <winver.h>

#include <array>
#include <string>
#include <vector>

#include "resource.h"
#include "MessageDialog.h"
#include "UpdateChecker.h"
#include "UiSupport.h"

namespace NativePad {

namespace {

constexpr wchar_t kAboutDialogClass[] = L"NativePadAboutDialog";
constexpr wchar_t kNativePadFallbackVersion[] = L"1.0.0.0";
constexpr wchar_t kNativePadAuthor[] = L"Wimukthi Bandara";
constexpr wchar_t kNativePadLicense[] = L"GPL V3";

#define NATIVEPAD_WIDEN2(value) L##value
#define NATIVEPAD_WIDEN(value) NATIVEPAD_WIDEN2(value)
constexpr wchar_t kNativePadBuildTimestamp[] = NATIVEPAD_WIDEN(__DATE__) L" " NATIVEPAD_WIDEN(__TIME__);
#undef NATIVEPAD_WIDEN
#undef NATIVEPAD_WIDEN2

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

struct AboutDialogState {
    // The About box is a custom modal window so it can use the same dark frame,
    // app icon, and DPI-aware layout as the other NativePad dialogs.
    HWND hwnd{};
    HWND owner{};
    HWND icon{};
    HWND title{};
    HWND description{};
    std::array<HWND, 4> metadataLabels{};
    HWND updateButton{};
    HWND autoUpdate{};
    HWND ok{};
    HINSTANCE instance{};
    HFONT uiFont{};
    HFONT titleFont{};
    HBRUSH backgroundBrush{};
    UINT dpi{USER_DEFAULT_SCREEN_DPI};
    bool dark{false};
};

std::array<HWND, 9> AboutDialogControls(AboutDialogState* state) {
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
        state->updateButton,
        state->autoUpdate,
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
    const int updateButtonWidth = ScaleForDpi(138, state->dpi);
    const int buttonHeight = ScaleForDpi(30, state->dpi);
    const int buttonGap = ScaleForDpi(12, state->dpi);
    const int contentWidth = client.right - client.left - titleLeft - margin;
    const int buttonTop = client.bottom - margin - buttonHeight - ScaleForDpi(8, state->dpi);

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

    MoveWindow(state->autoUpdate, margin, buttonTop + ScaleForDpi(4, state->dpi), ScaleForDpi(190, state->dpi), ScaleForDpi(22, state->dpi), TRUE);
    MoveWindow(state->updateButton, client.right - margin - buttonWidth - buttonGap - updateButtonWidth, buttonTop, updateButtonWidth, buttonHeight, TRUE);
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

        state->autoUpdate = CreateWindowExW(
            0,
            L"BUTTON",
            L"Check automatically",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_AUTOCHECKBOX,
            0,
            0,
            0,
            0,
            hwnd,
            reinterpret_cast<HMENU>(ID_HELP_AUTO_UPDATE),
            nullptr,
            nullptr);
        SendMessageW(state->autoUpdate, BM_SETCHECK, AutomaticUpdateChecksEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

        state->updateButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Check for Updates...",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            hwnd,
            reinterpret_cast<HMENU>(ID_HELP_CHECK_UPDATES),
            nullptr,
            nullptr);

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
        if (LOWORD(wParam) == ID_HELP_AUTO_UPDATE) {
            SetAutomaticUpdateChecksEnabled(SendMessageW(state->autoUpdate, BM_GETCHECK, 0, 0) == BST_CHECKED);
            return 0;
        }
        if (LOWORD(wParam) == ID_HELP_CHECK_UPDATES) {
            PostMessageW(state->owner, WM_COMMAND, ID_HELP_CHECK_UPDATES, 0);
            DestroyWindow(hwnd);
            return 0;
        }
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

} // namespace

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
        ShowMessageDialog(owner, instance, dpi, dark, L"NativePad - About", GetLastErrorText(), MessageDialogIcon::Error);
        return;
    }

    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int effectiveDpi = static_cast<int>(dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi);
    const int width = ScaleForDpi(500, static_cast<UINT>(effectiveDpi));
    const int height = ScaleForDpi(324, static_cast<UINT>(effectiveDpi));
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
        ShowMessageDialog(owner, instance, dpi, dark, L"NativePad - About", GetLastErrorText(), MessageDialogIcon::Error);
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

} // namespace NativePad
