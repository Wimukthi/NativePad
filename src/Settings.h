#pragma once

#include <windows.h>

#include <optional>
#include <string>

// Persisted user preferences live under HKCU\Software\NativePad. These helpers
// wrap the registry access so the application shell can load and save window
// placement, theme, font, and view options without repeating the boilerplate.

namespace NativePad {

[[nodiscard]] std::optional<DWORD> ReadSettingsDword(const wchar_t* name);
[[nodiscard]] std::optional<int> ReadSettingsInt(const wchar_t* name);
[[nodiscard]] std::optional<std::wstring> ReadSettingsString(const wchar_t* name);

[[nodiscard]] bool CreateSettingsKey(HKEY& key);
void WriteSettingsDword(HKEY key, const wchar_t* name, DWORD value);
void WriteSettingsInt(HKEY key, const wchar_t* name, int value);
void WriteSettingsString(HKEY key, const wchar_t* name, const std::wstring& value);

} // namespace NativePad
