#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <strsafe.h>
#include <uxtheme.h>
#include <winver.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "AboutDialog.h"
#include "DefaultEditor.h"
#include "DocumentBuffer.h"
#include "EditorView.h"
#include "FileCodec.h"
#include "FindReplaceDialog.h"
#include "FontDialog.h"
#include "GoToDialog.h"
#include "LargeTextDocument.h"
#include "MappedTextDocument.h"
#include "MessageDialog.h"
#include "PopupMenu.h"
#include "Printing.h"
#include "RecoveryJournal.h"
#include "resource.h"
#include "Settings.h"
#include "TextFormat.h"
#include "UpdateChecker.h"
#include "UiSupport.h"

// Defined after the Windows headers so the fallbacks never rewrite the
// DWMWINDOWATTRIBUTE enumerators on SDKs that already provide these constants.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

using namespace NativePad;

namespace {

// main.cpp contains the Win32 shell around the editor: menus, dialogs, file I/O,
// printing, theming, DPI/layout, and application lifetime. The editor control
// itself lives in EditorView so painting and text navigation remain isolated.
constexpr wchar_t kWindowClass[] = L"NativePadWindow";
constexpr wchar_t kMenuStripClass[] = L"NativePadMenuStrip";
constexpr wchar_t kUntitled[] = L"Untitled";

// Posted after activation so the external-change prompt runs outside the
// WM_ACTIVATE handshake instead of showing a modal dialog inside it.
constexpr UINT WM_NATIVEPAD_FILE_CHANGE_CHECK = WM_APP + 306;

// Posted at creation so the crash-recovery prompt runs from the message loop,
// after any command-line file has been opened.
constexpr UINT WM_NATIVEPAD_RECOVERY_CHECK = WM_APP + 307;

constexpr UINT_PTR kTailTimerId = 1;
constexpr UINT kTailTimerIntervalMs = 1000;

constexpr UINT_PTR kRecoveryTimerId = 2;
constexpr UINT kRecoverySaveDelayMs = 3000;

constexpr size_t kMaxRecentFiles = ID_FILE_RECENT_LAST - ID_FILE_RECENT_FIRST + 1;

// Size plus last write time is enough to notice external edits without keeping
// a change-notification handle open on the containing directory.
struct FileStamp {
    ULONGLONG size{0};
    FILETIME writeTime{};
};

std::optional<FileStamp> QueryFileStamp(const std::wstring& path) {
    if (path.empty()) {
        return std::nullopt;
    }

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        return std::nullopt;
    }

    FileStamp stamp;
    stamp.size = (static_cast<ULONGLONG>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
    stamp.writeTime = attributes.ftLastWriteTime;
    return stamp;
}

bool SameFileStamp(const FileStamp& left, const FileStamp& right) {
    return left.size == right.size && CompareFileTime(&left.writeTime, &right.writeTime) == 0;
}

std::wstring BaseName(std::wstring_view path) {
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring_view::npos) {
        return std::wstring(path);
    }

    return std::wstring(path.substr(pos + 1));
}

bool TextMatchesAt(std::wstring_view text, std::wstring_view needle, size_t position, bool matchCase) {
    if (position + needle.size() > text.size()) {
        return false;
    }

    for (size_t i = 0; i < needle.size(); ++i) {
        wchar_t left = text[position + i];
        wchar_t right = needle[i];
        if (!matchCase) {
            left = static_cast<wchar_t>(std::towlower(left));
            right = static_cast<wchar_t>(std::towlower(right));
        }
        if (left != right) {
            return false;
        }
    }

    return true;
}

std::optional<size_t> FindForward(std::wstring_view text, std::wstring_view needle, size_t start, bool matchCase) {
    if (needle.empty() || needle.size() > text.size()) {
        return std::nullopt;
    }

    const size_t last = text.size() - needle.size();
    for (size_t position = std::min(start, text.size()); position <= last; ++position) {
        if (TextMatchesAt(text, needle, position, matchCase)) {
            return position;
        }
    }

    return std::nullopt;
}

std::wstring UserDateTimeText() {
    SYSTEMTIME now{};
    GetLocalTime(&now);

    wchar_t time[128]{};
    wchar_t date[128]{};
    const int timeLength = GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &now, nullptr, time, static_cast<int>(std::size(time)));
    const int dateLength = GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &now, nullptr, date, static_cast<int>(std::size(date)), nullptr);
    if (timeLength > 0 && dateLength > 0) {
        std::wstring result = time;
        result += L" ";
        result += date;
        return result;
    }

    wchar_t fallback[64]{};
    StringCchPrintfW(
        fallback,
        std::size(fallback),
        L"%02u:%02u %u/%u/%u",
        now.wHour,
        now.wMinute,
        now.wMonth,
        now.wDay,
        now.wYear);
    return fallback;
}

class AppWindow {
public:
    explicit AppWindow(HINSTANCE instance)
        : instance_(instance),
          dpi_(GetDpiForSystem()),
          darkMode_(IsSystemDarkMode()) {
        LoadPreferences();
    }

    ~AppWindow() {
        if (fileMenu_ != nullptr) {
            DestroyMenu(fileMenu_);
        }
        if (editMenu_ != nullptr) {
            DestroyMenu(editMenu_);
        }
        if (formatMenu_ != nullptr) {
            DestroyMenu(formatMenu_);
        }
        if (viewMenu_ != nullptr) {
            DestroyMenu(viewMenu_);
        }
        if (helpMenu_ != nullptr) {
            DestroyMenu(helpMenu_);
        }
        if (menuBackgroundBrush_ != nullptr) {
            DeleteObject(menuBackgroundBrush_);
        }
        if (uiFont_ != nullptr && uiFont_ != GetStockObject(DEFAULT_GUI_FONT)) {
            DeleteObject(uiFont_);
        }
    }

    bool Create(int showCommand) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &AppWindow::StaticWndProc;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        AssignWindowClassIcons(wc, instance_);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kWindowClass;

        if (RegisterClassExW(&wc) == 0) {
            ShowStartupMessage(GetLastErrorText(), MessageDialogIcon::Error);
            return false;
        }

        if (!NativePad::EditorView::Register(instance_)) {
            ShowStartupMessage(GetLastErrorText(), MessageDialogIcon::Error);
            return false;
        }

        if (!RegisterChildClasses()) {
            ShowStartupMessage(GetLastErrorText(), MessageDialogIcon::Error);
            return false;
        }

        const UINT systemDpi = GetDpiForSystem();
        int x = CW_USEDEFAULT;
        int y = CW_USEDEFAULT;
        int width = ScaleForDpi(1000, systemDpi);
        int height = ScaleForDpi(700, systemDpi);
        if (hasSavedWindowRect_) {
            x = savedWindowRect_.left;
            y = savedWindowRect_.top;
            width = savedWindowRect_.right - savedWindowRect_.left;
            height = savedWindowRect_.bottom - savedWindowRect_.top;
        }

        hwnd_ = CreateWindowExW(
            0,
            kWindowClass,
            L"NativePad",
            WS_OVERLAPPEDWINDOW,
            x,
            y,
            width,
            height,
            nullptr,
            nullptr,
            instance_,
            this);

        if (hwnd_ == nullptr) {
            ShowStartupMessage(GetLastErrorText(), MessageDialogIcon::Error);
            return false;
        }

        int actualShowCommand = showCommand;
        if (savedWindowMaximized_ && showCommand != SW_SHOWMINIMIZED && showCommand != SW_SHOWMINNOACTIVE && showCommand != SW_MINIMIZE) {
            actualShowCommand = SW_SHOWMAXIMIZED;
        }
        ShowWindow(hwnd_, actualShowCommand);
        // A restored rect can point at a monitor that no longer exists, so make
        // sure the window is not launched off-screen.
        EnsureWindowOnVisibleMonitor();
        UpdateWindow(hwnd_);
        return true;
    }

    HWND Hwnd() const {
        return hwnd_;
    }

    bool TranslateModelessDialog(MSG* message) const {
        if (findDialog_ == nullptr || message == nullptr) {
            return false;
        }

        if (MessageTargetsWindow(findDialog_, *message) && message->message == WM_KEYDOWN && message->wParam == VK_ESCAPE) {
            DestroyWindow(findDialog_);
            return true;
        }

        return IsDialogMessageW(findDialog_, message);
    }

    void OpenInitialFile(const std::wstring& path) {
        if (!path.empty()) {
            OpenDocument(path);
        }
    }

private:
    void LoadPreferences() {
        // NativePad stores only UI/editor preferences. Document metadata remains
        // per-file state so opening an ANSI file cannot change the default for a
        // later new document.
        if (auto forced = ReadSettingsDword(L"DarkModeForced")) {
            darkModeForced_ = *forced != 0;
        }
        if (darkModeForced_) {
            if (auto dark = ReadSettingsDword(L"DarkMode")) {
                darkMode_ = *dark != 0;
            }
        }

        if (auto wordWrap = ReadSettingsDword(L"WordWrap")) {
            preferredWordWrap_ = *wordWrap != 0;
        }
        if (auto lineNumbers = ReadSettingsDword(L"LineNumbers")) {
            preferredLineNumbers_ = *lineNumbers != 0;
        }
        if (auto visible = ReadSettingsDword(L"StatusBarVisible")) {
            statusBarVisible_ = *visible != 0;
        }

        if (auto family = ReadSettingsString(L"FontFamily"); family && !family->empty()) {
            preferredFont_.family = *family;
        }
        if (auto sizeTenths = ReadSettingsDword(L"FontSizeTenths"); sizeTenths && *sizeTenths >= 60 && *sizeTenths <= 720) {
            preferredFont_.sizeDips = static_cast<float>(*sizeTenths) / 10.0f;
        }
        if (auto weight = ReadSettingsDword(L"FontWeight")) {
            preferredFont_.weight = std::clamp<LONG>(static_cast<LONG>(*weight), FW_THIN, FW_HEAVY);
        }
        if (auto italic = ReadSettingsDword(L"FontItalic")) {
            preferredFont_.italic = *italic != 0;
        }

        const auto marginLeft = ReadSettingsInt(L"MarginLeft");
        const auto marginTop = ReadSettingsInt(L"MarginTop");
        const auto marginRight = ReadSettingsInt(L"MarginRight");
        const auto marginBottom = ReadSettingsInt(L"MarginBottom");
        if (marginLeft && marginTop && marginRight && marginBottom) {
            RECT margins{*marginLeft, *marginTop, *marginRight, *marginBottom};
            if (margins.left >= 0 && margins.top >= 0 && margins.right >= 0 && margins.bottom >= 0 &&
                margins.left <= 10000 && margins.top <= 10000 && margins.right <= 10000 && margins.bottom <= 10000) {
                pageMarginsThousandths_ = margins;
            }
        }

        const auto left = ReadSettingsInt(L"WindowLeft");
        const auto top = ReadSettingsInt(L"WindowTop");
        const auto right = ReadSettingsInt(L"WindowRight");
        const auto bottom = ReadSettingsInt(L"WindowBottom");
        if (left && top && right && bottom) {
            RECT rect{*left, *top, *right, *bottom};
            if (rect.right - rect.left >= 320 && rect.bottom - rect.top >= 240) {
                savedWindowRect_ = rect;
                hasSavedWindowRect_ = true;
            }
        }
        if (auto maximized = ReadSettingsDword(L"WindowMaximized")) {
            savedWindowMaximized_ = *maximized != 0;
        }

        for (size_t i = 0; i < kMaxRecentFiles; ++i) {
            const std::wstring name = L"RecentFile" + std::to_wstring(i);
            if (auto path = ReadSettingsString(name.c_str()); path && !path->empty()) {
                recentFiles_.push_back(*path);
            }
        }
    }

    void SavePreferences() const {
        WriteSettingsDword(L"DarkModeForced", darkModeForced_ ? 1u : 0u);
        WriteSettingsDword(L"DarkMode", darkMode_ ? 1u : 0u);
        WriteSettingsDword(L"WordWrap", editorView_.WordWrap() ? 1u : 0u);
        WriteSettingsDword(L"LineNumbers", editorView_.ShowLineNumbers() ? 1u : 0u);
        WriteSettingsDword(L"StatusBarVisible", statusBarVisible_ ? 1u : 0u);
        WriteSettingsInt(L"MarginLeft", pageMarginsThousandths_.left);
        WriteSettingsInt(L"MarginTop", pageMarginsThousandths_.top);
        WriteSettingsInt(L"MarginRight", pageMarginsThousandths_.right);
        WriteSettingsInt(L"MarginBottom", pageMarginsThousandths_.bottom);

        const NativePad::EditorFontSpec& font = editorView_.Font();
        WriteSettingsString(L"FontFamily", font.family);
        WriteSettingsDword(L"FontSizeTenths", static_cast<DWORD>(std::lround(font.sizeDips * 10.0f)));
        WriteSettingsDword(L"FontWeight", static_cast<DWORD>(font.weight));
        WriteSettingsDword(L"FontItalic", font.italic ? 1u : 0u);

        for (size_t i = 0; i < kMaxRecentFiles; ++i) {
            const std::wstring name = L"RecentFile" + std::to_wstring(i);
            WriteSettingsString(name.c_str(), i < recentFiles_.size() ? recentFiles_[i] : L"");
        }

        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (GetWindowPlacement(hwnd_, &placement)) {
            WriteSettingsInt(L"WindowLeft", placement.rcNormalPosition.left);
            WriteSettingsInt(L"WindowTop", placement.rcNormalPosition.top);
            WriteSettingsInt(L"WindowRight", placement.rcNormalPosition.right);
            WriteSettingsInt(L"WindowBottom", placement.rcNormalPosition.bottom);
            WriteSettingsDword(L"WindowMaximized", placement.showCmd == SW_SHOWMAXIMIZED ? 1u : 0u);
        }
    }

    bool RegisterChildClasses() const {
        WNDCLASSEXW menuClass{};
        menuClass.cbSize = sizeof(menuClass);
        menuClass.lpfnWndProc = &AppWindow::StaticMenuStripProc;
        menuClass.hInstance = instance_;
        menuClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        menuClass.lpszClassName = kMenuStripClass;

        if (RegisterClassExW(&menuClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        return true;
    }

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        AppWindow* app = nullptr;

        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<AppWindow*>(create->lpCreateParams);
            app->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        } else {
            app = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (app != nullptr) {
            return app->WndProc(message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static LRESULT CALLBACK StaticMenuStripProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        AppWindow* app = nullptr;

        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<AppWindow*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        } else {
            app = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (app != nullptr) {
            return app->MenuStripProc(hwnd, message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }


    LRESULT WndProc(UINT message, WPARAM wParam, LPARAM lParam) {
        // AppWindow owns all top-level UI messages. Child controls forward only
        // high-level notifications so document state changes stay centralized.
        switch (message) {
        case WM_CREATE:
            return OnCreate();
        case WM_SIZE:
            Layout();
            return 0;
        case WM_CONTEXTMENU:
            if (reinterpret_cast<HWND>(wParam) == editor_) {
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ShowEditorContextMenu(point);
                return 0;
            }
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        case WM_DPICHANGED:
            OnDpiChanged(HIWORD(wParam), reinterpret_cast<RECT*>(lParam));
            return 0;
        case NativePad::WM_EDITOR_CHANGED:
            dirty_ = true;
            ScheduleRecoverySave();
            UpdateTitle();
            UpdateStatus();
            return 0;
        case NativePad::WM_EDITOR_CURSOR_CHANGED:
        case NativePad::WM_EDITOR_ZOOM_CHANGED:
            UpdateStatus();
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) != WA_INACTIVE) {
                PostMessageW(hwnd_, WM_NATIVEPAD_FILE_CHANGE_CHECK, 0, 0);
            }
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        case WM_NATIVEPAD_FILE_CHANGE_CHECK:
            CheckExternalFileChange();
            return 0;
        case WM_NATIVEPAD_RECOVERY_CHECK:
            OfferRecoveredDocument();
            return 0;
        case WM_TIMER:
            if (wParam == kTailTimerId) {
                OnTailTimer();
                return 0;
            }
            if (wParam == kRecoveryTimerId) {
                PersistRecoverySnapshot();
                return 0;
            }
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        case WM_NATIVEPAD_PRINT_COMPLETE:
            OnPrintComplete(reinterpret_cast<PrintResult*>(lParam));
            return 0;
        case WM_NATIVEPAD_FIND_REPLACE:
            OnFindReplaceRequest(reinterpret_cast<FindReplaceDialogRequest*>(lParam));
            return 0;
        case WM_NATIVEPAD_UPDATE_CHECK_COMPLETE:
            OnUpdateCheckComplete(reinterpret_cast<UpdateCheckResult*>(lParam));
            return 0;
        case WM_NATIVEPAD_UPDATE_DOWNLOAD_COMPLETE:
            OnUpdateDownloadComplete(reinterpret_cast<UpdateDownloadResult*>(lParam));
            return 0;
        case WM_COMMAND:
            OnCommand(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
            return 0;
        case WM_NOTIFY:
            return OnNotify(reinterpret_cast<NMHDR*>(lParam));
        case WM_INITMENUPOPUP:
            UpdateMenuState(reinterpret_cast<HMENU>(wParam));
            return 0;
        case WM_MEASUREITEM:
            return OnMeasureItem(reinterpret_cast<MEASUREITEMSTRUCT*>(lParam));
        case WM_DRAWITEM:
            return OnDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        case WM_KEYDOWN:
            if (HandleMenuKeyDown(wParam)) {
                return 0;
            }
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        case WM_SYSKEYDOWN:
            if (HandleMenuSysKeyDown(wParam)) {
                return 0;
            }
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        case WM_SYSKEYUP:
            if (HandleMenuSysKeyUp(wParam)) {
                return 0;
            }
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        case WM_ERASEBKGND:
            EraseClientBackground(reinterpret_cast<HDC>(wParam));
            return 1;
        case WM_POWERBROADCAST:
            if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
                RefreshAfterDisplaySurfaceChange();
            }
            return TRUE;
        case WM_DISPLAYCHANGE:
            RefreshAfterDisplaySurfaceChange();
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                const HWND target = reinterpret_cast<HWND>(wParam);
                if (target == hwnd_ || target == status_ || target == menuStrip_) {
                    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                    return TRUE;
                }
            }
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            if (!darkModeForced_) {
                darkMode_ = IsSystemDarkMode();
                ApplyTheme();
            }
            return 0;
        case WM_DROPFILES:
            OnDropFiles(reinterpret_cast<HDROP>(wParam));
            return 0;
        case WM_CLOSE:
            if (ConfirmSaveIfDirty()) {
                // The user saved or knowingly discarded the document, so the
                // journal must not resurrect it on the next launch.
                CancelRecoveryJournal();
                SavePreferences();
                DestroyWindow(hwnd_);
            }
            return 0;
        case WM_DESTROY:
            if (findDialog_ != nullptr) {
                DestroyWindow(findDialog_);
                findDialog_ = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        }
    }

    LRESULT OnCreate() {
        // Build the window chrome first, then attach the editor view to the
        // current DocumentBuffer. Large files can later swap in MappedTextDocument.
        dpi_ = GetDpiForWindow(hwnd_);
        RefreshUiFont();
        BuildPopupMenus();

        menuStrip_ = CreateWindowExW(
            0,
            kMenuStripClass,
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0,
            0,
            0,
            0,
            hwnd_,
            nullptr,
            instance_,
            this);

        if (menuStrip_ == nullptr) {
            ShowAppMessage(L"Could not create the menu strip.", MessageDialogIcon::Error);
            return -1;
        }

        if (!editorView_.Create(hwnd_, instance_, &document_)) {
            ShowAppMessage(L"Could not create the DirectWrite editor view.", MessageDialogIcon::Error);
            return -1;
        }
        editor_ = editorView_.Hwnd();

        status_ = CreateWindowExW(
            0,
            L"STATIC",
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_OWNERDRAW,
            0,
            0,
            0,
            0,
            hwnd_,
            nullptr,
            instance_,
            nullptr);

        if (status_ == nullptr) {
            ShowAppMessage(L"Could not create the native status bar.", MessageDialogIcon::Error);
            return -1;
        }

        DragAcceptFiles(hwnd_, TRUE);

        editorView_.OnDpiChanged(dpi_);
        editorView_.SetFont(preferredFont_);
        editorView_.SetShowLineNumbers(preferredLineNumbers_);
        editorView_.SetWordWrap(preferredWordWrap_);
        ShowWindow(status_, statusBarVisible_ ? SW_SHOW : SW_HIDE);
        ApplyTheme();
        UpdateTitle();
        UpdateStatus();
        Layout();
        SetFocus(editor_);
        MaybeStartAutomaticUpdateCheck();
        PostMessageW(hwnd_, WM_NATIVEPAD_RECOVERY_CHECK, 0, 0);

        return 0;
    }

    void AppendMenuCommand(HMENU menu, UINT id) const {
        AppendMenuW(menu, MF_OWNERDRAW, id, reinterpret_cast<LPCWSTR>(static_cast<UINT_PTR>(id)));
    }

    void AppendMenuSeparator(HMENU menu) const {
        AppendMenuW(menu, MF_OWNERDRAW | MF_SEPARATOR, 0, nullptr);
    }

    void BuildPopupMenus() {
        // Menus are owner-drawn so dark mode can control backgrounds, separators,
        // accelerator text, and hover/pressed states consistently.
        fileMenu_ = CreatePopupMenu();
        RebuildFileMenu();

        editMenu_ = CreatePopupMenu();
        AppendMenuCommand(editMenu_, ID_EDIT_UNDO);
        AppendMenuCommand(editMenu_, ID_EDIT_REDO);
        AppendMenuSeparator(editMenu_);
        AppendMenuCommand(editMenu_, ID_EDIT_CUT);
        AppendMenuCommand(editMenu_, ID_EDIT_COPY);
        AppendMenuCommand(editMenu_, ID_EDIT_PASTE);
        AppendMenuCommand(editMenu_, ID_EDIT_DELETE);
        AppendMenuSeparator(editMenu_);
        AppendMenuCommand(editMenu_, ID_EDIT_FIND);
        AppendMenuCommand(editMenu_, ID_EDIT_FIND_NEXT);
        AppendMenuCommand(editMenu_, ID_EDIT_FIND_PREVIOUS);
        AppendMenuCommand(editMenu_, ID_EDIT_REPLACE);
        AppendMenuCommand(editMenu_, ID_EDIT_GO_TO);
        AppendMenuSeparator(editMenu_);
        AppendMenuCommand(editMenu_, ID_EDIT_SELECT_ALL);
        AppendMenuCommand(editMenu_, ID_EDIT_TIME_DATE);
        AppendMenuSeparator(editMenu_);
        AppendMenuCommand(editMenu_, ID_EDIT_DUPLICATE_LINE);
        AppendMenuCommand(editMenu_, ID_EDIT_DELETE_LINE);
        AppendMenuCommand(editMenu_, ID_EDIT_MOVE_LINE_UP);
        AppendMenuCommand(editMenu_, ID_EDIT_MOVE_LINE_DOWN);
        AppendMenuSeparator(editMenu_);
        AppendMenuCommand(editMenu_, ID_EDIT_ENABLE_LARGE_EDIT);

        formatMenu_ = CreatePopupMenu();
        AppendMenuCommand(formatMenu_, ID_FORMAT_WORD_WRAP);
        AppendMenuCommand(formatMenu_, ID_FORMAT_FONT);

        viewMenu_ = CreatePopupMenu();
        AppendMenuCommand(viewMenu_, ID_VIEW_ZOOM_IN);
        AppendMenuCommand(viewMenu_, ID_VIEW_ZOOM_OUT);
        AppendMenuCommand(viewMenu_, ID_VIEW_ZOOM_RESET);
        AppendMenuSeparator(viewMenu_);
        AppendMenuCommand(viewMenu_, ID_VIEW_LINE_NUMBERS);
        AppendMenuCommand(viewMenu_, ID_VIEW_STATUS_BAR);
        AppendMenuCommand(viewMenu_, ID_VIEW_DARK_MODE);
        AppendMenuSeparator(viewMenu_);
        AppendMenuCommand(viewMenu_, ID_VIEW_FOLLOW_TAIL);

        helpMenu_ = CreatePopupMenu();
        AppendMenuCommand(helpMenu_, ID_HELP_DEFAULT_EDITOR);
        AppendMenuSeparator(helpMenu_);
        AppendMenuCommand(helpMenu_, ID_HELP_ABOUT);

        menuEntries_ = {{
            {L"&File", fileMenu_, {}},
            {L"&Edit", editMenu_, {}},
            {L"F&ormat", formatMenu_, {}},
            {L"&View", viewMenu_, {}},
            {L"&Help", helpMenu_, {}},
        }};
    }

    void RebuildFileMenu() {
        while (GetMenuItemCount(fileMenu_) > 0) {
            DeleteMenu(fileMenu_, 0, MF_BYPOSITION);
        }

        AppendMenuCommand(fileMenu_, ID_FILE_NEW);
        AppendMenuCommand(fileMenu_, ID_FILE_OPEN);
        AppendMenuCommand(fileMenu_, ID_FILE_SAVE);
        AppendMenuCommand(fileMenu_, ID_FILE_SAVE_AS);
        AppendMenuSeparator(fileMenu_);
        AppendMenuCommand(fileMenu_, ID_FILE_PAGE_SETUP);
        AppendMenuCommand(fileMenu_, ID_FILE_PRINT);
        AppendMenuSeparator(fileMenu_);
        if (!recentFiles_.empty()) {
            for (size_t i = 0; i < recentFiles_.size(); ++i) {
                AppendMenuCommand(fileMenu_, static_cast<UINT>(ID_FILE_RECENT_FIRST + i));
            }
            AppendMenuCommand(fileMenu_, ID_FILE_RECENT_CLEAR);
            AppendMenuSeparator(fileMenu_);
        }
        AppendMenuCommand(fileMenu_, ID_FILE_EXIT);
    }

    void AddRecentFile(const std::wstring& path) {
        if (path.empty()) {
            return;
        }

        std::erase_if(recentFiles_, [&](const std::wstring& existing) {
            return _wcsicmp(existing.c_str(), path.c_str()) == 0;
        });
        recentFiles_.insert(recentFiles_.begin(), path);
        if (recentFiles_.size() > kMaxRecentFiles) {
            recentFiles_.resize(kMaxRecentFiles);
        }
        RebuildFileMenu();
    }

    void OpenRecentFile(size_t index) {
        if (index >= recentFiles_.size()) {
            return;
        }

        const std::wstring path = recentFiles_[index];
        if (!ConfirmSaveIfDirty()) {
            return;
        }

        if (!OpenDocument(path)) {
            // The open failed (the error dialog already ran); drop the stale
            // entry so the menu only lists files that can still be opened.
            std::erase_if(recentFiles_, [&](const std::wstring& existing) {
                return _wcsicmp(existing.c_str(), path.c_str()) == 0;
            });
            RebuildFileMenu();
        }
    }

    void OnCommand(WORD id, WORD notification, HWND source) {
        UNREFERENCED_PARAMETER(notification);
        if (source == editor_) {
            return;
        }

        if (id >= ID_FILE_RECENT_FIRST && id <= ID_FILE_RECENT_LAST) {
            OpenRecentFile(static_cast<size_t>(id - ID_FILE_RECENT_FIRST));
            return;
        }

        switch (id) {
        case ID_FILE_NEW:
            NewDocument();
            break;
        case ID_FILE_OPEN:
            OpenDocumentFromDialog();
            break;
        case ID_FILE_SAVE:
            SaveDocument(false);
            break;
        case ID_FILE_SAVE_AS:
            SaveDocument(true);
            break;
        case ID_FILE_PAGE_SETUP:
            ShowPageSetupDialog();
            break;
        case ID_FILE_PRINT:
            ShowPrintDialog();
            break;
        case ID_FILE_EXIT:
            SendMessageW(hwnd_, WM_CLOSE, 0, 0);
            break;
        case ID_EDIT_UNDO:
            editorView_.Undo();
            break;
        case ID_EDIT_REDO:
            editorView_.Redo();
            break;
        case ID_EDIT_CUT:
            editorView_.Cut();
            break;
        case ID_EDIT_COPY:
            editorView_.Copy();
            break;
        case ID_EDIT_PASTE:
            editorView_.Paste();
            break;
        case ID_EDIT_DELETE:
            editorView_.Delete();
            break;
        case ID_EDIT_FIND:
            ShowFindDialog();
            break;
        case ID_EDIT_FIND_NEXT:
            FindNext(true);
            break;
        case ID_EDIT_FIND_PREVIOUS:
            FindNext(false);
            break;
        case ID_EDIT_REPLACE:
            ShowReplaceDialog();
            break;
        case ID_EDIT_GO_TO:
            ShowGoToDialog();
            break;
        case ID_EDIT_SELECT_ALL:
            editorView_.SelectAll();
            break;
        case ID_EDIT_TIME_DATE:
            editorView_.InsertAtCaret(UserDateTimeText());
            break;
        case ID_EDIT_DUPLICATE_LINE:
            editorView_.DuplicateLine();
            break;
        case ID_EDIT_DELETE_LINE:
            editorView_.DeleteLine();
            break;
        case ID_EDIT_MOVE_LINE_UP:
            editorView_.MoveLineUp();
            break;
        case ID_EDIT_MOVE_LINE_DOWN:
            editorView_.MoveLineDown();
            break;
        case ID_EDIT_ENABLE_LARGE_EDIT:
            EnableLargeFileEditing();
            break;
        case ID_FILE_RECENT_CLEAR:
            recentFiles_.clear();
            RebuildFileMenu();
            break;
        case ID_FORMAT_WORD_WRAP:
            ToggleWordWrap();
            break;
        case ID_FORMAT_FONT:
            ShowFontChooser();
            break;
        case ID_VIEW_STATUS_BAR:
            statusBarVisible_ = !statusBarVisible_;
            ShowWindow(status_, statusBarVisible_ ? SW_SHOW : SW_HIDE);
            Layout();
            break;
        case ID_VIEW_LINE_NUMBERS:
            editorView_.SetShowLineNumbers(!editorView_.ShowLineNumbers());
            InvalidateRect(menuStrip_, nullptr, FALSE);
            SetFocus(editor_);
            break;
        case ID_VIEW_DARK_MODE:
            darkMode_ = !darkMode_;
            darkModeForced_ = true;
            ApplyTheme();
            break;
        case ID_VIEW_FOLLOW_TAIL:
            ToggleFollowTail();
            break;
        case ID_VIEW_ZOOM_IN:
            editorView_.SetZoomPercent(editorView_.ZoomPercent() + 10);
            break;
        case ID_VIEW_ZOOM_OUT:
            editorView_.SetZoomPercent(editorView_.ZoomPercent() - 10);
            break;
        case ID_VIEW_ZOOM_RESET:
            editorView_.SetZoomPercent(100);
            break;
        case ID_HELP_DEFAULT_EDITOR:
            SetAsDefaultEditor();
            break;
        case ID_HELP_CHECK_UPDATES:
            BeginUpdateCheck(UpdateCheckKind::Manual);
            break;
        case ID_HELP_AUTO_UPDATE:
            ToggleAutomaticUpdateChecks();
            break;
        case ID_HELP_ABOUT:
            ShowAboutDialog(hwnd_, instance_, dpi_, darkMode_);
            break;
        default:
            break;
        }
    }

    void ShowRoadmapMessage(const wchar_t* command) const {
        std::wstring message = command;
        message += L" is on the classic Notepad parity roadmap and has not been implemented yet.";
        ShowAppMessage(message, MessageDialogIcon::Information);
    }

    int ShowAppMessage(
        std::wstring_view message,
        MessageDialogIcon icon,
        std::wstring_view title = L"NativePad",
        MessageDialogButtons buttons = MessageDialogButtons::Ok,
        int defaultResult = IDOK) const {
        return ShowMessageDialog(hwnd_, instance_, dpi_, darkMode_, title, message, icon, buttons, defaultResult);
    }

    int ShowStartupMessage(std::wstring_view message, MessageDialogIcon icon) const {
        return ShowMessageDialog(nullptr, instance_, dpi_, darkMode_, L"NativePad", message, icon);
    }

    void SetFindBuffer(std::wstring_view text) {
        const size_t count = std::min(text.size(), findBuffer_.size() - 1);
        std::copy_n(text.data(), count, findBuffer_.data());
        findBuffer_[count] = L'\0';
    }

    void SetReplaceBuffer(std::wstring_view text) {
        const size_t count = std::min(text.size(), replaceBuffer_.size() - 1);
        std::copy_n(text.data(), count, replaceBuffer_.data());
        replaceBuffer_[count] = L'\0';
    }

    std::wstring SelectedTextForFind() const {
        if (!editorView_.HasSelection()) {
            return {};
        }

        const std::wstring selected = editorView_.SelectedText();
        if (selected.find_first_of(L"\r\n") != std::wstring::npos) {
            return {};
        }

        return selected;
    }

    void SeedFindBufferFromSelectionOrLast() {
        std::wstring seed = SelectedTextForFind();
        if (seed.empty()) {
            seed = lastFindText_;
        }
        if (!seed.empty()) {
            SetFindBuffer(seed);
        }
    }

    void ShowFindDialog() {
        if (findDialog_ != nullptr && !replaceDialogOpen_) {
            SetForegroundWindow(findDialog_);
            SetFocus(findDialog_);
            return;
        }

        if (findDialog_ != nullptr) {
            DestroyWindow(findDialog_);
            findDialog_ = nullptr;
            replaceDialogOpen_ = false;
        }

        SeedFindBufferFromSelectionOrLast();
        findDialog_ = ShowFindReplaceDialog(
            hwnd_,
            instance_,
            dpi_,
            darkMode_,
            false,
            findBuffer_.data(),
            replaceBuffer_.data(),
            lastFindMatchCase_,
            lastFindDown_);
        replaceDialogOpen_ = false;
    }

    void ShowReplaceDialog() {
        if (readOnlyPreview_) {
            return;
        }

        if (findDialog_ != nullptr && replaceDialogOpen_) {
            SetForegroundWindow(findDialog_);
            SetFocus(findDialog_);
            return;
        }

        if (findDialog_ != nullptr) {
            DestroyWindow(findDialog_);
            findDialog_ = nullptr;
            replaceDialogOpen_ = false;
        }

        SeedFindBufferFromSelectionOrLast();
        if (!lastReplaceText_.empty()) {
            SetReplaceBuffer(lastReplaceText_);
        }

        findDialog_ = ShowFindReplaceDialog(
            hwnd_,
            instance_,
            dpi_,
            darkMode_,
            true,
            findBuffer_.data(),
            replaceBuffer_.data(),
            lastFindMatchCase_,
            lastFindDown_);
        replaceDialogOpen_ = findDialog_ != nullptr;
    }

    void OnFindReplaceRequest(FindReplaceDialogRequest* request) {
        if (request == nullptr) {
            return;
        }

        if (request->action == FindReplaceDialogAction::Closed) {
            if (request->dialog == findDialog_) {
                findDialog_ = nullptr;
                replaceDialogOpen_ = false;
            }
            return;
        }

        lastFindText_ = request->findText;
        lastReplaceText_ = request->replaceText;
        lastFindMatchCase_ = request->matchCase;
        lastFindDown_ = request->down;
        SetFindBuffer(lastFindText_);
        SetReplaceBuffer(lastReplaceText_);
        if (lastFindText_.empty()) {
            return;
        }

        if (request->action == FindReplaceDialogAction::ReplaceAll) {
            ReplaceAll();
            return;
        }

        if (request->action == FindReplaceDialogAction::Replace) {
            if (SelectionMatchesFind()) {
                editorView_.InsertAtCaret(lastReplaceText_);
            }
            FindNext(lastFindDown_, false);
            return;
        }

        if (request->action == FindReplaceDialogAction::FindNext) {
            FindNext(lastFindDown_, false);
        }
    }

    void ShowFindNotFound() const {
        std::wstring message = L"Cannot find \"";
        message += lastFindText_;
        message += L"\"";
        ShowAppMessage(message, MessageDialogIcon::Information);
    }

    void FindNext(bool down, bool focusEditor = true) {
        // Editable documents can search a materialized string. Mapped documents
        // search the mapped file directly and return backend-native positions.
        lastFindDown_ = down;
        if (lastFindText_.empty()) {
            ShowFindDialog();
            return;
        }

        if (mappedDocument_ != nullptr) {
            const size_t start = down ? editorView_.SelectionEnd() : editorView_.SelectionStart();
            std::optional<NativePad::MappedTextDocument::Match> match = mappedDocument_->Find(lastFindText_, start, down, lastFindMatchCase_);
            if (!match) {
                match = mappedDocument_->Find(lastFindText_, down ? 0 : mappedDocument_->Length(), down, lastFindMatchCase_);
            }

            if (!match) {
                ShowFindNotFound();
                return;
            }

            editorView_.SelectRange(match->position, match->length);
            if (focusEditor) {
                SetFocus(editor_);
            }
            return;
        }

        if (document_.Length() == 0) {
            ShowFindNotFound();
            return;
        }

        // Search the piece table directly instead of materializing the whole
        // document on every Find Next.
        const size_t start = down ? editorView_.SelectionEnd() : editorView_.SelectionStart();
        std::optional<size_t> match = document_.Find(lastFindText_, start, down, lastFindMatchCase_);
        if (!match) {
            match = document_.Find(lastFindText_, down ? 0 : document_.Length(), down, lastFindMatchCase_);
        }

        if (!match) {
            ShowFindNotFound();
            return;
        }

        editorView_.SelectRange(*match, lastFindText_.size());
        if (focusEditor) {
            SetFocus(editor_);
        }
    }

    bool SelectionMatchesFind() const {
        if (mappedDocument_ != nullptr || lastFindText_.empty() || !editorView_.HasSelection()) {
            return false;
        }

        const size_t start = editorView_.SelectionStart();
        const size_t length = editorView_.SelectionEnd() - start;
        if (length != lastFindText_.size()) {
            return false;
        }

        // Compare only the selected span instead of materializing the whole
        // document; the selection length already equals the search term length.
        const std::wstring selected = document_.TextRange(start, length);
        return TextMatchesAt(selected, lastFindText_, 0, lastFindMatchCase_);
    }

    void ReplaceAll() {
        // Replace All is intentionally limited to editable documents because it
        // rebuilds the full string and then resets the piece table.
        if (readOnlyPreview_ || lastFindText_.empty()) {
            return;
        }

        const std::wstring text = document_.Text();
        // Build the result in a single linear pass. Repeated in-place
        // std::wstring::replace shifts the tail on every hit (quadratic when the
        // replacement length differs); appending unchanged spans avoids that.
        std::wstring result;
        result.reserve(text.size());
        size_t replacements = 0;
        size_t searchStart = 0;
        size_t copied = 0;
        while (auto match = FindForward(text, lastFindText_, searchStart, lastFindMatchCase_)) {
            result.append(text, copied, *match - copied);
            result.append(lastReplaceText_);
            copied = *match + lastFindText_.size();
            searchStart = copied;
            ++replacements;
        }

        if (replacements == 0) {
            ShowFindNotFound();
            return;
        }

        result.append(text, copied, text.size() - copied);
        SetEditorText(result);
        dirty_ = true;
        UpdateTitle();
        UpdateStatus();
        SetFocus(editor_);

        std::wstring message = L"Replaced ";
        message += std::to_wstring(replacements);
        message += replacements == 1 ? L" occurrence." : L" occurrences.";
        ShowAppMessage(message, MessageDialogIcon::Information);
    }

    void ShowGoToDialog() {
        const size_t lineCount = std::max<size_t>(1, editorView_.LineCount());
        auto line = ShowGoToLineDialog(hwnd_, instance_, dpi_, darkMode_, editorView_.Line() + 1, lineCount);
        if (!line) {
            return;
        }

        editorView_.GoToLine(*line);
        SetFocus(editor_);
        UpdateStatus();
    }

    void SetAsDefaultEditor() {
        // Registration is per-user; Windows then asks the user to confirm the
        // default in its own UI, which PromptSetDefaultEditor opens.
        std::wstring error;
        if (!PromptSetDefaultEditor(hwnd_, instance_, error)) {
            std::wstring message = L"Could not register NativePad as a text editor:\n\n";
            message += error;
            ShowAppMessage(message, MessageDialogIcon::Error);
        }
    }

    void ToggleAutomaticUpdateChecks() {
        const bool enabled = !AutomaticUpdateChecksEnabled();
        SetAutomaticUpdateChecksEnabled(enabled);
        InvalidateRect(menuStrip_, nullptr, FALSE);
        if (enabled) {
            BeginUpdateCheck(UpdateCheckKind::Manual);
        }
    }

    void MaybeStartAutomaticUpdateCheck() {
        if (AutomaticUpdateCheckDue()) {
            BeginUpdateCheck(UpdateCheckKind::Automatic);
        }
    }

    void BeginUpdateCheck(UpdateCheckKind kind) {
        if (updateCheckInProgress_) {
            if (kind == UpdateCheckKind::Manual) {
                ShowAppMessage(L"An update check is already running.", MessageDialogIcon::Information);
            }
            return;
        }

        updateCheckInProgress_ = true;
        if (kind == UpdateCheckKind::Manual) {
            SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        }
        StartUpdateCheck(hwnd_, instance_, kind);
    }

    void BeginUpdateDownload(const UpdateInfo& update) {
        if (updateDownloadInProgress_) {
            ShowAppMessage(L"An update download is already running.", MessageDialogIcon::Information);
            return;
        }

        updateDownloadInProgress_ = true;
        ShowAppMessage(L"NativePad will download the installer in the background.", MessageDialogIcon::Information);
        StartUpdateDownload(hwnd_, update);
    }

    void OnUpdateCheckComplete(UpdateCheckResult* rawResult) {
        std::unique_ptr<UpdateCheckResult> result(rawResult);
        updateCheckInProgress_ = false;
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        if (!result) {
            return;
        }

        const bool manual = result->kind == UpdateCheckKind::Manual;
        if (!result->success) {
            if (manual) {
                std::wstring message = L"Could not check for updates:\n\n" + result->message;
                ShowAppMessage(message, MessageDialogIcon::Error);
            }
            return;
        }

        if (!result->updateAvailable) {
            if (manual) {
                const std::wstring message = L"NativePad is up to date.\n\nInstalled version: " + CurrentExecutableVersion(instance_);
                ShowAppMessage(message, MessageDialogIcon::Information);
            }
            return;
        }

        std::wstring message = L"NativePad ";
        message += result->update.version;
        message += L" is available.\n\nDownload and install it now?";
        if (ShowAppMessage(message, MessageDialogIcon::Question, L"NativePad Update", MessageDialogButtons::YesNo, IDYES) == IDYES) {
            BeginUpdateDownload(result->update);
        }
    }

    void OnUpdateDownloadComplete(UpdateDownloadResult* rawResult) {
        std::unique_ptr<UpdateDownloadResult> result(rawResult);
        updateDownloadInProgress_ = false;
        if (!result) {
            return;
        }

        if (!result->success) {
            std::wstring message = L"Could not download the NativePad update:\n\n" + result->message;
            ShowAppMessage(message, MessageDialogIcon::Error, L"NativePad Update");
            return;
        }

        std::wstring message = L"NativePad ";
        message += result->update.version;
        message += L" has been downloaded.\n\nSave any open work and run the installer now?";
        if (ShowAppMessage(message, MessageDialogIcon::Question, L"NativePad Update", MessageDialogButtons::YesNo, IDYES) != IDYES) {
            return;
        }

        if (!ConfirmSaveIfDirty()) {
            return;
        }

        LaunchDownloadedInstaller(result->installerPath);
    }

    void LaunchDownloadedInstaller(const std::wstring& installerPath) {
        const auto launchResult = reinterpret_cast<INT_PTR>(ShellExecuteW(
            hwnd_,
            L"runas",
            installerPath.c_str(),
            nullptr,
            nullptr,
            SW_SHOWNORMAL));
        if (launchResult <= 32) {
            std::wstring message = L"Could not launch the installer:\n\n";
            message += GetLastErrorText(static_cast<DWORD>(launchResult));
            ShowAppMessage(message, MessageDialogIcon::Error, L"NativePad Update");
            return;
        }

        SavePreferences();
        DestroyWindow(hwnd_);
    }

    void ToggleWordWrap() {
        editorView_.SetWordWrap(!editorView_.WordWrap());
        UpdateStatus();
        InvalidateRect(menuStrip_, nullptr, FALSE);
        SetFocus(editor_);
    }

    void ShowFontChooser() {
        auto font = ShowFontDialog(hwnd_, instance_, editorView_.Font(), dpi_, darkMode_);
        if (!font) {
            return;
        }

        editorView_.SetFont(*font);
        UpdateStatus();
        SetFocus(editor_);
    }

    void ShowPageSetupDialog() {
        PAGESETUPDLGW pageSetup{};
        pageSetup.lStructSize = sizeof(pageSetup);
        pageSetup.hwndOwner = hwnd_;
        pageSetup.Flags = PSD_INTHOUSANDTHSOFINCHES | PSD_MARGINS;
        pageSetup.rtMargin = pageMarginsThousandths_;

        if (PageSetupDlgW(&pageSetup)) {
            pageMarginsThousandths_ = pageSetup.rtMargin;
        }
    }

    void ShowPrintDialog() {
        if (readOnlyPreview_) {
            const wchar_t* message = IsMappedLargeFile()
                                         ? L"Printing is disabled for read-only mapped large files."
                                         : L"Printing is disabled for read-only large-file previews.";
            ShowAppMessage(message, MessageDialogIcon::Information);
            return;
        }

        PRINTDLGW print{};
        print.lStructSize = sizeof(print);
        print.hwndOwner = hwnd_;
        print.Flags = PD_RETURNDC | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
        print.nFromPage = 1;
        print.nToPage = 1;
        print.nMinPage = 1;
        print.nMaxPage = 0xFFFF;
        print.nCopies = 1;

        if (!PrintDlgW(&print)) {
            return;
        }

        PrintJob job{};
        job.owner = hwnd_;
        job.printerDc = print.hDC;
        job.documentName = currentPath_.empty() ? L"Untitled - NativePad" : currentPath_;
        job.text = EditorText();
        job.font = LogFontFromEditorFont(editorView_.Font(), static_cast<UINT>(std::max(1, GetDeviceCaps(print.hDC, LOGPIXELSY))));
        job.marginsThousandths = pageMarginsThousandths_;
        job.wordWrap = editorView_.WordWrap();

        if (print.hDevMode != nullptr) {
            GlobalFree(print.hDevMode);
        }
        if (print.hDevNames != nullptr) {
            GlobalFree(print.hDevNames);
        }

        statusText_ = L"Printing in background...";
        InvalidateRect(status_, nullptr, FALSE);
        StartPrintWorker(std::move(job));
    }

    void OnPrintComplete(PrintResult* rawResult) {
        std::unique_ptr<PrintResult> result(rawResult);
        if (!result) {
            return;
        }

        if (!result->success) {
            const std::wstring message = L"Could not print document:\n\n" + result->message;
            ShowAppMessage(message, MessageDialogIcon::Error);
        }

        UpdateStatus();
    }

    LRESULT OnNotify(NMHDR* header) {
        UNREFERENCED_PARAMETER(header);
        return 0;
    }

    LRESULT MenuStripProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_PAINT:
            PaintMenuStrip(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_KEYDOWN:
            if (HandleMenuKeyDown(wParam)) {
                return 0;
            }
            break;
        case WM_SYSKEYDOWN:
            if (HandleMenuSysKeyDown(wParam)) {
                return 0;
            }
            break;
        case WM_SYSKEYUP:
            if (HandleMenuSysKeyUp(wParam)) {
                return 0;
            }
            break;
        case WM_KILLFOCUS:
            if (activeMenu_ < 0) {
                ExitMenuKeyboardMode(false);
            }
            return 0;
        case WM_MOUSEMOVE:
            OnMenuMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSELEAVE:
            menuMouseTracking_ = false;
            hotMenu_ = -1;
            InvalidateRect(menuStrip_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            OnMenuClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    HFONT UiFont() const {
        return uiFont_ != nullptr ? uiFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }

    void RefreshUiFont() {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        BOOL loaded = SystemParametersInfoForDpi(
            SPI_GETNONCLIENTMETRICS,
            sizeof(metrics),
            &metrics,
            0,
            dpi_);
        if (!loaded) {
            loaded = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
        }

        HFONT nextFont = loaded ? CreateFontIndirectW(&metrics.lfMenuFont) : nullptr;
        if (nextFont == nullptr) {
            nextFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }

        if (uiFont_ != nullptr && uiFont_ != GetStockObject(DEFAULT_GUI_FONT)) {
            DeleteObject(uiFont_);
        }
        uiFont_ = nextFont;

        if (menuStrip_ != nullptr) {
            SendMessageW(menuStrip_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        }
        if (status_ != nullptr) {
            SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        }
    }

    void OnDpiChanged(UINT dpi, const RECT* suggestedRect) {
        dpi_ = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
        if (suggestedRect != nullptr) {
            SetWindowPos(
                hwnd_,
                nullptr,
                suggestedRect->left,
                suggestedRect->top,
                suggestedRect->right - suggestedRect->left,
                suggestedRect->bottom - suggestedRect->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }

        RefreshUiFont();
        editorView_.OnDpiChanged(dpi_);
        Layout();
        UpdateStatus();
        InvalidateRect(menuStrip_, nullptr, TRUE);
        InvalidateRect(status_, nullptr, TRUE);
    }

    int Scale(int value) const {
        return ScaleForDpi(value, dpi_);
    }

    int MenuStripHeight() const {
        return Scale(24);
    }

    int StatusBarHeight() const {
        return Scale(23);
    }

    int PopupMenuItemHeight() const {
        return Scale(24);
    }

    static std::wstring EscapeMenuText(std::wstring text) {
        size_t i = 0;
        while ((i = text.find(L'&', i)) != std::wstring::npos) {
            text.insert(i, 1, L'&');
            i += 2;
        }
        return text;
    }

    static std::wstring CompactPathForMenu(const std::wstring& path) {
        constexpr size_t kMaxLength = 48;
        if (path.size() <= kMaxLength) {
            return path;
        }
        return path.substr(0, 18) + L"..." + path.substr(path.size() - (kMaxLength - 19));
    }

    std::wstring MenuTextFor(UINT id) const {
        if (id >= ID_FILE_RECENT_FIRST && id <= ID_FILE_RECENT_LAST) {
            const size_t index = id - ID_FILE_RECENT_FIRST;
            if (index < recentFiles_.size()) {
                return L"&" + std::to_wstring(index + 1) + L"  " + EscapeMenuText(CompactPathForMenu(recentFiles_[index]));
            }
            return L"";
        }

        switch (id) {
        case ID_FILE_NEW:
            return L"&New\tCtrl+N";
        case ID_FILE_OPEN:
            return L"&Open...\tCtrl+O";
        case ID_FILE_SAVE:
            return L"&Save\tCtrl+S";
        case ID_FILE_SAVE_AS:
            return L"Save &As...\tCtrl+Shift+S";
        case ID_FILE_PAGE_SETUP:
            return L"Page Set&up...";
        case ID_FILE_PRINT:
            return L"&Print...\tCtrl+P";
        case ID_FILE_EXIT:
            return L"E&xit";
        case ID_FILE_RECENT_CLEAR:
            return L"&Clear Recent Files";
        case ID_EDIT_UNDO:
            return L"&Undo\tCtrl+Z";
        case ID_EDIT_REDO:
            return L"&Redo\tCtrl+Y";
        case ID_EDIT_CUT:
            return L"Cu&t\tCtrl+X";
        case ID_EDIT_COPY:
            return L"&Copy\tCtrl+C";
        case ID_EDIT_PASTE:
            return L"&Paste\tCtrl+V";
        case ID_EDIT_DELETE:
            return L"De&lete\tDel";
        case ID_EDIT_FIND:
            return L"&Find...\tCtrl+F";
        case ID_EDIT_FIND_NEXT:
            return L"Find &Next\tF3";
        case ID_EDIT_FIND_PREVIOUS:
            return L"Find Pre&vious\tShift+F3";
        case ID_EDIT_REPLACE:
            return L"&Replace...\tCtrl+H";
        case ID_EDIT_GO_TO:
            return L"&Go To...\tCtrl+G";
        case ID_EDIT_SELECT_ALL:
            return L"Select &All\tCtrl+A";
        case ID_EDIT_TIME_DATE:
            return L"Time/&Date\tF5";
        case ID_EDIT_DUPLICATE_LINE:
            return L"D&uplicate Line\tCtrl+Shift+D";
        case ID_EDIT_DELETE_LINE:
            return L"Delete Li&ne\tCtrl+Shift+K";
        case ID_EDIT_MOVE_LINE_UP:
            return L"Move Line U&p\tAlt+Up";
        case ID_EDIT_MOVE_LINE_DOWN:
            return L"Move Line Do&wn\tAlt+Down";
        case ID_EDIT_ENABLE_LARGE_EDIT:
            return L"Enable Large-File &Editing";
        case ID_FORMAT_WORD_WRAP:
            return L"&Word Wrap";
        case ID_FORMAT_FONT:
            return L"&Font...";
        case ID_VIEW_STATUS_BAR:
            return L"&Status Bar";
        case ID_VIEW_LINE_NUMBERS:
            return L"&Line Numbers";
        case ID_VIEW_DARK_MODE:
            return L"&Dark Mode";
        case ID_VIEW_FOLLOW_TAIL:
            return L"Follow &Tail\tF6";
        case ID_VIEW_ZOOM_IN:
            return L"Zoom &In\tCtrl+Plus";
        case ID_VIEW_ZOOM_OUT:
            return L"Zoom &Out\tCtrl+Minus";
        case ID_VIEW_ZOOM_RESET:
            return L"&Restore Default Zoom\tCtrl+0";
        case ID_HELP_CHECK_UPDATES:
            return L"Check for &Updates...";
        case ID_HELP_AUTO_UPDATE:
            return L"Check &Automatically";
        case ID_HELP_DEFAULT_EDITOR:
            return L"Set as &Default Editor...";
        case ID_HELP_ABOUT:
            return L"&About NativePad";
        default:
            return L"";
        }
    }

    static std::wstring StripMenuMnemonic(std::wstring text) {
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == L'&') {
                if (i + 1 < text.size() && text[i + 1] == L'&') {
                    text.erase(i, 1);
                } else {
                    text.erase(i, 1);
                    --i;
                }
            }
        }

        return text;
    }

    std::pair<std::wstring, std::wstring> SplitMenuText(UINT id) const {
        std::wstring text = StripMenuMnemonic(MenuTextFor(id));
        const size_t tab = text.find(L'\t');
        if (tab == std::wstring::npos) {
            return {text, L""};
        }

        return {text.substr(0, tab), text.substr(tab + 1)};
    }

    int MeasureTextWidth(HDC hdc, const std::wstring& text) const {
        SIZE size{};
        if (!text.empty()) {
            GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
        }
        return size.cx;
    }

    void LayoutMenuEntries(HDC hdc, int width) {
        int left = 0;
        const int horizontalPadding = Scale(12);
        const int height = MenuStripHeight();

        for (auto& entry : menuEntries_) {
            const std::wstring label = StripMenuMnemonic(entry.text);
            SIZE size{};
            GetTextExtentPoint32W(hdc, label.c_str(), static_cast<int>(label.size()), &size);
            const int itemWidth = size.cx + horizontalPadding * 2;
            entry.rect = {left, 0, std::min(width, left + itemWidth), height};
            left += itemWidth;
        }
    }

    void PaintMenuStrip(HWND hwnd) {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (hdc == nullptr) {
            return;
        }

        RECT client{};
        GetClientRect(hwnd, &client);
        const ThemeColors colors = ColorsForTheme(darkMode_);

        HBRUSH background = CreateSolidBrush(colors.menuBackground);
        FillRect(hdc, &client, background);
        DeleteObject(background);

        HFONT font = UiFont();

        HGDIOBJ oldFont = SelectObject(hdc, font);
        SetBkMode(hdc, TRANSPARENT);
        LayoutMenuEntries(hdc, client.right - client.left);

        for (size_t i = 0; i < menuEntries_.size(); ++i) {
            const MenuEntry& entry = menuEntries_[i];
            COLORREF itemBackground = colors.menuBackground;
            if (activeMenu_ == static_cast<int>(i)) {
                itemBackground = colors.menuPressed;
            } else if (hotMenu_ == static_cast<int>(i)) {
                itemBackground = colors.menuHot;
            }

            HBRUSH itemBrush = CreateSolidBrush(itemBackground);
            FillRect(hdc, &entry.rect, itemBrush);
            DeleteObject(itemBrush);

            RECT textRect = entry.rect;
            SetTextColor(hdc, colors.menuText);
            const UINT prefixFlag = menuKeyboardCuesVisible_ ? 0u : DT_HIDEPREFIX;
            DrawTextW(hdc, entry.text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | prefixFlag);
        }

        HPEN borderPen = CreatePen(PS_SOLID, 1, colors.menuBorder);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        MoveToEx(hdc, client.left, client.bottom - 1, nullptr);
        LineTo(hdc, client.right, client.bottom - 1);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        SelectObject(hdc, oldFont);
        EndPaint(hwnd, &ps);
    }

    void DrawStatusBar(HDC hdc, RECT client) {
        const ThemeColors colors = ColorsForTheme(darkMode_);

        HBRUSH background = CreateSolidBrush(colors.statusBackground);
        FillRect(hdc, &client, background);
        DeleteObject(background);

        HPEN borderPen = CreatePen(PS_SOLID, 1, colors.separator);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        MoveToEx(hdc, client.left, client.top, nullptr);
        LineTo(hdc, client.right, client.top);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        HFONT font = UiFont();

        HGDIOBJ oldFont = SelectObject(hdc, font);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, colors.statusText);
        RECT textRect = client;
        textRect.left += Scale(6);
        DrawTextW(hdc, statusText_.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, oldFont);
    }

    int HitTestMenu(int x, int y) const {
        POINT point{x, y};
        for (size_t i = 0; i < menuEntries_.size(); ++i) {
            if (PtInRect(&menuEntries_[i].rect, point)) {
                return static_cast<int>(i);
            }
        }

        return -1;
    }

    void TrackMenuMouseLeave() {
        if (menuMouseTracking_) {
            return;
        }

        TRACKMOUSEEVENT event{};
        event.cbSize = sizeof(event);
        event.dwFlags = TME_LEAVE;
        event.hwndTrack = menuStrip_;
        if (TrackMouseEvent(&event)) {
            menuMouseTracking_ = true;
        }
    }

    void OnMenuMouseMove(int x, int y) {
        TrackMenuMouseLeave();
        const int hit = HitTestMenu(x, y);
        if (hit != hotMenu_) {
            hotMenu_ = hit;
            InvalidateRect(menuStrip_, nullptr, FALSE);
        }
    }

    void OnMenuClick(int x, int y) {
        const int hit = HitTestMenu(x, y);
        if (hit >= 0) {
            ShowPopupMenu(hit);
        }
    }

    int MenuIndexForMnemonic(WPARAM key) const {
        switch (static_cast<wchar_t>(std::towupper(static_cast<wint_t>(key)))) {
        case L'F':
            return 0;
        case L'E':
            return 1;
        case L'O':
            return 2;
        case L'V':
            return 3;
        case L'H':
            return 4;
        default:
            return -1;
        }
    }

    int CurrentKeyboardMenuIndex() const {
        if (keyboardMenuIndex_ >= 0 && keyboardMenuIndex_ < static_cast<int>(menuEntries_.size())) {
            return keyboardMenuIndex_;
        }
        return 0;
    }

    void SetKeyboardMenuIndex(int index) {
        if (menuEntries_.empty()) {
            return;
        }

        const int count = static_cast<int>(menuEntries_.size());
        while (index < 0) {
            index += count;
        }
        keyboardMenuIndex_ = index % count;
        hotMenu_ = keyboardMenuIndex_;
        InvalidateRect(menuStrip_, nullptr, FALSE);
    }

    void EnterMenuKeyboardMode(int index = 0) {
        // Native menu bars reveal keyboard cues and take menu focus when Alt/F10
        // is pressed. The custom strip mirrors that behavior explicitly because
        // USER32 cannot do it for owner-painted top-level labels.
        menuKeyboardCuesVisible_ = true;
        SetKeyboardMenuIndex(index);
        if (menuStrip_ != nullptr) {
            SetFocus(menuStrip_);
        }
    }

    void ExitMenuKeyboardMode(bool restoreEditorFocus) {
        const bool changed = menuKeyboardCuesVisible_ || keyboardMenuIndex_ >= 0 || hotMenu_ >= 0;
        menuKeyboardCuesVisible_ = false;
        keyboardMenuIndex_ = -1;
        if (activeMenu_ < 0) {
            hotMenu_ = -1;
        }
        if (changed) {
            InvalidateRect(menuStrip_, nullptr, FALSE);
        }
        if (restoreEditorFocus && editor_ != nullptr) {
            SetFocus(editor_);
        }
    }

    bool HandleMenuKeyDown(WPARAM key) {
        if (key == VK_F10) {
            EnterMenuKeyboardMode(CurrentKeyboardMenuIndex());
            return true;
        }

        if (!menuKeyboardCuesVisible_) {
            return false;
        }

        switch (key) {
        case VK_ESCAPE:
            ExitMenuKeyboardMode(true);
            return true;
        case VK_LEFT:
            SetKeyboardMenuIndex(CurrentKeyboardMenuIndex() - 1);
            return true;
        case VK_RIGHT:
            SetKeyboardMenuIndex(CurrentKeyboardMenuIndex() + 1);
            return true;
        case VK_DOWN:
        case VK_RETURN:
        case VK_SPACE:
            ShowPopupMenu(CurrentKeyboardMenuIndex());
            return true;
        default:
            if (OpenMenuFromKey(key)) {
                return true;
            }
            return GetFocus() == menuStrip_;
        }
    }

    bool HandleMenuSysKeyDown(WPARAM key) {
        if (key == VK_MENU || key == VK_F10) {
            EnterMenuKeyboardMode(CurrentKeyboardMenuIndex());
            return true;
        }
        return OpenMenuFromKey(key);
    }

    bool HandleMenuSysKeyUp(WPARAM key) const {
        return key == VK_MENU && menuKeyboardCuesVisible_;
    }

    bool OpenMenuFromKey(WPARAM key) {
        const int index = MenuIndexForMnemonic(key);
        if (index < 0) {
            return false;
        }

        menuKeyboardCuesVisible_ = true;
        keyboardMenuIndex_ = index;
        ShowPopupMenu(index);
        return true;
    }

    bool RegisterCustomPopupMenuClass() const {
        auto registerClass = [](const WNDCLASSEXW& wc) {
            return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        };

        WNDCLASSEXW menuClass{};
        menuClass.cbSize = sizeof(menuClass);
        menuClass.lpfnWndProc = &CustomPopupMenuProc;
        menuClass.hInstance = instance_;
        menuClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        menuClass.hbrBackground = nullptr;
        menuClass.lpszClassName = kPopupMenuClass;
        if (!registerClass(menuClass)) {
            return false;
        }

        WNDCLASSEXW shadowClass{};
        shadowClass.cbSize = sizeof(shadowClass);
        shadowClass.lpfnWndProc = &PopupShadowProc;
        shadowClass.hInstance = instance_;
        shadowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        shadowClass.hbrBackground = nullptr;
        shadowClass.lpszClassName = kPopupShadowClass;
        return registerClass(shadowClass);
    }

    std::vector<PopupMenuItem> SnapshotPopupMenuItems(HMENU menu) const {
        std::vector<PopupMenuItem> items;
        const int count = GetMenuItemCount(menu);
        if (count <= 0) {
            return items;
        }

        items.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            MENUITEMINFOW info{};
            info.cbSize = sizeof(info);
            info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE;
            if (!GetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &info)) {
                continue;
            }

            PopupMenuItem item{};
            item.separator = (info.fType & MFT_SEPARATOR) != 0;
            item.id = item.separator ? 0 : info.wID;
            item.enabled = (info.fState & (MFS_DISABLED | MFS_GRAYED)) == 0;
            item.checked = (info.fState & MFS_CHECKED) != 0;
            if (!item.separator) {
                auto [label, shortcut] = SplitMenuText(item.id);
                item.label = std::move(label);
                item.shortcut = std::move(shortcut);
            }
            items.push_back(std::move(item));
        }

        return items;
    }

    SIZE LayoutCustomPopupMenuItems(std::vector<PopupMenuItem>& items) const {
        HDC hdc = GetDC(hwnd_);
        HFONT font = UiFont();
        HGDIOBJ oldFont = hdc != nullptr ? SelectObject(hdc, font) : nullptr;

        int width = Scale(190);
        for (const PopupMenuItem& item : items) {
            if (item.separator) {
                continue;
            }

            const int itemWidth = Scale(44) + (hdc != nullptr ? MeasureTextWidth(hdc, item.label) : 0) +
                                  Scale(36) + (hdc != nullptr ? MeasureTextWidth(hdc, item.shortcut) : 0);
            width = std::max(width, itemWidth);
        }

        if (hdc != nullptr) {
            SelectObject(hdc, oldFont);
            ReleaseDC(hwnd_, hdc);
        }

        const int border = 1;
        int y = border;
        for (PopupMenuItem& item : items) {
            const int height = item.separator ? Scale(9) : PopupMenuItemHeight();
            item.rect = {border, y, width - border, y + height};
            y += height;
        }

        return SIZE{width, y + border};
    }

    POINT ClampPopupMenuPosition(POINT position, SIZE size) const {
        HMONITOR monitor = MonitorFromPoint(position, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) {
            return position;
        }

        position.x = std::min(position.x, info.rcWork.right - size.cx);
        position.y = std::min(position.y, info.rcWork.bottom - size.cy);
        position.x = std::max(position.x, info.rcWork.left);
        position.y = std::max(position.y, info.rcWork.top);
        return position;
    }

    UINT ShowCustomPopupMenu(HMENU menu, int x, int y) {
        if (menu == nullptr || !RegisterCustomPopupMenuClass()) {
            return 0;
        }

        PopupMenuWindowState state{};
        state.owner = hwnd_;
        state.dpi = dpi_;
        state.font = UiFont();
        state.dark = darkMode_;
        state.items = SnapshotPopupMenuItems(menu);
        if (state.items.empty()) {
            return 0;
        }

        const SIZE size = LayoutCustomPopupMenuItems(state.items);
        const POINT position = ClampPopupMenuPosition(POINT{x, y}, size);
        state.shadow = CreatePopupShadowWindow(hwnd_, instance_, position, size, dpi_);

        HWND popup = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            kPopupMenuClass,
            nullptr,
            WS_POPUP | WS_CLIPSIBLINGS,
            position.x,
            position.y,
            size.cx,
            size.cy,
            hwnd_,
            nullptr,
            instance_,
            &state);
        if (popup == nullptr) {
            if (state.shadow != nullptr) {
                DestroyWindow(state.shadow);
            }
            return 0;
        }
        if (state.shadow != nullptr) {
            SetWindowPos(state.shadow, popup, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }

        // A small modal message loop gives the custom popup native menu behavior
        // without using USER32's popup window, whose border cannot be darkened
        // reliably across all menu positions.
        //
        // The popup is deliberately non-activating: native menus keep the owner
        // window active while they are open, so keyboard handling is intercepted
        // in this local loop instead of moving focus to the popup.
        ShowWindow(popup, SW_SHOWNOACTIVATE);
        UpdateWindow(popup);
        SetCapture(popup);

        MSG message{};
        while (IsWindow(popup)) {
            const BOOL result = GetMessageW(&message, nullptr, 0, 0);
            if (result <= 0) {
                if (result == 0) {
                    PostQuitMessage(static_cast<int>(message.wParam));
                }
                break;
            }

            if (RouteCustomPopupKey(popup, message)) {
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        return state.command;
    }

    void ShowPopupMenu(int index) {
        if (index < 0 || index >= static_cast<int>(menuEntries_.size())) {
            return;
        }

        MenuEntry& entry = menuEntries_[static_cast<size_t>(index)];
        if (entry.menu == nullptr) {
            return;
        }

        activeMenu_ = index;
        hotMenu_ = index;
        InvalidateRect(menuStrip_, nullptr, FALSE);
        UpdateWindow(menuStrip_);
        UpdateMenuState(entry.menu);

        RECT rect = entry.rect;
        MapWindowPoints(menuStrip_, HWND_DESKTOP, reinterpret_cast<POINT*>(&rect), 2);
        SetForegroundWindow(hwnd_);

        const UINT command = ShowCustomPopupMenu(entry.menu, rect.left, rect.bottom);

        const bool repaintAfterClose = activeMenu_ >= 0 || hotMenu_ >= 0 || keyboardMenuIndex_ >= 0 || menuKeyboardCuesVisible_;
        activeMenu_ = -1;
        hotMenu_ = -1;
        menuMouseTracking_ = false;
        ExitMenuKeyboardMode(true);
        if (repaintAfterClose) {
            InvalidateRect(menuStrip_, nullptr, FALSE);
        }

        if (command != 0) {
            OnCommand(static_cast<WORD>(command), 0, nullptr);
        }
    }

    void ShowEditorContextMenu(POINT point) {
        if (point.x == -1 && point.y == -1) {
            RECT rect{};
            GetWindowRect(editor_, &rect);
            point.x = rect.left + Scale(24);
            point.y = rect.top + Scale(24);
        }

        SetFocus(editor_);
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr) {
            return;
        }

        AppendMenuCommand(menu, ID_EDIT_UNDO);
        AppendMenuCommand(menu, ID_EDIT_REDO);
        AppendMenuSeparator(menu);
        AppendMenuCommand(menu, ID_EDIT_CUT);
        AppendMenuCommand(menu, ID_EDIT_COPY);
        AppendMenuCommand(menu, ID_EDIT_PASTE);
        AppendMenuCommand(menu, ID_EDIT_DELETE);
        AppendMenuSeparator(menu);
        AppendMenuCommand(menu, ID_EDIT_FIND);
        AppendMenuCommand(menu, ID_EDIT_REPLACE);
        AppendMenuSeparator(menu);
        AppendMenuCommand(menu, ID_EDIT_SELECT_ALL);

        ApplyMenuBackgroundTo(menu);
        UpdateMenuState(menu);
        SetForegroundWindow(hwnd_);
        const UINT command = ShowCustomPopupMenu(menu, point.x, point.y);
        DestroyMenu(menu);

        if (command != 0) {
            OnCommand(static_cast<WORD>(command), 0, nullptr);
        }
    }

    void ApplyMenuBackgroundTo(HMENU menu) const {
        if (menu == nullptr || menuBackgroundBrush_ == nullptr) {
            return;
        }
        MENUINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = MIM_BACKGROUND;
        info.hbrBack = menuBackgroundBrush_;
        SetMenuInfo(menu, &info);
    }

    void ApplyMenuBackground() {
        if (menuBackgroundBrush_ != nullptr) {
            DeleteObject(menuBackgroundBrush_);
            menuBackgroundBrush_ = nullptr;
        }

        const ThemeColors colors = ColorsForTheme(darkMode_);
        menuBackgroundBrush_ = CreateSolidBrush(colors.menuBackground);

        ApplyMenuBackgroundTo(fileMenu_);
        ApplyMenuBackgroundTo(editMenu_);
        ApplyMenuBackgroundTo(formatMenu_);
        ApplyMenuBackgroundTo(viewMenu_);
        ApplyMenuBackgroundTo(helpMenu_);
    }

    LRESULT OnMeasureItem(MEASUREITEMSTRUCT* measure) const {
        if (measure == nullptr || measure->CtlType != ODT_MENU) {
            return FALSE;
        }

        HDC hdc = GetDC(hwnd_);
        HFONT font = UiFont();

        HGDIOBJ oldFont = SelectObject(hdc, font);
        if (measure->itemID == 0) {
            measure->itemWidth = static_cast<UINT>(Scale(190));
            measure->itemHeight = static_cast<UINT>(Scale(9));
            SelectObject(hdc, oldFont);
            ReleaseDC(hwnd_, hdc);
            return TRUE;
        }

        auto [label, shortcut] = SplitMenuText(measure->itemID);
        const int width = Scale(44) + MeasureTextWidth(hdc, label) + Scale(36) + MeasureTextWidth(hdc, shortcut);
        measure->itemWidth = static_cast<UINT>(std::max(width, Scale(190)));
        measure->itemHeight = static_cast<UINT>(PopupMenuItemHeight());
        SelectObject(hdc, oldFont);
        ReleaseDC(hwnd_, hdc);
        return TRUE;
    }

    LRESULT OnDrawItem(DRAWITEMSTRUCT* draw) {
        if (draw == nullptr) {
            return FALSE;
        }

        if (draw->CtlType == ODT_STATIC && draw->hwndItem == status_) {
            DrawStatusBar(draw->hDC, draw->rcItem);
            return TRUE;
        }

        if (draw->CtlType != ODT_MENU) {
            return FALSE;
        }

        const ThemeColors colors = ColorsForTheme(darkMode_);
        const bool selected = (draw->itemState & ODS_SELECTED) != 0;
        const bool disabled = (draw->itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
        const bool checked = (draw->itemState & ODS_CHECKED) != 0;
        const COLORREF backgroundColor = selected ? colors.menuHot : colors.menuBackground;
        HBRUSH background = CreateSolidBrush(backgroundColor);
        FillRect(draw->hDC, &draw->rcItem, background);
        DeleteObject(background);

        if (draw->itemID == 0) {
            const int midY = draw->rcItem.top + ((draw->rcItem.bottom - draw->rcItem.top) / 2);
            HPEN separatorPen = CreatePen(PS_SOLID, 1, colors.separator);
            HGDIOBJ oldPen = SelectObject(draw->hDC, separatorPen);
            MoveToEx(draw->hDC, draw->rcItem.left + Scale(4), midY, nullptr);
            LineTo(draw->hDC, draw->rcItem.right - Scale(4), midY);
            SelectObject(draw->hDC, oldPen);
            DeleteObject(separatorPen);
            return TRUE;
        }

        HFONT font = UiFont();

        HGDIOBJ oldFont = SelectObject(draw->hDC, font);
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, disabled ? colors.menuDisabledText : colors.menuText);

        if (checked) {
            HPEN checkPen = CreatePen(PS_SOLID, Scale(2), disabled ? colors.menuDisabledText : colors.menuText);
            HGDIOBJ oldPen = SelectObject(draw->hDC, checkPen);
            const int left = draw->rcItem.left + Scale(9);
            const int midY = draw->rcItem.top + ((draw->rcItem.bottom - draw->rcItem.top) / 2);
            MoveToEx(draw->hDC, left, midY, nullptr);
            LineTo(draw->hDC, left + Scale(4), midY + Scale(4));
            LineTo(draw->hDC, left + Scale(12), midY - Scale(5));
            SelectObject(draw->hDC, oldPen);
            DeleteObject(checkPen);
        }

        auto [label, shortcut] = SplitMenuText(draw->itemID);
        RECT labelRect = draw->rcItem;
        labelRect.left += Scale(30);
        labelRect.right -= Scale(14);

        RECT shortcutRect = labelRect;
        shortcutRect.left += Scale(40);

        DrawTextW(draw->hDC, label.c_str(), -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (!shortcut.empty()) {
            DrawTextW(draw->hDC, shortcut.c_str(), -1, &shortcutRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        SelectObject(draw->hDC, oldFont);
        return TRUE;
    }

    size_t DocumentLength() const noexcept {
        if (largeDocument_ != nullptr) {
            return largeDocument_->Length();
        }
        return mappedDocument_ != nullptr ? mappedDocument_->Length() : document_.Length();
    }

    bool IsMappedLargeFile() const noexcept {
        return mappedDocument_ != nullptr;
    }

    bool IsLargeEditable() const noexcept {
        return largeDocument_ != nullptr;
    }

    void UpdateMenuState(HMENU menu) const {
        // Menus are updated immediately before display so selection, read-only
        // mapped files, clipboard state, and word-wrap restrictions are current.
        if (menu == nullptr) {
            return;
        }

        // Tail following reloads the buffer from disk, so edits (and saves that
        // would race the writer) stay disabled while it is active.
        const bool canEdit = !readOnlyPreview_ && !followTail_;
        const bool hasText = DocumentLength() > 0;
        const bool canDelete = canEdit && (editorView_.HasSelection() || editorView_.CaretPosition() < DocumentLength());

        EnableMenuItem(menu, ID_EDIT_UNDO, MF_BYCOMMAND | (editorView_.CanUndo() ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_REDO, MF_BYCOMMAND | (editorView_.CanRedo() ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_CUT, MF_BYCOMMAND | (canEdit && editorView_.HasSelection() ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_COPY, MF_BYCOMMAND | (editorView_.HasSelection() ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_PASTE, MF_BYCOMMAND | (canEdit && IsClipboardFormatAvailable(CF_UNICODETEXT) ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_DELETE, MF_BYCOMMAND | (canDelete ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_FIND, MF_BYCOMMAND | (hasText ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_FIND_NEXT, MF_BYCOMMAND | (hasText ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_FIND_PREVIOUS, MF_BYCOMMAND | (hasText ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_REPLACE, MF_BYCOMMAND | (canEdit && hasText ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_GO_TO, MF_BYCOMMAND | (hasText ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_SELECT_ALL, MF_BYCOMMAND | (hasText ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_TIME_DATE, MF_BYCOMMAND | (canEdit ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_DUPLICATE_LINE, MF_BYCOMMAND | (canEdit ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_DELETE_LINE, MF_BYCOMMAND | (canEdit ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_MOVE_LINE_UP, MF_BYCOMMAND | (canEdit ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_EDIT_MOVE_LINE_DOWN, MF_BYCOMMAND | (canEdit ? MF_ENABLED : MF_GRAYED));
        // Enabling editing only applies to a read-only mapped large file.
        EnableMenuItem(menu, ID_EDIT_ENABLE_LARGE_EDIT, MF_BYCOMMAND | (IsMappedLargeFile() ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_SAVE, MF_BYCOMMAND | (canEdit ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_SAVE_AS, MF_BYCOMMAND | (canEdit ? MF_ENABLED : MF_GRAYED));
        // Printing is not supported for read-only previews or editable large
        // files (whole-document pagination would materialize the entire file).
        EnableMenuItem(menu, ID_FILE_PRINT, MF_BYCOMMAND | (!readOnlyPreview_ && !IsLargeEditable() ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(menu, ID_FORMAT_WORD_WRAP, MF_BYCOMMAND | (editorView_.WordWrap() ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menu, ID_VIEW_LINE_NUMBERS, MF_BYCOMMAND | (editorView_.ShowLineNumbers() ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menu, ID_VIEW_STATUS_BAR, MF_BYCOMMAND | (statusBarVisible_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menu, ID_VIEW_DARK_MODE, MF_BYCOMMAND | (darkMode_ ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(menu, ID_VIEW_FOLLOW_TAIL, MF_BYCOMMAND | (!currentPath_.empty() ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(menu, ID_VIEW_FOLLOW_TAIL, MF_BYCOMMAND | (followTail_ ? MF_CHECKED : MF_UNCHECKED));

        // Reflect the current default-editor state, but only query the shell when
        // the Help menu is actually opening so other menus stay cheap.
        if (menu == helpMenu_) {
            CheckMenuItem(menu, ID_HELP_DEFAULT_EDITOR, MF_BYCOMMAND | (IsDefaultEditor() ? MF_CHECKED : MF_UNCHECKED));
        }
    }

    void Layout() const {
        // The editor consumes all space between the custom menu strip and status
        // bar. Both heights are DPI-scaled elsewhere.
        if (menuStrip_ == nullptr || editor_ == nullptr || status_ == nullptr) {
            return;
        }

        RECT client{};
        GetClientRect(hwnd_, &client);
        const int width = static_cast<int>(client.right - client.left);
        const int menuHeight = MenuStripHeight();
        const int statusHeight = statusBarVisible_ ? StatusBarHeight() : 0;
        const int editorHeight = std::max(0, static_cast<int>((client.bottom - client.top) - menuHeight - statusHeight));

        MoveWindow(menuStrip_, 0, 0, width, menuHeight, TRUE);
        MoveWindow(editor_, 0, menuHeight, width, editorHeight, TRUE);
        MoveWindow(status_, 0, menuHeight + editorHeight, width, statusHeight, TRUE);
    }

    void ApplyTheme() {
        // Apply theme changes to native windows first, then repaint owner-draw
        // surfaces that do not automatically pick up SetWindowTheme changes.
        const ThemeColors colors = ColorsForTheme(darkMode_);
        ApplyDarkFrame(hwnd_, darkMode_);
        ApplyDarkControlTheme(editor_, darkMode_);
        ApplyDarkControlTheme(status_, darkMode_);

        editorView_.SetTheme({
            colors.editorBackground,
            colors.editorText,
            darkMode_ ? RGB(60, 92, 140) : RGB(173, 214, 255),
            colors.editorText,
            colors.editorLineNumberBackground,
            colors.editorLineNumberText,
            colors.editorLineNumberSeparator,
        });

        ApplyMenuBackground();
        InvalidateRect(menuStrip_, nullptr, TRUE);
        InvalidateRect(editor_, nullptr, TRUE);
        InvalidateRect(status_, nullptr, TRUE);
    }

    void EraseClientBackground(HDC hdc) const {
        if (hdc == nullptr) {
            return;
        }

        RECT client{};
        GetClientRect(hwnd_, &client);
        const ThemeColors colors = ColorsForTheme(darkMode_);
        HBRUSH background = CreateSolidBrush(colors.editorBackground);
        FillRect(hdc, &client, background);
        DeleteObject(background);
    }

    void EnsureWindowOnVisibleMonitor() {
        // When the monitor a window sits on is disconnected, Windows does not
        // always pull the window back onto a surviving display. Detect a window
        // that no longer intersects any monitor and recenter it on the primary
        // monitor's work area so it cannot become stranded off-screen.
        if (hwnd_ == nullptr || IsIconic(hwnd_)) {
            return;
        }

        if (MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONULL) != nullptr) {
            return;
        }

        MONITORINFO primary{};
        primary.cbSize = sizeof(primary);
        if (!GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &primary)) {
            return;
        }

        const RECT work = primary.rcWork;
        const int workWidth = work.right - work.left;
        const int workHeight = work.bottom - work.top;

        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        const bool maximized = GetWindowPlacement(hwnd_, &placement) && placement.showCmd == SW_SHOWMAXIMIZED;

        RECT frame{};
        GetWindowRect(hwnd_, &frame);
        int newWidth = std::clamp<int>(frame.right - frame.left, 320, workWidth);
        int newHeight = std::clamp<int>(frame.bottom - frame.top, 240, workHeight);
        const int left = work.left + std::max(0, (workWidth - newWidth) / 2);
        const int top = work.top + std::max(0, (workHeight - newHeight) / 2);

        if (maximized) {
            // A maximized window ignores position changes, so restore it, move
            // the restored frame onto the primary monitor, then re-maximize
            // there.
            ShowWindow(hwnd_, SW_RESTORE);
        }
        SetWindowPos(hwnd_, nullptr, left, top, newWidth, newHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        if (maximized) {
            ShowWindow(hwnd_, SW_SHOWMAXIMIZED);
        }
    }

    void RefreshAfterDisplaySurfaceChange() {
        // After sleep or display-driver resets, D2D HWND targets can keep a
        // stale or white back buffer until forced through a full repaint cycle.
        EnsureWindowOnVisibleMonitor();
        if (!darkModeForced_) {
            darkMode_ = IsSystemDarkMode();
        }
        ApplyTheme();
        editorView_.ResetDeviceResources();
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }

    void UpdateTitle() const {
        std::wstring title;
        if (dirty_) {
            title += L"*";
        }

        title += currentPath_.empty() ? kUntitled : BaseName(currentPath_);
        if (IsMappedLargeFile()) {
            title += L" [Read-only mapped]";
        } else if (IsLargeEditable()) {
            title += L" [Large file]";
        } else if (readOnlyPreview_) {
            title += L" [Read-only preview]";
        }
        if (followTail_) {
            title += L" [Tail]";
        }
        title += L" - NativePad";
        SetWindowTextW(hwnd_, title.c_str());
    }

    std::wstring EditorText() const {
        if (mappedDocument_ != nullptr || largeDocument_ != nullptr) {
            return {};
        }
        return document_.Text();
    }

    void SetEditorText(const std::wstring& text, bool readOnlyPreview = false) {
        mappedDocument_.reset();
        largeDocument_.reset();
        document_.Reset(text);
        readOnlyPreview_ = readOnlyPreview;
        editorView_.SetDocument(&document_);
        editorView_.SetReadOnly(readOnlyPreview_);
    }

    void SetMappedEditorDocument(std::unique_ptr<NativePad::MappedTextDocument> document) {
        // The mapped backend is view/search-only. Reset the other backends so
        // accidental save/replace paths cannot operate on stale text.
        document_.Reset();
        largeDocument_.reset();
        mappedDocument_ = std::move(document);
        readOnlyPreview_ = true;
        previewDecodedByteCount_ = 0;
        editorView_.SetMappedDocument(mappedDocument_.get());
        editorView_.SetReadOnly(true);
    }

    void SetLargeEditorDocument(std::unique_ptr<NativePad::LargeTextDocument> document) {
        // The editable large-file backend keeps the file mapped read-only and
        // stores edits in a piece table over an in-memory add buffer.
        document_.Reset();
        mappedDocument_.reset();
        largeDocument_ = std::move(document);
        readOnlyPreview_ = false;
        previewDecodedByteCount_ = 0;
        editorView_.SetLargeDocument(largeDocument_.get());
        editorView_.SetReadOnly(false);
    }

    void UpdateStatus() {
        if (editor_ == nullptr || status_ == nullptr) {
            return;
        }

        const auto line = static_cast<unsigned long long>(editorView_.Line() + 1);
        const auto column = static_cast<unsigned long long>(editorView_.Column() + 1);
        const auto lineCount = static_cast<unsigned long long>(editorView_.LineCount());
        const auto documentLength = static_cast<unsigned long long>(DocumentLength());

        wchar_t status[512]{};
        if (IsMappedLargeFile()) {
            StringCchPrintfW(
                status,
                std::size(status),
                L"Ln %llu, Col %llu    Lines %llu    %s    READ-ONLY MAPPED    %llu MB    %llu chars",
                line,
                column,
                lineCount,
                encodingLabel_.c_str(),
                static_cast<unsigned long long>(fileByteCount_ / (1024u * 1024u)),
                documentLength);
        } else if (IsLargeEditable()) {
            StringCchPrintfW(
                status,
                std::size(status),
                L"Ln %llu, Col %llu    Lines %llu    %s    LARGE FILE    %llu MB    %llu chars",
                line,
                column,
                lineCount,
                encodingLabel_.c_str(),
                static_cast<unsigned long long>(fileByteCount_ / (1024u * 1024u)),
                documentLength);
        } else if (readOnlyPreview_) {
            StringCchPrintfW(
                status,
                std::size(status),
                L"Ln %llu, Col %llu    Lines %llu    %s    READ-ONLY PREVIEW    %llu/%llu MB    %llu chars",
                line,
                column,
                lineCount,
                encodingLabel_.c_str(),
                static_cast<unsigned long long>(previewDecodedByteCount_ / (1024u * 1024u)),
                static_cast<unsigned long long>(fileByteCount_ / (1024u * 1024u)),
                documentLength);
        } else {
            StringCchPrintfW(
                status,
                std::size(status),
                L"Ln %llu, Col %llu    Lines %llu    %s    %llu chars",
                line,
                column,
                lineCount,
                encodingLabel_.c_str(),
                documentLength);
        }

        statusText_ = status;
        statusText_ += L"    " + std::to_wstring(editorView_.ZoomPercent()) + L"%";
        if (followTail_) {
            statusText_ += L"    FOLLOW TAIL";
        }
        InvalidateRect(status_, nullptr, FALSE);
    }

    bool ConfirmSaveIfDirty() {
        if (!dirty_) {
            return true;
        }

        const int choice = ShowAppMessage(
            L"Save changes to this document?",
            MessageDialogIcon::Question,
            L"NativePad",
            MessageDialogButtons::YesNoCancel,
            IDYES);

        if (choice == IDCANCEL) {
            return false;
        }

        if (choice == IDYES) {
            return SaveDocument(false);
        }

        return true;
    }

    void NewDocument() {
        if (!ConfirmSaveIfDirty()) {
            return;
        }

        StopFollowTail();
        CancelRecoveryJournal();
        currentPath_.clear();
        fileStamp_.reset();
        documentEncoding_ = NativePad::TextEncoding::Utf8;
        documentLineEnding_ = NativePad::LineEnding::CrLf;
        encodingLabel_ = NativePad::EncodingLabel(documentEncoding_);
        fileByteCount_ = 0;
        previewDecodedByteCount_ = 0;
        SetEditorText(L"");
        dirty_ = false;
        UpdateTitle();
        UpdateStatus();
        SetFocus(editor_);
    }

    void OpenDocumentFromDialog() {
        if (!ConfirmSaveIfDirty()) {
            return;
        }

        auto path = ShowOpenDialog(hwnd_);
        if (!path) {
            return;
        }

        OpenDocument(*path);
    }

    bool OpenDocument(const std::wstring& path) {
        // Tail following is a per-file mode; keep it only when the same path is
        // being reloaded (external change or tail refresh), not on a new file.
        if (followTail_ && path != currentPath_) {
            StopFollowTail();
        }

        // File size decides the backend: normal files become editable UTF-16 text,
        // while oversized files stay mapped and read-only.
        std::wstring error;
        if (auto fileByteCount = FileByteCountForPath(path); fileByteCount && *fileByteCount > kReadChunkLimit) {
            auto mapped = std::make_unique<NativePad::MappedTextDocument>();
            if (!mapped->Open(path, error)) {
                std::wstring message = L"Could not open large file:\n\n" + error;
                ShowAppMessage(message, MessageDialogIcon::Error);
                return false;
            }

            currentPath_ = path;
            encodingLabel_ = mapped->EncodingLabel();
            documentEncoding_ = NativePad::TextEncoding::Utf8;
            documentLineEnding_ = NativePad::LineEnding::CrLf;
            fileByteCount_ = mapped->FileByteCount();
            SetMappedEditorDocument(std::move(mapped));
            dirty_ = false;
            CancelRecoveryJournal();
            RecordFileStamp();
            AddRecentFile(currentPath_);
            UpdateTitle();
            UpdateStatus();
            SetFocus(editor_);
            return true;
        }

        auto file = ReadTextFile(path, error);
        if (!file) {
            std::wstring message = L"Could not open file:\n\n" + error;
            ShowAppMessage(message, MessageDialogIcon::Error);
            return false;
        }

        currentPath_ = path;
        documentEncoding_ = file->encoding;
        documentLineEnding_ = file->lineEnding;
        encodingLabel_ = NativePad::EncodingLabel(documentEncoding_);
        fileByteCount_ = file->fileByteCount;
        previewDecodedByteCount_ = file->decodedByteCount;
        SetEditorText(file->text, file->readOnlyPreview);
        dirty_ = false;
        CancelRecoveryJournal();
        RecordFileStamp();
        AddRecentFile(currentPath_);
        UpdateTitle();
        UpdateStatus();
        SetFocus(editor_);
        return true;
    }

    void RecordFileStamp() {
        fileStamp_ = QueryFileStamp(currentPath_);
    }

    void ScheduleRecoverySave() {
        // Journal only normal editable documents; read-only files have no unsaved
        // work, and journaling a multi-gigabyte large-file edit is impractical.
        // The timer is not reset on every keystroke so continuous typing still
        // journals at most once per delay window.
        if (recoverySavePending_ || readOnlyPreview_ || IsMappedLargeFile() || IsLargeEditable()) {
            return;
        }

        recoverySavePending_ = true;
        SetTimer(hwnd_, kRecoveryTimerId, kRecoverySaveDelayMs, nullptr);
    }

    void PersistRecoverySnapshot() {
        KillTimer(hwnd_, kRecoveryTimerId);
        recoverySavePending_ = false;

        if (!dirty_ || readOnlyPreview_ || IsMappedLargeFile() || IsLargeEditable()) {
            return;
        }

        NativePad::RecoverySnapshot snapshot;
        snapshot.originalPath = currentPath_;
        snapshot.encoding = documentEncoding_;
        snapshot.lineEnding = documentLineEnding_;
        snapshot.text = EditorText();
        recoveryJournal_.Save(snapshot);
    }

    void CancelRecoveryJournal() {
        if (recoverySavePending_) {
            KillTimer(hwnd_, kRecoveryTimerId);
            recoverySavePending_ = false;
        }
        recoveryJournal_.Clear();
    }

    void OfferRecoveredDocument() {
        auto snapshot = NativePad::RecoveryJournal::ClaimAbandoned(NativePad::RecoveryJournal::DefaultRootDirectory());
        if (!snapshot) {
            return;
        }

        std::wstring message = L"NativePad found unsaved work from a previous session";
        if (!snapshot->originalPath.empty()) {
            message += L" for \"" + BaseName(snapshot->originalPath) + L"\"";
        }
        message += L".\n\nRestore it?";

        const int choice = ShowAppMessage(
            message,
            MessageDialogIcon::Question,
            L"NativePad",
            MessageDialogButtons::YesNo,
            IDYES);
        if (choice != IDYES) {
            return;
        }

        StopFollowTail();
        currentPath_ = snapshot->originalPath;
        documentEncoding_ = snapshot->encoding;
        documentLineEnding_ = snapshot->lineEnding;
        encodingLabel_ = NativePad::EncodingLabel(documentEncoding_);
        fileByteCount_ = 0;
        previewDecodedByteCount_ = 0;
        SetEditorText(snapshot->text);
        dirty_ = true;
        RecordFileStamp();
        UpdateTitle();
        UpdateStatus();
        SetFocus(editor_);

        // Re-journal immediately: the claimed journal was deleted, and the
        // restored text must survive another crash before the first edit.
        NativePad::RecoverySnapshot rejournal = std::move(*snapshot);
        recoveryJournal_.Save(rejournal);
    }

    bool ReloadCurrentDocument(bool preserveCaret) {
        const std::wstring path = currentPath_;
        if (path.empty()) {
            return false;
        }

        const size_t caret = editorView_.CaretPosition();
        if (!OpenDocument(path)) {
            return false;
        }

        if (preserveCaret) {
            editorView_.SelectRange(std::min(caret, DocumentLength()), 0);
        }
        return true;
    }

    void CheckExternalFileChange() {
        // Follow Tail owns disk polling while it is active; this path only backs
        // the on-activation "file changed on disk" prompt.
        if (checkingExternalChange_ || followTail_ || currentPath_.empty() || !fileStamp_) {
            return;
        }

        const auto stamp = QueryFileStamp(currentPath_);
        if (!stamp || SameFileStamp(*stamp, *fileStamp_)) {
            return;
        }

        // Record before prompting so declining does not re-prompt for the same
        // change every time the window is activated.
        fileStamp_ = stamp;

        checkingExternalChange_ = true;
        std::wstring message = BaseName(currentPath_);
        message += dirty_
                       ? L" has changed on disk.\n\nReload it and lose your unsaved changes?"
                       : L" has changed on disk.\n\nReload it?";
        const int choice = ShowAppMessage(
            message,
            MessageDialogIcon::Question,
            L"NativePad",
            MessageDialogButtons::YesNo,
            dirty_ ? IDNO : IDYES);
        checkingExternalChange_ = false;

        if (choice == IDYES) {
            ReloadCurrentDocument(true);
        }
    }

    void ToggleFollowTail() {
        if (followTail_) {
            StopFollowTail();
            UpdateTitle();
            UpdateStatus();
            SetFocus(editor_);
            return;
        }

        if (currentPath_.empty()) {
            return;
        }

        if (dirty_ && !ConfirmSaveIfDirty()) {
            return;
        }

        followTail_ = true;

        // Editable documents follow the file by reloading, so start from the
        // current on-disk state and lock out edits that would be overwritten.
        if (!IsMappedLargeFile()) {
            const auto stamp = QueryFileStamp(currentPath_);
            const bool staleBuffer = dirty_ || !stamp || !fileStamp_ || !SameFileStamp(*stamp, *fileStamp_);
            if (staleBuffer && !ReloadCurrentDocument(false)) {
                StopFollowTail();
                return;
            }
            if (!IsMappedLargeFile()) {
                editorView_.SetReadOnly(true);
            }
        }

        editorView_.MoveCaretToDocumentEnd();
        SetTimer(hwnd_, kTailTimerId, kTailTimerIntervalMs, nullptr);
        UpdateTitle();
        UpdateStatus();
        SetFocus(editor_);
    }

    void StopFollowTail() {
        if (!followTail_) {
            return;
        }

        followTail_ = false;
        KillTimer(hwnd_, kTailTimerId);
        editorView_.SetReadOnly(readOnlyPreview_);
    }

    void OnTailTimer() {
        if (!followTail_) {
            return;
        }

        if (IsMappedLargeFile()) {
            std::wstring error;
            switch (mappedDocument_->Refresh(error)) {
            case NativePad::MappedTextDocument::RefreshStatus::Unchanged: {
                // Refresh follows the original handle. If the path now points at
                // a different file (log rotation by delete + rename), the handle
                // never changes, so compare the on-disk stamp separately.
                const auto stamp = QueryFileStamp(currentPath_);
                if (stamp && fileStamp_ && !SameFileStamp(*stamp, *fileStamp_)) {
                    ReloadForTail();
                }
                return;
            }
            case NativePad::MappedTextDocument::RefreshStatus::Appended:
                fileByteCount_ = mappedDocument_->FileByteCount();
                RecordFileStamp();
                editorView_.RefreshDocumentMetrics();
                editorView_.MoveCaretToDocumentEnd();
                UpdateStatus();
                return;
            case NativePad::MappedTextDocument::RefreshStatus::Replaced:
                ReloadForTail();
                return;
            case NativePad::MappedTextDocument::RefreshStatus::Failed:
                StopFollowTail();
                UpdateTitle();
                UpdateStatus();
                ShowAppMessage(
                    L"Follow Tail stopped because the file could not be refreshed:\n\n" + error,
                    MessageDialogIcon::Warning);
                return;
            }
            return;
        }

        const auto stamp = QueryFileStamp(currentPath_);
        if (!stamp) {
            // The file may be mid-replace (delete + rename). Keep polling.
            return;
        }

        if (fileStamp_ && SameFileStamp(*stamp, *fileStamp_)) {
            return;
        }

        ReloadForTail();
    }

    void ReloadForTail() {
        if (!ReloadCurrentDocument(false)) {
            // OpenDocument already reported the error; stop instead of showing
            // the same failure dialog on every timer tick.
            StopFollowTail();
            UpdateTitle();
            UpdateStatus();
            return;
        }

        if (followTail_) {
            if (!IsMappedLargeFile()) {
                editorView_.SetReadOnly(true);
            }
            editorView_.MoveCaretToDocumentEnd();
        }
    }

    bool SaveDocument(bool saveAs) {
        if (IsLargeEditable()) {
            return SaveLargeDocument(saveAs);
        }

        if (readOnlyPreview_) {
            const wchar_t* message = IsMappedLargeFile()
                                         ? L"This file is opened through the read-only large-file viewer. Choose Edit > Enable Large-File Editing to make changes."
                                         : L"This is a read-only large-file preview. NativePad is showing only the initial chunk, so saving is disabled.";
            ShowAppMessage(message, MessageDialogIcon::Information);
            return false;
        }

        std::wstring path = currentPath_;
        NativePad::TextEncoding targetEncoding = documentEncoding_;
        if (saveAs || path.empty()) {
            auto selected = ShowSaveDialog(hwnd_, path, targetEncoding);
            if (!selected) {
                return false;
            }
            path = selected->path;
            targetEncoding = selected->encoding;
        }

        const std::wstring text = EditorText();
        std::wstring error;
        if (!WriteTextFile(path, text, targetEncoding, documentLineEnding_, error)) {
            std::wstring message = L"Could not save file:\n\n" + error;
            ShowAppMessage(message, MessageDialogIcon::Error);
            return false;
        }

        if (followTail_ && path != currentPath_) {
            StopFollowTail();
        }
        currentPath_ = path;
        documentEncoding_ = targetEncoding;
        encodingLabel_ = NativePad::EncodingLabel(documentEncoding_);
        dirty_ = false;
        CancelRecoveryJournal();
        RecordFileStamp();
        AddRecentFile(currentPath_);
        UpdateTitle();
        UpdateStatus();
        return true;
    }

    void EnableLargeFileEditing() {
        // Reopen the currently viewed read-only large file through the editable
        // piece-table backend. The mapped viewer stays the default so the fast
        // read-only and Follow Tail paths are unaffected until the user opts in.
        if (!IsMappedLargeFile() || currentPath_.empty()) {
            return;
        }

        auto large = std::make_unique<NativePad::LargeTextDocument>();
        std::wstring error;
        if (!large->Open(currentPath_, error)) {
            ShowAppMessage(L"Could not enable editing for this file:\n\n" + error, MessageDialogIcon::Error);
            return;
        }

        const size_t caret = editorView_.CaretPosition();
        StopFollowTail();
        encodingLabel_ = large->EncodingLabel();
        documentEncoding_ = large->Encoding();
        documentLineEnding_ = large->DetectedLineEnding();
        fileByteCount_ = large->FileByteCount();
        SetLargeEditorDocument(std::move(large));
        editorView_.SelectRange(std::min<std::size_t>(caret, DocumentLength()), 0);
        dirty_ = false;
        RecordFileStamp();
        UpdateTitle();
        UpdateStatus();
        SetFocus(editor_);
    }

    bool SaveLargeDocument(bool saveAs) {
        std::wstring targetPath = currentPath_;
        if (saveAs) {
            // Save As writes in the document's own encoding; the encoding picker
            // does not re-encode a large file in this version.
            auto selected = ShowSaveDialog(hwnd_, targetPath, documentEncoding_);
            if (!selected) {
                return false;
            }
            targetPath = selected->path;
        }
        if (targetPath.empty()) {
            return false;
        }

        // Stage the new content beside the target so the replace is atomic, then
        // unmap the original before swapping it in and reopening from disk.
        std::wstring error;
        const std::wstring stagingPath = targetPath + L".np-save";
        if (!largeDocument_->SaveTo(stagingPath, error)) {
            ShowAppMessage(L"Could not save file:\n\n" + error, MessageDialogIcon::Error);
            DeleteFileW(stagingPath.c_str());
            return false;
        }

        const size_t caret = editorView_.CaretPosition();
        largeDocument_->Close();

        if (!MoveFileExW(stagingPath.c_str(), targetPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            error = GetLastErrorText();
            DeleteFileW(stagingPath.c_str());
            // Reopen the original so the document is not left detached.
            if (largeDocument_->Open(currentPath_, error)) {
                editorView_.SetLargeDocument(largeDocument_.get());
                editorView_.SelectRange(std::min<std::size_t>(caret, DocumentLength()), 0);
            }
            ShowAppMessage(L"Could not replace the file while saving.", MessageDialogIcon::Error);
            return false;
        }

        if (!largeDocument_->Open(targetPath, error)) {
            ShowAppMessage(L"The file was saved, but could not be reopened:\n\n" + error, MessageDialogIcon::Error);
            return false;
        }

        currentPath_ = targetPath;
        encodingLabel_ = largeDocument_->EncodingLabel();
        documentEncoding_ = largeDocument_->Encoding();
        documentLineEnding_ = largeDocument_->DetectedLineEnding();
        fileByteCount_ = largeDocument_->FileByteCount();
        // The backend object is unchanged, but the view must rebuild against the
        // freshly reopened content.
        editorView_.SetLargeDocument(largeDocument_.get());
        editorView_.SelectRange(std::min<std::size_t>(caret, DocumentLength()), 0);
        dirty_ = false;
        RecordFileStamp();
        AddRecentFile(currentPath_);
        UpdateTitle();
        UpdateStatus();
        return true;
    }

    void OnDropFiles(HDROP drop) {
        if (!ConfirmSaveIfDirty()) {
            DragFinish(drop);
            return;
        }

        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        if (count > 0) {
            std::array<wchar_t, 32768> path{};
            if (DragQueryFileW(drop, 0, path.data(), static_cast<UINT>(path.size())) > 0) {
                OpenDocument(path.data());
            }
        }

        DragFinish(drop);
    }

    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND menuStrip_{};
    HWND editor_{};
    HWND status_{};
    HMENU fileMenu_{};
    HMENU editMenu_{};
    HMENU formatMenu_{};
    HMENU viewMenu_{};
    HMENU helpMenu_{};
    HBRUSH menuBackgroundBrush_{};
    HFONT uiFont_{};
    std::array<MenuEntry, 5> menuEntries_{{
        {L"File", nullptr, {}},
        {L"Edit", nullptr, {}},
        {L"Format", nullptr, {}},
        {L"View", nullptr, {}},
        {L"Help", nullptr, {}},
    }};
    NativePad::EditorFontSpec preferredFont_{};
    NativePad::DocumentBuffer document_;
    std::unique_ptr<NativePad::MappedTextDocument> mappedDocument_;
    std::unique_ptr<NativePad::LargeTextDocument> largeDocument_;
    NativePad::EditorView editorView_;
    std::wstring currentPath_;
    std::wstring encodingLabel_{L"UTF-8"};
    NativePad::TextEncoding documentEncoding_{NativePad::TextEncoding::Utf8};
    NativePad::LineEnding documentLineEnding_{NativePad::LineEnding::CrLf};
    std::wstring statusText_;
    std::array<wchar_t, 512> findBuffer_{};
    std::array<wchar_t, 512> replaceBuffer_{};
    std::wstring lastFindText_;
    std::wstring lastReplaceText_;
    HWND findDialog_{};
    RECT pageMarginsThousandths_{1000, 1000, 1000, 1000};
    RECT savedWindowRect_{};
    NativePad::RecoveryJournal recoveryJournal_;
    std::vector<std::wstring> recentFiles_;
    std::optional<FileStamp> fileStamp_;
    uint64_t fileByteCount_{0};
    size_t previewDecodedByteCount_{0};
    UINT dpi_{USER_DEFAULT_SCREEN_DPI};
    int hotMenu_{-1};
    int activeMenu_{-1};
    int keyboardMenuIndex_{-1};
    bool dirty_{false};
    bool readOnlyPreview_{false};
    bool followTail_{false};
    bool checkingExternalChange_{false};
    bool recoverySavePending_{false};
    bool replaceDialogOpen_{false};
    bool lastFindMatchCase_{false};
    bool lastFindDown_{true};
    bool preferredWordWrap_{false};
    bool preferredLineNumbers_{false};
    bool statusBarVisible_{true};
    bool darkMode_{false};
    bool darkModeForced_{false};
    bool hasSavedWindowRect_{false};
    bool savedWindowMaximized_{false};
    bool menuMouseTracking_{false};
    bool menuKeyboardCuesVisible_{false};
    bool updateCheckInProgress_{false};
    bool updateDownloadInProgress_{false};
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    EnableProcessDpiAwareness();

    // The Vista+ common file dialogs use COM, and the Save As encoding picker
    // is added through IFileDialogCustomize before the dialog is shown.
    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninitializeCom = SUCCEEDED(comInit);

    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&commonControls);

    AppWindow app(instance);
    if (!app.Create(showCommand)) {
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 1;
    }

    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments != nullptr) {
        if (argumentCount > 1) {
            app.OpenInitialFile(arguments[1]);
        }
        LocalFree(arguments);
    }

    ACCEL accelerators[] = {
        {FVIRTKEY | FCONTROL, 'N', ID_FILE_NEW},
        {FVIRTKEY | FCONTROL, 'O', ID_FILE_OPEN},
        {FVIRTKEY | FCONTROL, 'S', ID_FILE_SAVE},
        {FVIRTKEY | FCONTROL | FSHIFT, 'S', ID_FILE_SAVE_AS},
        {FVIRTKEY | FCONTROL, 'P', ID_FILE_PRINT},
        {FVIRTKEY | FCONTROL, 'Z', ID_EDIT_UNDO},
        {FVIRTKEY | FCONTROL, 'Y', ID_EDIT_REDO},
        {FVIRTKEY | FCONTROL, 'X', ID_EDIT_CUT},
        {FVIRTKEY | FCONTROL, 'C', ID_EDIT_COPY},
        {FVIRTKEY | FCONTROL, 'V', ID_EDIT_PASTE},
        {FVIRTKEY, VK_DELETE, ID_EDIT_DELETE},
        {FVIRTKEY | FCONTROL, 'F', ID_EDIT_FIND},
        {FVIRTKEY, VK_F3, ID_EDIT_FIND_NEXT},
        {FVIRTKEY | FSHIFT, VK_F3, ID_EDIT_FIND_PREVIOUS},
        {FVIRTKEY | FCONTROL, 'H', ID_EDIT_REPLACE},
        {FVIRTKEY | FCONTROL, 'G', ID_EDIT_GO_TO},
        {FVIRTKEY | FCONTROL, 'A', ID_EDIT_SELECT_ALL},
        {FVIRTKEY, VK_F5, ID_EDIT_TIME_DATE},
        {FVIRTKEY, VK_F6, ID_VIEW_FOLLOW_TAIL},
        {FVIRTKEY | FCONTROL | FSHIFT, 'D', ID_EDIT_DUPLICATE_LINE},
        {FVIRTKEY | FCONTROL | FSHIFT, 'K', ID_EDIT_DELETE_LINE},
        {FVIRTKEY | FALT, VK_UP, ID_EDIT_MOVE_LINE_UP},
        {FVIRTKEY | FALT, VK_DOWN, ID_EDIT_MOVE_LINE_DOWN},
        {FVIRTKEY | FCONTROL, VK_OEM_PLUS, ID_VIEW_ZOOM_IN},
        {FVIRTKEY | FCONTROL, VK_ADD, ID_VIEW_ZOOM_IN},
        {FVIRTKEY | FCONTROL, VK_OEM_MINUS, ID_VIEW_ZOOM_OUT},
        {FVIRTKEY | FCONTROL, VK_SUBTRACT, ID_VIEW_ZOOM_OUT},
        {FVIRTKEY | FCONTROL, '0', ID_VIEW_ZOOM_RESET},
        {FVIRTKEY | FCONTROL, VK_NUMPAD0, ID_VIEW_ZOOM_RESET},
    };

    HACCEL acceleratorTable = CreateAcceleratorTableW(accelerators, static_cast<int>(std::size(accelerators)));

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (app.TranslateModelessDialog(&message)) {
            continue;
        }
        if (acceleratorTable == nullptr || TranslateAcceleratorW(app.Hwnd(), acceleratorTable, &message) == 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (acceleratorTable != nullptr) {
        DestroyAcceleratorTable(acceleratorTable);
    }

    if (shouldUninitializeCom) {
        CoUninitialize();
    }

    return static_cast<int>(message.wParam);
}
