#include "SessionStore.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <set>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace NativePad {

namespace {

constexpr std::uint32_t kSessionVersion = 1;
constexpr std::string_view kSessionMagic = "NPSESSION";
constexpr std::wstring_view kManifestName = L"session.bin";
constexpr std::wstring_view kTabPrefix = L"tab-";
constexpr std::size_t kMaximumTabs = 4096;
constexpr std::uint64_t kMaximumManifestString = 32768;

constexpr std::uint32_t kFlagDirty = 1u << 0;
constexpr std::uint32_t kFlagReadOnlyPreview = 1u << 1;
constexpr std::uint32_t kFlagFollowTail = 1u << 2;
constexpr std::uint32_t kFlagHasSnapshot = 1u << 3;

std::wstring LastErrorText(DWORD error = GetLastError()) {
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        reinterpret_cast<wchar_t*>(&message),
        0,
        nullptr);
    std::wstring result = length != 0 && message != nullptr ? std::wstring(message, length) : L"Windows error " + std::to_wstring(error);
    if (message != nullptr) {
        LocalFree(message);
    }
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) {
        result.pop_back();
    }
    return result;
}

std::wstring ManifestPath(std::wstring_view root) {
    return (std::filesystem::path(root) / kManifestName).wstring();
}

bool WriteFileBytes(const std::wstring& path, const void* data, std::size_t byteCount, std::wstring& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = LastErrorText();
        return false;
    }

    const auto* cursor = static_cast<const unsigned char*>(data);
    std::size_t remaining = byteCount;
    bool ok = true;
    while (remaining > 0) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 1024u * 1024u));
        DWORD written = 0;
        if (!WriteFile(file, cursor, chunk, &written, nullptr) || written != chunk) {
            error = LastErrorText();
            ok = false;
            break;
        }
        cursor += written;
        remaining -= written;
    }
    if (ok && !FlushFileBuffers(file)) {
        error = LastErrorText();
        ok = false;
    }
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(path.c_str());
    }
    return ok;
}

bool ReadFileBytes(const std::wstring& path, std::vector<unsigned char>& bytes, std::wstring& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = LastErrorText();
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        static_cast<unsigned long long>(size.QuadPart) > std::numeric_limits<std::size_t>::max()) {
        error = L"The session file has an invalid size.";
        CloseHandle(file);
        return false;
    }

    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1024u * 1024u));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + offset, chunk, &read, nullptr) || read != chunk) {
            error = LastErrorText();
            CloseHandle(file);
            return false;
        }
        offset += read;
    }
    CloseHandle(file);
    return true;
}

bool WriteTextSnapshot(const std::wstring& path, const std::wstring& text, std::wstring& error) {
    std::vector<unsigned char> bytes;
    bytes.reserve(2 + text.size() * sizeof(wchar_t));
    bytes.push_back(0xFF);
    bytes.push_back(0xFE);
    const auto* raw = reinterpret_cast<const unsigned char*>(text.data());
    bytes.insert(bytes.end(), raw, raw + text.size() * sizeof(wchar_t));
    return WriteFileBytes(path, bytes.data(), bytes.size(), error);
}

bool ReadTextSnapshot(const std::wstring& path, std::wstring& text, std::wstring& error) {
    std::vector<unsigned char> bytes;
    if (!ReadFileBytes(path, bytes, error)) {
        return false;
    }
    if (bytes.size() < 2 || bytes[0] != 0xFF || bytes[1] != 0xFE || (bytes.size() - 2) % sizeof(wchar_t) != 0) {
        error = L"The saved tab content is invalid.";
        return false;
    }
    const std::size_t charCount = (bytes.size() - 2) / sizeof(wchar_t);
    text.resize(charCount);
    if (charCount != 0) {
        memcpy(text.data(), bytes.data() + 2, charCount * sizeof(wchar_t));
    }
    return true;
}

template <typename T>
void AppendValue(std::vector<unsigned char>& bytes, T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* raw = reinterpret_cast<const unsigned char*>(&value);
    bytes.insert(bytes.end(), raw, raw + sizeof(T));
}

void AppendString(std::vector<unsigned char>& bytes, std::wstring_view text) {
    AppendValue(bytes, static_cast<std::uint64_t>(text.size()));
    const auto* raw = reinterpret_cast<const unsigned char*>(text.data());
    bytes.insert(bytes.end(), raw, raw + text.size() * sizeof(wchar_t));
}

template <typename T>
bool ReadValue(const std::vector<unsigned char>& bytes, std::size_t& offset, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
        return false;
    }
    memcpy(&value, bytes.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

bool ReadString(const std::vector<unsigned char>& bytes, std::size_t& offset, std::wstring& text) {
    std::uint64_t length = 0;
    if (!ReadValue(bytes, offset, length) || length > kMaximumManifestString ||
        length > (bytes.size() - std::min(offset, bytes.size())) / sizeof(wchar_t)) {
        return false;
    }
    text.resize(static_cast<std::size_t>(length));
    if (length != 0) {
        memcpy(text.data(), bytes.data() + offset, static_cast<std::size_t>(length) * sizeof(wchar_t));
    }
    offset += static_cast<std::size_t>(length) * sizeof(wchar_t);
    return true;
}

bool IsSafeContentFileName(std::wstring_view name) {
    if (name.empty()) {
        return true;
    }
    const std::filesystem::path path(name);
    return !path.has_parent_path() && path.filename().wstring() == name && name.rfind(kTabPrefix, 0) == 0;
}

void RemoveFiles(const std::vector<std::wstring>& paths) noexcept {
    for (const auto& path : paths) {
        DeleteFileW(path.c_str());
    }
}

} // namespace

SessionStore::SessionStore() : rootDirectory_(DefaultRootDirectory()) {}

SessionStore::SessionStore(std::wstring rootDirectory) : rootDirectory_(std::move(rootDirectory)) {}

bool SessionStore::Save(
    const SessionState& state,
    const LargeSessionSnapshotWriter& largeWriter,
    std::wstring& error) const {
    error.clear();
    if (rootDirectory_.empty()) {
        error = L"The session directory is unavailable.";
        return false;
    }
    if (state.tabs.empty() || state.tabs.size() > kMaximumTabs) {
        error = L"The session has an invalid tab count.";
        return false;
    }

    std::error_code filesystemError;
    std::filesystem::create_directories(rootDirectory_, filesystemError);
    if (filesystemError) {
        error = L"Could not create the session directory.";
        return false;
    }

    const std::wstring generation = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    std::vector<std::wstring> contentFileNames(state.tabs.size());
    std::vector<std::wstring> createdPaths;
    std::set<std::wstring> referencedNames;

    for (std::size_t i = 0; i < state.tabs.size(); ++i) {
        const SessionTabState& tab = state.tabs[i];
        const bool snapshotText = tab.kind == SessionDocumentKind::Normal &&
                                  (tab.dirty || tab.currentPath.empty());
        const bool snapshotLarge = tab.kind == SessionDocumentKind::LargeEditable && tab.dirty;
        if (snapshotText || snapshotLarge) {
            const wchar_t* extension = snapshotText ? L".txt" : L".large";
            contentFileNames[i] = std::wstring(kTabPrefix) + generation + L"-" + std::to_wstring(i) + extension;
            const std::wstring contentPath = (std::filesystem::path(rootDirectory_) / contentFileNames[i]).wstring();
            const bool written = snapshotText
                                     ? WriteTextSnapshot(contentPath, tab.text, error)
                                     : largeWriter && largeWriter(i, contentPath, error);
            if (!written) {
                if (error.empty()) {
                    error = L"Could not save the tab content.";
                }
                RemoveFiles(createdPaths);
                DeleteFileW(contentPath.c_str());
                return false;
            }
            createdPaths.push_back(contentPath);
            referencedNames.insert(contentFileNames[i]);
        }
    }

    std::vector<unsigned char> manifest;
    manifest.insert(manifest.end(), kSessionMagic.begin(), kSessionMagic.end());
    AppendValue(manifest, kSessionVersion);
    AppendValue(manifest, static_cast<std::uint64_t>(std::min(state.activeIndex, state.tabs.size() - 1)));
    AppendValue(manifest, static_cast<std::uint64_t>(state.tabs.size()));
    for (std::size_t i = 0; i < state.tabs.size(); ++i) {
        const SessionTabState& tab = state.tabs[i];
        AppendValue(manifest, static_cast<std::uint32_t>(tab.kind));
        AppendValue(manifest, static_cast<std::uint32_t>(tab.encoding));
        AppendValue(manifest, static_cast<std::uint32_t>(tab.lineEnding));
        std::uint32_t flags = 0;
        flags |= tab.dirty ? kFlagDirty : 0;
        flags |= tab.readOnlyPreview ? kFlagReadOnlyPreview : 0;
        flags |= tab.followTail ? kFlagFollowTail : 0;
        flags |= !contentFileNames[i].empty() ? kFlagHasSnapshot : 0;
        AppendValue(manifest, flags);
        AppendValue(manifest, tab.untitledNumber);
        AppendString(manifest, tab.currentPath);
        AppendString(manifest, contentFileNames[i]);
    }

    const std::wstring stagingPath =
        (std::filesystem::path(rootDirectory_) / (L"session-" + generation + L".tmp")).wstring();
    if (!WriteFileBytes(stagingPath, manifest.data(), manifest.size(), error) ||
        !MoveFileExW(stagingPath.c_str(), ManifestPath(rootDirectory_).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (error.empty()) {
            error = LastErrorText();
        }
        DeleteFileW(stagingPath.c_str());
        RemoveFiles(createdPaths);
        return false;
    }

    std::filesystem::directory_iterator iterator(rootDirectory_, filesystemError);
    if (!filesystemError) {
        for (const auto& entry : iterator) {
            const std::wstring name = entry.path().filename().wstring();
            if (entry.is_regular_file(filesystemError) && name.rfind(kTabPrefix, 0) == 0 && !referencedNames.contains(name)) {
                DeleteFileW(entry.path().wstring().c_str());
            }
        }
    }
    return true;
}

SessionLoadStatus SessionStore::LoadAndConsume(SessionState& state, std::wstring& error) const {
    state = {};
    error.clear();
    const std::wstring manifestPath = ManifestPath(rootDirectory_);
    if (GetFileAttributesW(manifestPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return SessionLoadStatus::NotFound;
    }

    std::vector<unsigned char> manifest;
    if (!ReadFileBytes(manifestPath, manifest, error)) {
        return SessionLoadStatus::Failed;
    }
    std::size_t offset = 0;
    if (manifest.size() < kSessionMagic.size() ||
        !std::equal(kSessionMagic.begin(), kSessionMagic.end(), manifest.begin())) {
        error = L"The saved session has an invalid header.";
        return SessionLoadStatus::Failed;
    }
    offset += kSessionMagic.size();

    std::uint32_t version = 0;
    std::uint64_t activeIndex = 0;
    std::uint64_t tabCount = 0;
    if (!ReadValue(manifest, offset, version) || version != kSessionVersion ||
        !ReadValue(manifest, offset, activeIndex) || !ReadValue(manifest, offset, tabCount) ||
        tabCount == 0 || tabCount > kMaximumTabs) {
        error = L"The saved session has unsupported metadata.";
        return SessionLoadStatus::Failed;
    }

    struct PendingTab {
        SessionTabState state;
        std::wstring contentFileName;
    };
    std::vector<PendingTab> pending;
    pending.reserve(static_cast<std::size_t>(tabCount));
    for (std::uint64_t i = 0; i < tabCount; ++i) {
        std::uint32_t kind = 0;
        std::uint32_t encoding = 0;
        std::uint32_t lineEnding = 0;
        std::uint32_t flags = 0;
        PendingTab tab;
        if (!ReadValue(manifest, offset, kind) || kind > static_cast<std::uint32_t>(SessionDocumentKind::LargeEditable) ||
            !ReadValue(manifest, offset, encoding) || encoding > static_cast<std::uint32_t>(TextEncoding::Oem437) ||
            !ReadValue(manifest, offset, lineEnding) || lineEnding > static_cast<std::uint32_t>(LineEnding::Mixed) ||
            !ReadValue(manifest, offset, flags) || !ReadValue(manifest, offset, tab.state.untitledNumber) ||
            !ReadString(manifest, offset, tab.state.currentPath) ||
            !ReadString(manifest, offset, tab.contentFileName) || !IsSafeContentFileName(tab.contentFileName)) {
            error = L"The saved session contains an invalid tab record.";
            return SessionLoadStatus::Failed;
        }
        tab.state.kind = static_cast<SessionDocumentKind>(kind);
        tab.state.encoding = static_cast<TextEncoding>(encoding);
        tab.state.lineEnding = static_cast<LineEnding>(lineEnding);
        tab.state.dirty = (flags & kFlagDirty) != 0;
        tab.state.readOnlyPreview = (flags & kFlagReadOnlyPreview) != 0;
        tab.state.followTail = (flags & kFlagFollowTail) != 0;
        tab.state.hasTextSnapshot = (flags & kFlagHasSnapshot) != 0 && tab.state.kind == SessionDocumentKind::Normal;
        if (((flags & kFlagHasSnapshot) != 0) != !tab.contentFileName.empty()) {
            error = L"The saved session has inconsistent content metadata.";
            return SessionLoadStatus::Failed;
        }
        pending.push_back(std::move(tab));
    }
    if (offset != manifest.size()) {
        error = L"The saved session has trailing data.";
        return SessionLoadStatus::Failed;
    }

    std::vector<std::wstring> consumedTextPaths;
    for (auto& tab : pending) {
        if (tab.contentFileName.empty()) {
            state.tabs.push_back(std::move(tab.state));
            continue;
        }
        const std::wstring contentPath = (std::filesystem::path(rootDirectory_) / tab.contentFileName).wstring();
        if (tab.state.kind == SessionDocumentKind::Normal) {
            if (!ReadTextSnapshot(contentPath, tab.state.text, error)) {
                return SessionLoadStatus::Failed;
            }
            consumedTextPaths.push_back(contentPath);
        } else if (GetFileAttributesW(contentPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            error = L"A saved large-file tab is missing its content snapshot.";
            return SessionLoadStatus::Failed;
        } else {
            tab.state.largeSnapshotPath = contentPath;
        }
        state.tabs.push_back(std::move(tab.state));
    }

    state.activeIndex = static_cast<std::size_t>(std::min<std::uint64_t>(activeIndex, state.tabs.size() - 1));
    DeleteFileW(manifestPath.c_str());
    RemoveFiles(consumedTextPaths);
    return SessionLoadStatus::Loaded;
}

void SessionStore::Clear() const noexcept {
    if (rootDirectory_.empty()) {
        return;
    }
    DeleteFileW(ManifestPath(rootDirectory_).c_str());
    std::error_code error;
    std::filesystem::directory_iterator iterator(rootDirectory_, error);
    if (error) {
        return;
    }
    for (const auto& entry : iterator) {
        const std::wstring name = entry.path().filename().wstring();
        if (entry.is_regular_file(error) &&
            (name.rfind(kTabPrefix, 0) == 0 || (name.rfind(L"session-", 0) == 0 && entry.path().extension() == L".tmp"))) {
            DeleteFileW(entry.path().wstring().c_str());
        }
    }
}

const std::wstring& SessionStore::RootDirectory() const noexcept {
    return rootDirectory_;
}

std::wstring SessionStore::DefaultRootDirectory() {
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path);
    if (FAILED(result) || path == nullptr) {
        return {};
    }
    std::filesystem::path directory(path);
    CoTaskMemFree(path);
    directory /= L"NativePad";
    directory /= L"Session";
    return directory.wstring();
}

} // namespace NativePad
