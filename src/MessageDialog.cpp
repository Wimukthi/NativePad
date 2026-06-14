#include "MessageDialog.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "resource.h"
#include "UiSupport.h"

namespace NativePad {

namespace {

constexpr wchar_t kMessageDialogClass[] = L"NativePadMessageDialog";

struct MessageButtonSpec {
    int id{};
    const wchar_t* text{};
};

struct MessageDialogState {
    HWND hwnd{};
    HWND owner{};
    HWND iconControl{};
    HWND messageControl{};
    std::array<HWND, 3> buttons{};
    HINSTANCE instance{};
    HFONT font{};
    HBITMAP iconBitmap{};
    HBRUSH backgroundBrush{};
    std::wstring title;
    std::wstring message;
    std::vector<MessageButtonSpec> buttonSpecs;
    MessageDialogIcon icon{MessageDialogIcon::Information};
    MessageDialogButtons buttonSet{MessageDialogButtons::Ok};
    UINT dpi{USER_DEFAULT_SCREEN_DPI};
    int messageHeight{};
    int result{IDCANCEL};
    int defaultResult{IDOK};
    bool dark{false};
};

std::vector<MessageButtonSpec> ButtonSpecsFor(MessageDialogButtons buttons) {
    switch (buttons) {
    case MessageDialogButtons::OkCancel:
        return {{IDOK, L"OK"}, {IDCANCEL, L"Cancel"}};
    case MessageDialogButtons::YesNo:
        return {{IDYES, L"Yes"}, {IDNO, L"No"}};
    case MessageDialogButtons::YesNoCancel:
        return {{IDYES, L"Yes"}, {IDNO, L"No"}, {IDCANCEL, L"Cancel"}};
    case MessageDialogButtons::Ok:
    default:
        return {{IDOK, L"OK"}};
    }
}

int CloseResultFor(MessageDialogButtons buttons) {
    switch (buttons) {
    case MessageDialogButtons::Ok:
        return IDOK;
    case MessageDialogButtons::OkCancel:
    case MessageDialogButtons::YesNoCancel:
        return IDCANCEL;
    case MessageDialogButtons::YesNo:
    default:
        return 0;
    }
}

UINT FallbackFlags(MessageDialogIcon icon, MessageDialogButtons buttons, int defaultResult) {
    UINT flags = 0;
    switch (icon) {
    case MessageDialogIcon::Warning:
        flags |= MB_ICONWARNING;
        break;
    case MessageDialogIcon::Error:
        flags |= MB_ICONERROR;
        break;
    case MessageDialogIcon::Question:
        flags |= MB_ICONQUESTION;
        break;
    case MessageDialogIcon::Information:
        flags |= MB_ICONINFORMATION;
        break;
    case MessageDialogIcon::None:
    default:
        break;
    }

    switch (buttons) {
    case MessageDialogButtons::OkCancel:
        flags |= MB_OKCANCEL;
        break;
    case MessageDialogButtons::YesNo:
        flags |= MB_YESNO;
        break;
    case MessageDialogButtons::YesNoCancel:
        flags |= MB_YESNOCANCEL;
        break;
    case MessageDialogButtons::Ok:
    default:
        flags |= MB_OK;
        break;
    }

    if (defaultResult == IDNO) {
        flags |= MB_DEFBUTTON2;
    } else if (defaultResult == IDCANCEL && buttons == MessageDialogButtons::YesNoCancel) {
        flags |= MB_DEFBUTTON3;
    } else {
        flags |= MB_DEFBUTTON1;
    }

    return flags;
}

LPCWSTR SystemIconFor(MessageDialogIcon icon) {
    switch (icon) {
    case MessageDialogIcon::Warning:
        return IDI_WARNING;
    case MessageDialogIcon::Error:
        return IDI_ERROR;
    case MessageDialogIcon::Question:
        return IDI_QUESTION;
    case MessageDialogIcon::Information:
        return IDI_INFORMATION;
    case MessageDialogIcon::None:
    default:
        return nullptr;
    }
}

int MessagePngResourceFor(MessageDialogIcon icon) {
    switch (icon) {
    case MessageDialogIcon::Warning:
        return IDB_MESSAGE_WARNING;
    case MessageDialogIcon::Error:
        return IDB_MESSAGE_ERROR;
    case MessageDialogIcon::Question:
        return IDB_MESSAGE_QUESTION;
    case MessageDialogIcon::Information:
        return IDB_MESSAGE_INFO;
    case MessageDialogIcon::None:
    default:
        return 0;
    }
}

class ScopedComApartment {
public:
    ScopedComApartment()
        : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)),
          shouldUninitialize_(result_ == S_OK || result_ == S_FALSE) {}

    ~ScopedComApartment() {
        if (shouldUninitialize_) {
            CoUninitialize();
        }
    }

private:
    HRESULT result_{E_FAIL};
    bool shouldUninitialize_{false};
};

void PremultiplyPngOverBackground(void* bits, int width, int height, COLORREF background) {
    if (bits == nullptr || width <= 0 || height <= 0) {
        return;
    }

    const BYTE backgroundBlue = GetBValue(background);
    const BYTE backgroundGreen = GetGValue(background);
    const BYTE backgroundRed = GetRValue(background);
    auto* pixels = static_cast<BYTE*>(bits);
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t index = 0; index < count; ++index) {
        BYTE* pixel = pixels + (index * 4);
        const BYTE alpha = pixel[3];
        const int inverseAlpha = 255 - alpha;
        pixel[0] = static_cast<BYTE>(std::min(255, static_cast<int>(pixel[0]) + ((static_cast<int>(backgroundBlue) * inverseAlpha + 127) / 255)));
        pixel[1] = static_cast<BYTE>(std::min(255, static_cast<int>(pixel[1]) + ((static_cast<int>(backgroundGreen) * inverseAlpha + 127) / 255)));
        pixel[2] = static_cast<BYTE>(std::min(255, static_cast<int>(pixel[2]) + ((static_cast<int>(backgroundRed) * inverseAlpha + 127) / 255)));
        pixel[3] = 255;
    }
}

HBITMAP CreateMessageIconBitmap(HINSTANCE instance, MessageDialogIcon icon, int targetSize, COLORREF background) {
    const int resourceId = MessagePngResourceFor(icon);
    if (resourceId == 0 || instance == nullptr || targetSize <= 0) {
        return nullptr;
    }

    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resourceId), L"PNG");
    if (resource == nullptr) {
        return nullptr;
    }

    HGLOBAL loadedResource = LoadResource(instance, resource);
    const DWORD resourceSize = SizeofResource(instance, resource);
    void* resourceBytes = loadedResource != nullptr ? LockResource(loadedResource) : nullptr;
    if (resourceBytes == nullptr || resourceSize == 0) {
        return nullptr;
    }

    ScopedComApartment comApartment;
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IWICStream> stream;
    hr = factory->CreateStream(stream.GetAddressOf());
    if (FAILED(hr)) {
        return nullptr;
    }

    hr = stream->InitializeFromMemory(static_cast<BYTE*>(resourceBytes), resourceSize);
    if (FAILED(hr)) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr)) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr)) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
    hr = factory->CreateBitmapScaler(scaler.GetAddressOf());
    if (FAILED(hr)) {
        return nullptr;
    }

    // Decode from the embedded high-resolution PNG into the exact display size
    // for the current DPI so message icons stay crisp on scaled monitors.
    hr = scaler->Initialize(frame.Get(), static_cast<UINT>(targetSize), static_cast<UINT>(targetSize), WICBitmapInterpolationModeFant);
    if (FAILED(hr)) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) {
        return nullptr;
    }

    hr = converter->Initialize(
        scaler.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        return nullptr;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = targetSize;
    info.bmiHeader.biHeight = -targetSize;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr) {
        return nullptr;
    }

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screenDc);
    if (bitmap == nullptr || bits == nullptr) {
        return nullptr;
    }

    const UINT stride = static_cast<UINT>(targetSize * 4);
    const UINT bufferSize = stride * static_cast<UINT>(targetSize);
    hr = converter->CopyPixels(nullptr, stride, bufferSize, static_cast<BYTE*>(bits));
    if (FAILED(hr)) {
        DeleteObject(bitmap);
        return nullptr;
    }

    PremultiplyPngOverBackground(bits, targetSize, targetSize, background);
    return bitmap;
}

int MeasureMessageHeight(std::wstring_view message, int textWidth, HFONT font) {
    HDC hdc = GetDC(nullptr);
    if (hdc == nullptr) {
        return USER_DEFAULT_SCREEN_DPI;
    }

    HGDIOBJ oldFont = font != nullptr ? SelectObject(hdc, font) : nullptr;
    RECT rect{0, 0, std::max(1, textWidth), 0};
    const std::wstring text(message);
    DrawTextW(hdc, text.c_str(), -1, &rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    if (oldFont != nullptr) {
        SelectObject(hdc, oldFont);
    }
    ReleaseDC(nullptr, hdc);
    return std::max<int>(static_cast<int>(rect.bottom - rect.top), 1);
}

RECT OwnerOrMonitorRect(HWND owner) {
    RECT rect{};
    if (owner != nullptr && GetWindowRect(owner, &rect)) {
        return rect;
    }

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTOPRIMARY);
    if (GetMonitorInfoW(monitor, &monitorInfo)) {
        return monitorInfo.rcWork;
    }

    return {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
}

void CompleteMessageDialog(MessageDialogState* state, int result) {
    if (state == nullptr || state->hwnd == nullptr) {
        return;
    }

    state->result = result;
    DestroyWindow(state->hwnd);
}

void LayoutMessageDialog(MessageDialogState* state) {
    if (state == nullptr || state->hwnd == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(state->hwnd, &client);
    const int margin = ScaleForDpi(18, state->dpi);
    const int iconSize = ScaleForDpi(32, state->dpi);
    const int iconGap = ScaleForDpi(14, state->dpi);
    const int buttonWidth = ScaleForDpi(88, state->dpi);
    const int buttonHeight = ScaleForDpi(30, state->dpi);
    const int buttonGap = ScaleForDpi(10, state->dpi);
    const int hasIcon = state->iconControl != nullptr ? 1 : 0;
    const int textLeft = margin + (hasIcon * (iconSize + iconGap));
    const int textWidth = std::max<int>(1, static_cast<int>(client.right - margin - textLeft));
    const int contentTop = margin;

    if (state->iconControl != nullptr) {
        MoveWindow(state->iconControl, margin, contentTop + ScaleForDpi(1, state->dpi), iconSize, iconSize, TRUE);
    }
    MoveWindow(state->messageControl, textLeft, contentTop, textWidth, state->messageHeight, TRUE);

    const int buttonCount = static_cast<int>(state->buttonSpecs.size());
    const int totalButtonWidth = (buttonCount * buttonWidth) + (std::max(0, buttonCount - 1) * buttonGap);
    int buttonLeft = client.right - margin - totalButtonWidth;
    const int buttonTop = client.bottom - margin - buttonHeight;
    for (int index = 0; index < buttonCount; ++index) {
        MoveWindow(state->buttons[static_cast<size_t>(index)], buttonLeft, buttonTop, buttonWidth, buttonHeight, TRUE);
        buttonLeft += buttonWidth + buttonGap;
    }
}

LRESULT MessageDialogCtlColor(MessageDialogState* state, WPARAM wParam) {
    if (state == nullptr) {
        return 0;
    }

    const DialogColors colors = DialogColorsForTheme(state->dark);
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetTextColor(dc, colors.text);
    SetBkColor(dc, colors.background);
    return reinterpret_cast<LRESULT>(state->backgroundBrush);
}

LRESULT CALLBACK MessageDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<MessageDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<MessageDialogState*>(create->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_CREATE: {
        const DialogColors colors = DialogColorsForTheme(state->dark);
        state->font = CreateUiFontForDpi(state->dpi);
        state->backgroundBrush = CreateSolidBrush(colors.background);
        ApplyDarkFrame(hwnd, state->dark);
        ApplyWindowIcons(hwnd, state->instance);

        state->messageControl = CreateWindowExW(
            0,
            L"STATIC",
            state->message.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0,
            0,
            0,
            0,
            hwnd,
            nullptr,
            state->instance,
            nullptr);

        const int iconSize = ScaleForDpi(32, state->dpi);
        state->iconBitmap = CreateMessageIconBitmap(state->instance, state->icon, iconSize, colors.background);
        if (state->iconBitmap != nullptr) {
            state->iconControl = CreateWindowExW(
                0,
                L"STATIC",
                nullptr,
                WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE,
                0,
                0,
                0,
                0,
                hwnd,
                nullptr,
                state->instance,
                nullptr);
            SendMessageW(state->iconControl, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(state->iconBitmap));
        } else if (LPCWSTR iconResource = SystemIconFor(state->icon)) {
            state->iconControl = CreateWindowExW(
                0,
                L"STATIC",
                nullptr,
                WS_CHILD | WS_VISIBLE | SS_ICON,
                0,
                0,
                0,
                0,
                hwnd,
                nullptr,
                state->instance,
                nullptr);
            SendMessageW(state->iconControl, STM_SETICON, reinterpret_cast<WPARAM>(LoadIconW(nullptr, iconResource)), 0);
        }

        for (size_t index = 0; index < state->buttonSpecs.size(); ++index) {
            const MessageButtonSpec& spec = state->buttonSpecs[index];
            const DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                (spec.id == state->defaultResult ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
            state->buttons[index] = CreateWindowExW(
                0,
                L"BUTTON",
                spec.text,
                style,
                0,
                0,
                0,
                0,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(spec.id)),
                state->instance,
                nullptr);
        }

        std::array<HWND, 4> controls{state->messageControl, state->buttons[0], state->buttons[1], state->buttons[2]};
        for (HWND control : controls) {
            SetControlFont(control, state->font);
            ApplyDialogControlTheme(control, state->dark);
        }

        // Match MessageBox behavior: Yes/No prompts cannot be dismissed through
        // the close box because there is no unambiguous cancellation result.
        if (CloseResultFor(state->buttonSet) == 0) {
            EnableMenuItem(GetSystemMenu(hwnd, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
        }

        LayoutMessageDialog(state);
        if (HWND defaultButton = GetDlgItem(hwnd, state->defaultResult)) {
            SetFocus(defaultButton);
        }
        return 0;
    }
    case WM_SIZE:
        LayoutMessageDialog(state);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return MessageDialogCtlColor(state, wParam);
    case WM_ERASEBKGND: {
        if (state == nullptr) {
            return 1;
        }
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
        return 1;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        for (const MessageButtonSpec& spec : state->buttonSpecs) {
            if (id == spec.id) {
                CompleteMessageDialog(state, id);
                return 0;
            }
        }
        break;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            const int closeResult = CloseResultFor(state->buttonSet);
            if (closeResult != 0) {
                CompleteMessageDialog(state, closeResult);
            }
            return 0;
        }
        break;
    case WM_CLOSE: {
        const int closeResult = CloseResultFor(state->buttonSet);
        if (closeResult != 0) {
            CompleteMessageDialog(state, closeResult);
        }
        return 0;
    }
    case WM_NCDESTROY:
        if (state != nullptr) {
            DeleteUiFont(state->font);
            if (state->iconBitmap != nullptr) {
                DeleteObject(state->iconBitmap);
            }
            if (state->backgroundBrush != nullptr) {
                DeleteObject(state->backgroundBrush);
            }
            state->font = nullptr;
            state->iconBitmap = nullptr;
            state->backgroundBrush = nullptr;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool RegisterMessageDialogClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &MessageDialogProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kMessageDialogClass;
    AssignWindowClassIcons(wc, instance);
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

} // namespace

int ShowMessageDialog(
    HWND owner,
    HINSTANCE instance,
    UINT dpi,
    bool dark,
    std::wstring_view title,
    std::wstring_view message,
    MessageDialogIcon icon,
    MessageDialogButtons buttons,
    int defaultResult) {
    if (instance == nullptr) {
        instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    }

    const UINT effectiveDpi = dpi != 0 ? dpi : (owner != nullptr ? GetDpiForWindow(owner) : GetDpiForSystem());
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    const DWORD exStyle = WS_EX_WINDOWEDGE | WS_EX_CONTROLPARENT;
    const int clientWidth = ScaleForDpi(430, effectiveDpi);
    const int margin = ScaleForDpi(18, effectiveDpi);
    const int iconSize = ScaleForDpi(32, effectiveDpi);
    const int iconGap = ScaleForDpi(14, effectiveDpi);
    const int contentTextWidth = clientWidth - (margin * 2) - (icon == MessageDialogIcon::None ? 0 : iconSize + iconGap);

    HFONT measureFont = CreateUiFontForDpi(effectiveDpi);
    const int messageHeight = MeasureMessageHeight(message, contentTextWidth, measureFont);
    DeleteUiFont(measureFont);

    const int contentHeight = std::max(icon == MessageDialogIcon::None ? 0 : iconSize, messageHeight);
    const int buttonHeight = ScaleForDpi(30, effectiveDpi);
    const int clientHeight = margin + contentHeight + ScaleForDpi(22, effectiveDpi) + buttonHeight + margin;

    RECT windowRect{0, 0, clientWidth, clientHeight};
    AdjustWindowRectExForDpi(&windowRect, style, FALSE, exStyle, effectiveDpi);
    const int windowWidth = windowRect.right - windowRect.left;
    const int windowHeight = windowRect.bottom - windowRect.top;
    const RECT anchor = OwnerOrMonitorRect(owner);
    const int x = anchor.left + ((anchor.right - anchor.left - windowWidth) / 2);
    const int y = anchor.top + ((anchor.bottom - anchor.top - windowHeight) / 2);

    if (!RegisterMessageDialogClass(instance)) {
        return MessageBoxW(
            owner,
            std::wstring(message).c_str(),
            std::wstring(title).c_str(),
            FallbackFlags(icon, buttons, defaultResult));
    }

    MessageDialogState state{};
    state.owner = owner;
    state.instance = instance;
    state.title = std::wstring(title);
    state.message = std::wstring(message);
    state.buttonSpecs = ButtonSpecsFor(buttons);
    state.icon = icon;
    state.buttonSet = buttons;
    state.dpi = effectiveDpi;
    state.messageHeight = messageHeight;
    state.result = CloseResultFor(buttons) != 0 ? CloseResultFor(buttons) : IDNO;
    state.defaultResult = defaultResult;
    state.dark = dark;

    HWND dialog = CreateWindowExW(
        exStyle,
        kMessageDialogClass,
        state.title.c_str(),
        style,
        x,
        y,
        windowWidth,
        windowHeight,
        owner,
        nullptr,
        instance,
        &state);
    if (dialog == nullptr) {
        return MessageBoxW(
            owner,
            state.message.c_str(),
            state.title.c_str(),
            FallbackFlags(icon, buttons, defaultResult));
    }

    if (owner != nullptr) {
        EnableWindow(owner, FALSE);
    }
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    // Run a nested modal loop so callers keep the same synchronous contract as
    // MessageBoxW while still using NativePad-owned rendering and colors.
    MSG msg{};
    while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (owner != nullptr) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }
    return state.result;
}

} // namespace NativePad
