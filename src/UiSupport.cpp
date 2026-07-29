#include "UiSupport.h"

#include "resource.h"
#include <wimukthi/win32_theme.hpp>

namespace NativePad {

namespace {

wimukthi::win32_theme::AttachOptions DialogThemeOptions() {
    wimukthi::win32_theme::AttachOptions options;
    options.menu_bar = false;
    options.erase_background = false;
    return options;
}

} // namespace

int ScaleForDpi(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi), USER_DEFAULT_SCREEN_DPI);
}

void EnableProcessDpiAwareness() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

HFONT CreateUiFontForDpi(UINT dpi) {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    BOOL loaded = SystemParametersInfoForDpi(
        SPI_GETNONCLIENTMETRICS,
        sizeof(metrics),
        &metrics,
        0,
        dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi);
    if (!loaded) {
        loaded = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    }

    HFONT font = loaded ? CreateFontIndirectW(&metrics.lfMessageFont) : nullptr;
    return font != nullptr ? font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

void DeleteUiFont(HFONT font) {
    if (font != nullptr && font != GetStockObject(DEFAULT_GUI_FONT)) {
        DeleteObject(font);
    }
}

HICON LoadNativePadIcon(HINSTANCE instance, int width, int height) {
    // The executable embeds a multi-size .ico; LoadImage lets Windows pick the
    // nearest resource for title bars, Alt+Tab, and shell surfaces.
    HICON icon = static_cast<HICON>(LoadImageW(
        instance,
        MAKEINTRESOURCEW(IDI_NATIVEPAD),
        IMAGE_ICON,
        width,
        height,
        LR_DEFAULTCOLOR | LR_SHARED));
    return icon != nullptr ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

void AssignWindowClassIcons(WNDCLASSEXW& wc, HINSTANCE instance) {
    wc.hIcon = LoadNativePadIcon(instance, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    wc.hIconSm = LoadNativePadIcon(instance, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
}

void ApplyWindowIcons(HWND hwnd, HINSTANCE instance) {
    // Dialog-style windows can otherwise inherit a generic small icon even when
    // the main frame uses the embedded resource, so set both icon slots.
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadNativePadIcon(instance, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON))));
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadNativePadIcon(instance, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON))));
}

ThemeColors ColorsForTheme(bool dark) {
    // Keep application chrome colors centralized so owner-draw menus, status bar,
    // dialogs, and editor background move together when dark mode changes.
    if (IsHighContrastMode()) {
        return {
            GetSysColor(COLOR_WINDOW),
            GetSysColor(COLOR_WINDOWTEXT),
            GetSysColor(COLOR_BTNFACE),
            GetSysColor(COLOR_BTNTEXT),
            GetSysColor(COLOR_WINDOWFRAME),
            GetSysColor(COLOR_MENU),
            GetSysColor(COLOR_HIGHLIGHT),
            GetSysColor(COLOR_HIGHLIGHT),
            GetSysColor(COLOR_MENUTEXT),
            GetSysColor(COLOR_GRAYTEXT),
            GetSysColor(COLOR_WINDOWFRAME),
            GetSysColor(COLOR_WINDOWFRAME),
            GetSysColor(COLOR_BTNFACE),
            GetSysColor(COLOR_BTNTEXT),
        };
    }

    if (dark) {
        return {
            RGB(30, 30, 30),
            RGB(238, 238, 238),
            RGB(28, 28, 28),
            RGB(150, 150, 150),
            RGB(54, 54, 54),
            RGB(31, 31, 31),
            RGB(49, 49, 49),
            RGB(64, 64, 64),
            RGB(238, 238, 238),
            RGB(140, 140, 140),
            RGB(42, 42, 42),
            RGB(54, 54, 54),
            RGB(38, 38, 38),
            RGB(222, 222, 222),
        };
    }

    return {
        RGB(255, 255, 255),
        RGB(0, 0, 0),
        RGB(247, 247, 247),
        RGB(96, 96, 96),
        RGB(220, 220, 220),
        RGB(250, 250, 250),
        RGB(229, 241, 251),
        RGB(204, 228, 247),
        RGB(0, 0, 0),
        RGB(120, 120, 120),
        RGB(210, 210, 210),
        RGB(210, 210, 210),
        RGB(240, 240, 240),
        RGB(0, 0, 0),
    };
}

std::wstring GetLastErrorText(DWORD error) {
    if (error == ERROR_SUCCESS) {
        return L"No error.";
    }

    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstring message = length > 0 && buffer != nullptr ? std::wstring(buffer, length) : L"Unknown error.";
    if (buffer != nullptr) {
        LocalFree(buffer);
    }

    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
        message.pop_back();
    }

    return message;
}

bool IsSystemDarkMode() {
    return wimukthi::win32_theme::system_prefers_dark();
}

bool IsHighContrastMode() {
    return wimukthi::win32_theme::is_high_contrast();
}

bool HandleThemeSettingChange(LPARAM lparam) {
    return wimukthi::win32_theme::handle_setting_change(lparam);
}

bool IsNativeThemeDark() {
    return wimukthi::win32_theme::is_dark();
}

void ApplyDarkFrame(HWND hwnd, bool dark) {
    (void)dark;
    wimukthi::win32_theme::apply_title_bar(hwnd);
}

void ApplyDarkControlTheme(HWND hwnd, bool dark) {
    (void)dark;
    wimukthi::win32_theme::apply_control(hwnd);
}

void SetControlFont(HWND control, HFONT font) {
    if (control != nullptr && font != nullptr) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

std::wstring ControlText(HWND control) {
    if (control == nullptr) {
        return {};
    }

    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }

    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

void SetControlText(HWND control, std::wstring_view text) {
    if (control != nullptr) {
        SetWindowTextW(control, std::wstring(text).c_str());
    }
}

DialogColors DialogColorsForTheme(bool dark) {
    // Custom dialog surfaces and framework-themed standard controls share this
    // palette so the application does not develop competing dark-mode shades.
    if (IsHighContrastMode()) {
        return {
            GetSysColor(COLOR_BTNFACE),
            GetSysColor(COLOR_BTNTEXT),
            GetSysColor(COLOR_WINDOW),
            GetSysColor(COLOR_HIGHLIGHT),
            GetSysColor(COLOR_HIGHLIGHTTEXT),
            GetSysColor(COLOR_GRAYTEXT),
            GetSysColor(COLOR_WINDOWFRAME),
            GetSysColor(COLOR_HOTLIGHT),
        };
    }

    if (dark) {
        return {
            RGB(31, 31, 31),
            RGB(238, 238, 238),
            RGB(30, 30, 30),
            RGB(55, 78, 112),
            RGB(255, 255, 255),
            RGB(150, 150, 150),
            RGB(62, 62, 62),
            RGB(78, 115, 158),
        };
    }

    return {
        RGB(240, 240, 240),
        RGB(0, 0, 0),
        RGB(255, 255, 255),
        RGB(0, 120, 215),
        RGB(255, 255, 255),
        RGB(120, 120, 120),
        RGB(170, 170, 170),
        RGB(0, 120, 215),
    };
}

bool ConfigureNativeTheme(bool requestedDark, bool followSystem) {
    using namespace wimukthi::win32_theme;

    const bool paletteDark = followSystem ? IsSystemDarkMode() : requestedDark;
    const ThemeColors chrome = ColorsForTheme(paletteDark);
    const DialogColors dialog = DialogColorsForTheme(paletteDark);

    Configuration configuration;
    configuration.mode =
        followSystem ? Mode::system : (requestedDark ? Mode::dark : Mode::light);
    configuration.use_custom_palette = !IsHighContrastMode();
    configuration.palette = {
        chrome.editorBackground,
        dialog.controlBackground,
        chrome.menuHot,
        dialog.background,
        paletteDark ? RGB(78, 36, 36) : RGB(255, 240, 240),
        dialog.text,
        chrome.menuDisabledText,
        dialog.disabledText,
        dialog.focusBorder,
        dialog.border,
        dialog.focusBorder,
        chrome.separator,
        dialog.selectionBackground,
        chrome.editorBackground,
        chrome.editorText,
        chrome.editorLineNumberSeparator,
        chrome.menuBackground,
        chrome.menuHot,
        chrome.menuText,
        chrome.menuBorder,
    };
    configure(configuration);
    return is_dark();
}

void ApplyThemedDialog(HWND dialog) {
    if (dialog != nullptr) {
        wimukthi::win32_theme::attach(dialog, DialogThemeOptions());
    }
}

void RefreshThemedDialog(HWND dialog) {
    if (dialog != nullptr) {
        wimukthi::win32_theme::refresh(dialog, DialogThemeOptions());
    }
}

int ShowThemedMessageBox(HWND owner, std::wstring_view text, std::wstring_view caption, UINT type) {
    const std::wstring textCopy(text);
    const std::wstring captionCopy(caption);
    return wimukthi::win32_theme::message_box(owner, textCopy.c_str(), captionCopy.c_str(), type);
}

bool MessageTargetsWindow(HWND hwnd, const MSG& message) {
    return hwnd != nullptr && (message.hwnd == hwnd || IsChild(hwnd, message.hwnd));
}

void CloseModalWindow(HWND dialog, HWND owner, HWND previousFocus) {
    // Restore the owner before destroying its active owned window. Destroying
    // first leaves USER32 with no enabled activation target, so the owner frame
    // briefly deactivates and activates again after EnableWindow.
    if (owner != nullptr && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
        if (previousFocus != nullptr &&
            IsWindow(previousFocus) &&
            (previousFocus == owner || IsChild(owner, previousFocus)) &&
            IsWindowEnabled(previousFocus)) {
            SetFocus(previousFocus);
        }
    }

    if (dialog != nullptr && IsWindow(dialog)) {
        DestroyWindow(dialog);
    }
}

} // namespace NativePad
