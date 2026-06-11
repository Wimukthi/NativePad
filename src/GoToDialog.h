#pragma once

#include <windows.h>

#include <cstddef>
#include <optional>

// Custom modal "Go To Line" dialog. Returns the chosen zero-based line index,
// or nullopt if the user cancelled. Line input is one-based to match Notepad.

namespace NativePad {

[[nodiscard]] std::optional<std::size_t> ShowGoToLineDialog(
    HWND owner,
    HINSTANCE instance,
    UINT dpi,
    bool dark,
    std::size_t currentLine,
    std::size_t maxLine);

} // namespace NativePad
