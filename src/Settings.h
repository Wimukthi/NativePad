#pragma once

#include <windows.h>

#include <optional>
#include <string>

// Persisted user preferences live in %LOCALAPPDATA%\NativePad\NativePad.ini.
// These helpers wrap the file access so the application shell can load and save
// window placement, theme, font, update, and view options without repeating the
// boilerplate.

namespace NativePad {

[[nodiscard]] std::optional<DWORD> ReadSettingsDword(const wchar_t* name);
[[nodiscard]] std::optional<int> ReadSettingsInt(const wchar_t* name);
[[nodiscard]] std::optional<std::wstring> ReadSettingsString(const wchar_t* name);

void WriteSettingsDword(const wchar_t* name, DWORD value);
void WriteSettingsInt(const wchar_t* name, int value);
void WriteSettingsString(const wchar_t* name, const std::wstring& value);

} // namespace NativePad
