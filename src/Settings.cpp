#include "Settings.h"

#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace NativePad {

namespace {

constexpr wchar_t kSettingsSection[] = L"Settings";
constexpr wchar_t kLegacySettingsKey[] = L"Software\\NativePad";

std::optional<std::wstring> LocalAppDataPath() {
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path);
    if (FAILED(result) || path == nullptr) {
        return std::nullopt;
    }

    std::wstring value(path);
    CoTaskMemFree(path);
    return value;
}

std::optional<std::wstring> SettingsFilePath() {
    const auto localAppData = LocalAppDataPath();
    if (!localAppData) {
        return std::nullopt;
    }

    std::filesystem::path directory(*localAppData);
    directory /= L"NativePad";

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        return std::nullopt;
    }

    return (directory / L"NativePad.ini").wstring();
}

void WriteIniString(const std::wstring& path, const wchar_t* name, const std::wstring& value) {
    WritePrivateProfileStringW(kSettingsSection, name, value.c_str(), path.c_str());
}

void MigrateLegacyRegistrySettingsIfNeeded(const std::wstring& path) {
    if (std::filesystem::exists(std::filesystem::path(path))) {
        return;
    }

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kLegacySettingsKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return;
    }

    DWORD valueCount = 0;
    DWORD maxNameLength = 0;
    DWORD maxValueBytes = 0;
    if (RegQueryInfoKeyW(
            key,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            &valueCount,
            &maxNameLength,
            &maxValueBytes,
            nullptr,
            nullptr) != ERROR_SUCCESS) {
        RegCloseKey(key);
        return;
    }

    std::vector<wchar_t> name(maxNameLength + 1);
    std::vector<BYTE> value(std::max<DWORD>(maxValueBytes, sizeof(DWORD)));
    for (DWORD index = 0; index < valueCount; ++index) {
        DWORD nameLength = static_cast<DWORD>(name.size());
        DWORD valueBytes = static_cast<DWORD>(value.size());
        DWORD type = 0;
        if (RegEnumValueW(key, index, name.data(), &nameLength, nullptr, &type, value.data(), &valueBytes) != ERROR_SUCCESS) {
            continue;
        }

        if (type == REG_DWORD && valueBytes == sizeof(DWORD)) {
            DWORD number = 0;
            std::memcpy(&number, value.data(), sizeof(number));
            WriteIniString(path, name.data(), std::to_wstring(number));
        } else if (type == REG_SZ && valueBytes >= sizeof(wchar_t)) {
            std::wstring text(reinterpret_cast<const wchar_t*>(value.data()), valueBytes / sizeof(wchar_t));
            while (!text.empty() && text.back() == L'\0') {
                text.pop_back();
            }
            WriteIniString(path, name.data(), text);
        }
    }

    RegCloseKey(key);
}

std::optional<std::wstring> SettingsPathReady() {
    auto path = SettingsFilePath();
    if (path) {
        MigrateLegacyRegistrySettingsIfNeeded(*path);
    }
    return path;
}

std::optional<std::wstring> ReadRawSetting(const wchar_t* name) {
    const auto path = SettingsPathReady();
    if (!path) {
        return std::nullopt;
    }

    constexpr wchar_t kMissing[] = L"\x1fNativePadMissing\x1f";
    std::wstring buffer(512, L'\0');
    for (;;) {
        const DWORD copied = GetPrivateProfileStringW(
            kSettingsSection,
            name,
            kMissing,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            path->c_str());
        if (copied == buffer.size() - 1) {
            buffer.resize(buffer.size() * 2);
            continue;
        }

        buffer.resize(copied);
        if (buffer == kMissing) {
            return std::nullopt;
        }
        return buffer;
    }
}

} // namespace

std::optional<DWORD> ReadSettingsDword(const wchar_t* name) {
    const auto text = ReadRawSetting(name);
    if (!text || text->empty()) {
        return std::nullopt;
    }

    unsigned long long value = 0;
    for (wchar_t ch : *text) {
        if (ch < L'0' || ch > L'9') {
            return std::nullopt;
        }
        value = (value * 10ull) + static_cast<unsigned long long>(ch - L'0');
        if (value > std::numeric_limits<DWORD>::max()) {
            return std::nullopt;
        }
    }

    return static_cast<DWORD>(value);
}

std::optional<int> ReadSettingsInt(const wchar_t* name) {
    const auto text = ReadRawSetting(name);
    if (!text || text->empty()) {
        return std::nullopt;
    }

    size_t index = 0;
    const bool negative = (*text)[0] == L'-';
    if (negative) {
        index = 1;
        if (index == text->size()) {
            return std::nullopt;
        }
    }

    long long magnitude = 0;
    for (; index < text->size(); ++index) {
        const wchar_t ch = (*text)[index];
        if (ch < L'0' || ch > L'9') {
            return std::nullopt;
        }
        magnitude = (magnitude * 10ll) + static_cast<long long>(ch - L'0');
        const long long limit = negative
            ? static_cast<long long>(std::numeric_limits<int>::max()) + 1ll
            : static_cast<long long>(std::numeric_limits<int>::max());
        if (magnitude > limit) {
            return std::nullopt;
        }
    }

    const long long value = negative ? -magnitude : magnitude;
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }

    return static_cast<int>(value);
}

std::optional<std::wstring> ReadSettingsString(const wchar_t* name) {
    return ReadRawSetting(name);
}

void WriteSettingsDword(const wchar_t* name, DWORD value) {
    WriteSettingsString(name, std::to_wstring(value));
}

void WriteSettingsInt(const wchar_t* name, int value) {
    WriteSettingsString(name, std::to_wstring(value));
}

void WriteSettingsString(const wchar_t* name, const std::wstring& value) {
    const auto path = SettingsPathReady();
    if (path) {
        WriteIniString(*path, name, value);
    }
}

} // namespace NativePad
