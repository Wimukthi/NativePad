#pragma once

#include <windows.h>

#include <string>
#include <string_view>

// Modeless Find/Replace dialog. Instead of the common-dialog FINDREPLACE struct,
// it posts compact action requests to its owner through WM_NATIVEPAD_FIND_REPLACE
// (the LPARAM points at a FindReplaceDialogRequest owned by the dialog thread).

namespace NativePad {

constexpr UINT WM_NATIVEPAD_FIND_REPLACE = WM_APP + 303;

enum class FindReplaceDialogAction {
    Closed,
    FindNext,
    Replace,
    ReplaceAll,
};

struct FindReplaceDialogRequest {
    HWND dialog{};
    FindReplaceDialogAction action{FindReplaceDialogAction::Closed};
    std::wstring findText;
    std::wstring replaceText;
    bool matchCase{false};
    bool down{true};
};

[[nodiscard]] HWND ShowFindReplaceDialog(
    HWND owner,
    HINSTANCE instance,
    UINT dpi,
    bool dark,
    bool replaceMode,
    std::wstring_view findText,
    std::wstring_view replaceText,
    bool matchCase,
    bool down);

} // namespace NativePad
