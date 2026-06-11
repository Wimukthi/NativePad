#pragma once

#include <windows.h>

#include <optional>

#include "EditorView.h"

// Custom Font dialog plus the EditorFontSpec <-> LOGFONTW bridge. The conversion
// helper is shared because the print path also needs a LOGFONTW built from the
// editor's current font.

namespace NativePad {

[[nodiscard]] LOGFONTW LogFontFromEditorFont(const EditorFontSpec& font, UINT dpi);

[[nodiscard]] std::optional<EditorFontSpec> ShowFontDialog(
    HWND owner,
    HINSTANCE instance,
    const EditorFontSpec& currentFont,
    UINT dpi,
    bool dark);

} // namespace NativePad
