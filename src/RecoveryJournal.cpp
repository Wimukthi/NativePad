#include "RecoveryJournal.h"

#include <shlobj.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>

namespace NativePad {

namespace {

constexpr std::wstring_view kMetaExtension = L".meta";
constexpr std::wstring_view kContentExtension = L".txt";
constexpr std::wstring_view kSessionPrefix = L"session-";
constexpr int kMetaVersion = 1;

std::wstring SessionBaseName(DWORD processId) {
    return std::wstring(kSessionPrefix) + std::to_wstring(processId);
}

bool WriteFileBytes(const std::wstring& path, const void* data, std::size_t byteCount) {
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool ok = true;
    const auto* cursor = static_cast<const unsigned char*>(data);
    std::size_t remaining = byteCount;
    while (ok && remaining > 0) {
        const DWORD toWrite = static_cast<DWORD>(std::min<std::size_t>(remaining, 1024u * 1024u));
        DWORD written = 0;
        ok = WriteFile(file, cursor, toWrite, &written, nullptr) && written > 0;
        cursor += written;
        remaining -= written;
    }

    CloseHandle(file);
    if (!ok) {
        DeleteFileW(path.c_str());
    }
    return ok;
}

std::optional<std::vector<unsigned char>> ReadFileBytes(const std::wstring& path) {
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > (1ll << 31)) {
        CloseHandle(file);
        return std::nullopt;
    }

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &read, nullptr) || read == 0) {
            CloseHandle(file);
            return std::nullopt;
        }
        offset += read;
    }

    CloseHandle(file);
    return bytes;
}

// Journal content is always UTF-16 LE with a BOM regardless of the document's
// own encoding, so restore is an exact round trip of the editor buffer. The
// document encoding is kept in the metadata for the eventual real save.
bool WriteContentFile(const std::wstring& path, const std::wstring& text) {
    std::vector<unsigned char> bytes;
    bytes.reserve(2 + text.size() * sizeof(wchar_t));
    bytes.push_back(0xFF);
    bytes.push_back(0xFE);
    const auto* raw = reinterpret_cast<const unsigned char*>(text.data());
    bytes.insert(bytes.end(), raw, raw + text.size() * sizeof(wchar_t));

    // Stage next to the target and rename so a crash mid-write never leaves a
    // truncated journal behind a valid metadata file.
    const std::wstring staging = path + L".tmp";
    if (!WriteFileBytes(staging, bytes.data(), bytes.size())) {
        return false;
    }

    if (!MoveFileExW(staging.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(staging.c_str());
        return false;
    }

    return true;
}

std::optional<std::wstring> ReadContentFile(const std::wstring& path) {
    const auto bytes = ReadFileBytes(path);
    if (!bytes || bytes->size() < 2 || (*bytes)[0] != 0xFF || (*bytes)[1] != 0xFE) {
        return std::nullopt;
    }

    const std::size_t charCount = (bytes->size() - 2) / sizeof(wchar_t);
    std::wstring text(charCount, L'\0');
    memcpy(text.data(), bytes->data() + 2, charCount * sizeof(wchar_t));
    return text;
}

std::string ToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }

    std::string bytes(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), bytes.data(), required, nullptr, nullptr);
    return bytes;
}

std::wstring FromUtf8(std::string_view bytes) {
    if (bytes.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }

    std::wstring text(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), required);
    return text;
}

struct JournalMeta {
    DWORD processId{0};
    std::wstring originalPath;
    TextEncoding encoding{TextEncoding::Utf8};
    LineEnding lineEnding{LineEnding::CrLf};
};

bool WriteMetaFile(const std::wstring& path, const JournalMeta& meta) {
    std::wstring text;
    text += L"version=" + std::to_wstring(kMetaVersion) + L"\n";
    text += L"pid=" + std::to_wstring(meta.processId) + L"\n";
    text += L"encoding=" + std::to_wstring(static_cast<int>(meta.encoding)) + L"\n";
    text += L"lineEnding=" + std::to_wstring(static_cast<int>(meta.lineEnding)) + L"\n";
    text += L"path=" + meta.originalPath + L"\n";

    const std::string bytes = ToUtf8(text);
    return WriteFileBytes(path, bytes.data(), bytes.size());
}

std::optional<JournalMeta> ReadMetaFile(const std::wstring& path) {
    const auto bytes = ReadFileBytes(path);
    if (!bytes) {
        return std::nullopt;
    }

    const std::wstring text = FromUtf8({reinterpret_cast<const char*>(bytes->data()), bytes->size()});

    JournalMeta meta;
    bool versionOk = false;
    std::size_t lineStart = 0;
    while (lineStart < text.size()) {
        std::size_t lineEnd = text.find(L'\n', lineStart);
        if (lineEnd == std::wstring::npos) {
            lineEnd = text.size();
        }

        const std::wstring_view line(text.data() + lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        const std::size_t equals = line.find(L'=');
        if (equals == std::wstring_view::npos) {
            continue;
        }

        const std::wstring_view key = line.substr(0, equals);
        const std::wstring value(line.substr(equals + 1));
        if (key == L"version") {
            versionOk = value == std::to_wstring(kMetaVersion);
        } else if (key == L"pid") {
            meta.processId = static_cast<DWORD>(wcstoul(value.c_str(), nullptr, 10));
        } else if (key == L"encoding") {
            const long parsed = wcstol(value.c_str(), nullptr, 10);
            if (parsed >= static_cast<long>(TextEncoding::Utf8) && parsed <= static_cast<long>(TextEncoding::Ansi)) {
                meta.encoding = static_cast<TextEncoding>(parsed);
            }
        } else if (key == L"lineEnding") {
            const long parsed = wcstol(value.c_str(), nullptr, 10);
            if (parsed >= static_cast<long>(LineEnding::CrLf) && parsed <= static_cast<long>(LineEnding::Mixed)) {
                meta.lineEnding = static_cast<LineEnding>(parsed);
            }
        } else if (key == L"path") {
            meta.originalPath = value;
        }
    }

    if (!versionOk || meta.processId == 0) {
        return std::nullopt;
    }

    return meta;
}

bool ProcessIsRunning(DWORD processId) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) {
        // Access denied still proves the process exists; every other failure
        // means the id is not a live process.
        return GetLastError() == ERROR_ACCESS_DENIED;
    }

    DWORD exitCode = 0;
    const bool running = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(process);
    return running;
}

} // namespace

RecoveryJournal::RecoveryJournal()
    : rootDirectory_(DefaultRootDirectory()), processId_(GetCurrentProcessId()) {}

RecoveryJournal::RecoveryJournal(std::wstring rootDirectory, DWORD processId)
    : rootDirectory_(std::move(rootDirectory)), processId_(processId) {}

std::wstring RecoveryJournal::MetaPath() const {
    return (std::filesystem::path(rootDirectory_) / (SessionBaseName(processId_) + std::wstring(kMetaExtension))).wstring();
}

std::wstring RecoveryJournal::ContentPath() const {
    return (std::filesystem::path(rootDirectory_) / (SessionBaseName(processId_) + std::wstring(kContentExtension))).wstring();
}

bool RecoveryJournal::Save(const RecoverySnapshot& snapshot) {
    if (rootDirectory_.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(rootDirectory_, ec);
    if (ec) {
        return false;
    }

    if (!WriteContentFile(ContentPath(), snapshot.text)) {
        return false;
    }

    JournalMeta meta;
    meta.processId = processId_;
    meta.originalPath = snapshot.originalPath;
    meta.encoding = snapshot.encoding;
    meta.lineEnding = snapshot.lineEnding;
    return WriteMetaFile(MetaPath(), meta);
}

void RecoveryJournal::Clear() noexcept {
    if (rootDirectory_.empty()) {
        return;
    }

    // Metadata goes first so a partially deleted journal is never claimable.
    DeleteFileW(MetaPath().c_str());
    DeleteFileW(ContentPath().c_str());
}

std::wstring RecoveryJournal::DefaultRootDirectory() {
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path);
    if (FAILED(result) || path == nullptr) {
        return {};
    }

    std::filesystem::path directory(path);
    CoTaskMemFree(path);
    directory /= L"NativePad";
    directory /= L"Recovery";
    return directory.wstring();
}

std::optional<RecoverySnapshot> RecoveryJournal::ClaimAbandoned(const std::wstring& rootDirectory) {
    if (rootDirectory.empty()) {
        return std::nullopt;
    }

    std::error_code ec;
    std::filesystem::directory_iterator it(rootDirectory, ec);
    if (ec) {
        return std::nullopt;
    }

    for (const auto& entry : it) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != kMetaExtension) {
            continue;
        }

        const std::wstring fileName = entry.path().filename().wstring();
        if (fileName.rfind(kSessionPrefix, 0) != 0) {
            continue;
        }

        const std::wstring metaPath = entry.path().wstring();
        const auto meta = ReadMetaFile(metaPath);
        if (!meta) {
            DeleteFileW(metaPath.c_str());
            continue;
        }

        if (ProcessIsRunning(meta->processId)) {
            continue;
        }

        std::filesystem::path contentPath = entry.path();
        contentPath.replace_extension(kContentExtension);
        const auto text = ReadContentFile(contentPath.wstring());

        DeleteFileW(metaPath.c_str());
        DeleteFileW(contentPath.wstring().c_str());

        if (!text) {
            continue;
        }

        RecoverySnapshot snapshot;
        snapshot.originalPath = meta->originalPath;
        snapshot.encoding = meta->encoding;
        snapshot.lineEnding = meta->lineEnding;
        snapshot.text = *text;
        return snapshot;
    }

    return std::nullopt;
}

} // namespace NativePad
