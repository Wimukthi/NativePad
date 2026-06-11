#include "DefaultEditor.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace NativePad {

namespace {

// Per-user ProgID and the RegisteredApplications entry name. The Windows 11
// Default-apps deep link below references this same registered-application name.
constexpr wchar_t kProgId[] = L"NativePad.txt";
constexpr wchar_t kRegisteredAppName[] = L"NativePad";
constexpr wchar_t kCapabilitiesKey[] = L"Software\\NativePad\\Capabilities";

std::wstring ExecutablePath(HINSTANCE instance) {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(instance, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size() - 1) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

bool SetRegString(HKEY root, const wchar_t* subkey, const wchar_t* name, const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const DWORD byteCount = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS status = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), byteCount);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool RegisterCapabilities(const std::wstring& exe, std::wstring& error) {
    const std::wstring openCommand = L"\"" + exe + L"\" \"%1\"";
    const std::wstring iconValue = L"\"" + exe + L"\",0";

    // ProgID describing how NativePad opens a text file.
    const bool ok =
        SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\NativePad.txt", nullptr, L"Text Document") &&
        SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\NativePad.txt\\DefaultIcon", nullptr, iconValue) &&
        SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\NativePad.txt\\shell\\open\\command", nullptr, openCommand) &&
        // "Open with" application entry.
        SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\NativePad.exe", L"FriendlyAppName", L"NativePad") &&
        SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\NativePad.exe\\shell\\open\\command", nullptr, openCommand) &&
        SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\NativePad.exe\\SupportedTypes", L".txt", L"") &&
        // Offer NativePad in the .txt "Open with" list without changing the default.
        SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\.txt\\OpenWithProgids", kProgId, L"") &&
        // Capabilities so NativePad appears in Windows "Default apps".
        SetRegString(HKEY_CURRENT_USER, kCapabilitiesKey, L"ApplicationName", L"NativePad") &&
        SetRegString(HKEY_CURRENT_USER, kCapabilitiesKey, L"ApplicationDescription", L"A fast native Win32 Notepad replacement.") &&
        SetRegString(HKEY_CURRENT_USER, L"Software\\NativePad\\Capabilities\\FileAssociations", L".txt", kProgId) &&
        SetRegString(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", kRegisteredAppName, kCapabilitiesKey);

    if (!ok) {
        error = L"Writing the per-user file associations to the registry failed.";
        return false;
    }

    return true;
}

} // namespace

bool PromptSetDefaultEditor(HWND owner, HINSTANCE instance, std::wstring& error) {
    const std::wstring exe = ExecutablePath(instance);
    if (exe.empty()) {
        error = L"Could not determine the NativePad executable path.";
        return false;
    }

    if (!RegisterCapabilities(exe, error)) {
        return false;
    }

    // Tell the shell associations changed so the new ProgID shows up immediately.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_FLUSH, nullptr, nullptr);

    // Open Windows "Default apps". Windows 11 supports deep-linking straight to
    // NativePad's page; older builds ignore the query and open the main page.
    const auto deepLink = reinterpret_cast<INT_PTR>(ShellExecuteW(
        owner, L"open", L"ms-settings:defaultapps?registeredAppUser=NativePad", nullptr, nullptr, SW_SHOWNORMAL));
    if (deepLink <= 32) {
        ShellExecuteW(owner, L"open", L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL);
    }

    return true;
}

bool IsDefaultEditor() {
    Microsoft::WRL::ComPtr<IApplicationAssociationRegistration> registration;
    if (FAILED(CoCreateInstance(
            CLSID_ApplicationAssociationRegistration,
            nullptr,
            CLSCTX_INPROC,
            IID_PPV_ARGS(&registration)))) {
        return false;
    }

    LPWSTR currentProgId = nullptr;
    bool isDefault = false;
    if (SUCCEEDED(registration->QueryCurrentDefault(L".txt", AT_FILEEXTENSION, AL_EFFECTIVE, &currentProgId)) &&
        currentProgId != nullptr) {
        isDefault = CompareStringOrdinal(currentProgId, -1, kProgId, -1, TRUE) == CSTR_EQUAL;
        CoTaskMemFree(currentProgId);
    }

    return isDefault;
}

} // namespace NativePad
