#include "Settings.h"

namespace NativePad {

namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\NativePad";

} // namespace

std::optional<DWORD> ReadSettingsDword(const wchar_t* name) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        kSettingsKey,
        name,
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);

    if (status != ERROR_SUCCESS || size != sizeof(value)) {
        return std::nullopt;
    }

    return value;
}

std::optional<int> ReadSettingsInt(const wchar_t* name) {
    auto value = ReadSettingsDword(name);
    if (!value) {
        return std::nullopt;
    }
    return static_cast<int>(static_cast<LONG>(*value));
}

std::optional<std::wstring> ReadSettingsString(const wchar_t* name) {
    DWORD byteCount = 0;
    LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        kSettingsKey,
        name,
        RRF_RT_REG_SZ,
        nullptr,
        nullptr,
        &byteCount);

    if (status != ERROR_SUCCESS || byteCount < sizeof(wchar_t)) {
        return std::nullopt;
    }

    std::wstring value(byteCount / sizeof(wchar_t), L'\0');
    status = RegGetValueW(
        HKEY_CURRENT_USER,
        kSettingsKey,
        name,
        RRF_RT_REG_SZ,
        nullptr,
        value.data(),
        &byteCount);

    if (status != ERROR_SUCCESS) {
        return std::nullopt;
    }

    value.resize(byteCount / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

bool CreateSettingsKey(HKEY& key) {
    key = nullptr;
    return RegCreateKeyExW(
               HKEY_CURRENT_USER,
               kSettingsKey,
               0,
               nullptr,
               REG_OPTION_NON_VOLATILE,
               KEY_SET_VALUE,
               nullptr,
               &key,
               nullptr) == ERROR_SUCCESS;
}

void WriteSettingsDword(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

void WriteSettingsInt(HKEY key, const wchar_t* name, int value) {
    WriteSettingsDword(key, name, static_cast<DWORD>(static_cast<LONG>(value)));
}

void WriteSettingsString(HKEY key, const wchar_t* name, const std::wstring& value) {
    const DWORD byteCount = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), byteCount);
}

} // namespace NativePad
