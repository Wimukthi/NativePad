#include "AppDataPaths.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <filesystem>
#include <system_error>
#include <vector>

namespace NativePad {

namespace {

bool FileExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

// Creating the folder is not enough: a read-only USB stick or a protected
// Program Files folder can already contain Data. Write and delete a probe file.
bool DirectoryIsWritable(const std::filesystem::path& directory) {
    const std::filesystem::path probe = directory / L".nativepad-write-test";
    HANDLE file = CreateFileW(
        probe.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    const char byte = 0;
    DWORD written = 0;
    const bool ok = WriteFile(file, &byte, 1, &written, nullptr) != FALSE && written == 1;
    CloseHandle(file);
    return ok;
}

std::wstring ExecutableDirectory() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path().wstring();
        }
        if (buffer.size() >= 32768) {
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }
}

bool CopyFileIfPresent(const std::filesystem::path& from, const std::filesystem::path& to) {
    if (!FileExists(from)) {
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(to.parent_path(), ec);
    return CopyFileW(from.c_str(), to.c_str(), TRUE) != FALSE || GetLastError() == ERROR_FILE_EXISTS;
}

} // namespace

DataRootInfo ResolveDataRoot(const std::wstring& executableDirectory, const std::wstring& localAppDataRoot) {
    DataRootInfo info;
    info.root = localAppDataRoot;
    info.location = DataLocation::LocalAppData;

    if (executableDirectory.empty()) {
        return info;
    }

    const std::filesystem::path exeDirectory(executableDirectory);
    if (!FileExists(exeDirectory / kPortableMarkerFileName)) {
        return info;
    }

    const std::filesystem::path dataDirectory = exeDirectory / kPortableDataFolderName;
    std::error_code ec;
    const bool existed = std::filesystem::is_directory(dataDirectory, ec);
    if (!existed) {
        std::filesystem::create_directories(dataDirectory, ec);
    }

    if (ec || !std::filesystem::is_directory(dataDirectory, ec) || !DirectoryIsWritable(dataDirectory)) {
        info.portableFallback = true;
        return info;
    }

    info.root = dataDirectory.wstring();
    info.location = DataLocation::Portable;
    info.portableFolderCreated = !existed;
    return info;
}

std::wstring LocalAppDataRoot() {
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path);
    if (FAILED(result) || path == nullptr) {
        return {};
    }

    std::filesystem::path directory(path);
    CoTaskMemFree(path);
    directory /= L"NativePad";
    return directory.wstring();
}

const DataRootInfo& ApplicationDataRoot() {
    static const DataRootInfo info = ResolveDataRoot(ExecutableDirectory(), LocalAppDataRoot());
    return info;
}

std::wstring ApplicationDataDirectory(const wchar_t* child) {
    const DataRootInfo& info = ApplicationDataRoot();
    if (info.root.empty()) {
        return {};
    }

    std::filesystem::path directory(info.root);
    if (child != nullptr && *child != L'\0') {
        directory /= child;
    }

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        return {};
    }
    return directory.wstring();
}

bool IsPortableMode() {
    return ApplicationDataRoot().location == DataLocation::Portable;
}

bool LocalDataWorthCopying(const std::wstring& localAppDataRoot) {
    if (localAppDataRoot.empty()) {
        return false;
    }
    const std::filesystem::path root(localAppDataRoot);
    return FileExists(root / L"NativePad.ini") || FileExists(root / L"Session" / L"session.bin");
}

bool CopyLocalDataToPortable(const std::wstring& localAppDataRoot, const std::wstring& portableRoot) {
    if (localAppDataRoot.empty() || portableRoot.empty()) {
        return false;
    }

    const std::filesystem::path from(localAppDataRoot);
    const std::filesystem::path to(portableRoot);
    bool ok = CopyFileIfPresent(from / L"NativePad.ini", to / L"NativePad.ini");

    // The session folder holds the manifest and its tab snapshots. Copy every
    // regular file; the manifest is copied last so a partial copy is never
    // mistaken for a complete session.
    const std::filesystem::path sessionFrom = from / L"Session";
    const std::filesystem::path sessionTo = to / L"Session";
    std::error_code ec;
    if (std::filesystem::is_directory(sessionFrom, ec)) {
        std::filesystem::path manifest;
        for (const auto& entry : std::filesystem::directory_iterator(sessionFrom, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            if (entry.path().filename() == L"session.bin") {
                manifest = entry.path();
                continue;
            }
            if (entry.path().filename().wstring().rfind(L"tab-", 0) != 0) {
                continue; // staging leftovers are not part of a session
            }
            ok =CopyFileIfPresent(entry.path(), sessionTo / entry.path().filename()) && ok;
        }
        if (ec) {
            ok = false;
        }
        if (!manifest.empty()) {
            ok = ok && CopyFileIfPresent(manifest, sessionTo / manifest.filename());
        }
    }
    return ok;
}

} // namespace NativePad
