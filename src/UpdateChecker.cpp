#include "UpdateChecker.h"

#include <bcrypt.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <winhttp.h>
#include <winver.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Settings.h"
#include "UiSupport.h"

namespace NativePad {

namespace {

constexpr wchar_t kDefaultUpdateUrl[] = L"https://api.github.com/repos/Wimukthi/NativePad/releases/latest";
constexpr wchar_t kUpdateUrlSetting[] = L"UpdateUrl";
constexpr wchar_t kAutoUpdateSetting[] = L"CheckForUpdates";
constexpr wchar_t kLastUpdateCheckUtcSetting[] = L"LastUpdateCheckUtc";
constexpr DWORD kUpdateCheckIntervalSeconds = 24u * 60u * 60u;
constexpr wchar_t kVersionFallback[] = L"1.0.0.0";

class WinHttpHandle {
public:
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : handle_(handle) {}
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    WinHttpHandle(WinHttpHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    WinHttpHandle& operator=(WinHttpHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    ~WinHttpHandle() {
        Reset();
    }

    [[nodiscard]] HINTERNET Get() const noexcept {
        return handle_;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != nullptr;
    }

private:
    void Reset() noexcept {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
            handle_ = nullptr;
        }
    }

    HINTERNET handle_{};
};

class BcryptAlgHandle {
public:
    BcryptAlgHandle() = default;
    explicit BcryptAlgHandle(BCRYPT_ALG_HANDLE handle) : handle_(handle) {}
    BcryptAlgHandle(const BcryptAlgHandle&) = delete;
    BcryptAlgHandle& operator=(const BcryptAlgHandle&) = delete;
    ~BcryptAlgHandle() {
        if (handle_ != nullptr) {
            BCryptCloseAlgorithmProvider(handle_, 0);
        }
    }

    [[nodiscard]] BCRYPT_ALG_HANDLE Get() const noexcept {
        return handle_;
    }

private:
    BCRYPT_ALG_HANDLE handle_{};
};

class BcryptHashHandle {
public:
    BcryptHashHandle() = default;
    explicit BcryptHashHandle(BCRYPT_HASH_HANDLE handle) : handle_(handle) {}
    BcryptHashHandle(const BcryptHashHandle&) = delete;
    BcryptHashHandle& operator=(const BcryptHashHandle&) = delete;
    ~BcryptHashHandle() {
        if (handle_ != nullptr) {
            BCryptDestroyHash(handle_);
        }
    }

    [[nodiscard]] BCRYPT_HASH_HANDLE Get() const noexcept {
        return handle_;
    }

private:
    BCRYPT_HASH_HANDLE handle_{};
};

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), required);
    return result;
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), required, nullptr, nullptr);
    return result;
}

DWORD UnixSecondsNow() {
    FILETIME fileTime{};
    GetSystemTimeAsFileTime(&fileTime);

    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;

    constexpr unsigned long long kUnixEpochFileTime = 116444736000000000ull;
    const unsigned long long seconds = (value.QuadPart - kUnixEpochFileTime) / 10000000ull;
    return static_cast<DWORD>(std::min<unsigned long long>(seconds, 0xffffffffull));
}

void WriteSettingsBool(const wchar_t* name, bool value) {
    WriteSettingsDword(name, value ? 1u : 0u);
}

void WriteSettingsDwordValue(const wchar_t* name, DWORD value) {
    WriteSettingsDword(name, value);
}

std::wstring NormalizeConfiguredUrl(std::wstring value) {
    const size_t first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return {};
    }

    const size_t last = value.find_last_not_of(L" \t\r\n");
    value = value.substr(first, last - first + 1);

    // INI files are user-editable, so accept the common habit of quoting values
    // while still passing WinHTTP a plain URL.
    if (value.size() >= 2 &&
        ((value.front() == L'"' && value.back() == L'"') || (value.front() == L'\'' && value.back() == L'\''))) {
        value = value.substr(1, value.size() - 2);
    }

    const size_t quotedFirst = value.find_first_not_of(L" \t\r\n");
    if (quotedFirst == std::wstring::npos) {
        return {};
    }
    const size_t quotedLast = value.find_last_not_of(L" \t\r\n");
    return value.substr(quotedFirst, quotedLast - quotedFirst + 1);
}

std::wstring UpdateFeedUrl() {
    // Keep the feed endpoint user-configurable for testing, self-hosted forks,
    // and future release infrastructure without recompiling the application.
    if (auto configuredUrl = ReadSettingsString(kUpdateUrlSetting)) {
        const std::wstring normalized = NormalizeConfiguredUrl(*configuredUrl);
        if (!normalized.empty()) {
            if (normalized != *configuredUrl) {
                WriteSettingsString(kUpdateUrlSetting, normalized);
            }
            return normalized;
        }
    }

    WriteSettingsString(kUpdateUrlSetting, kDefaultUpdateUrl);
    return kDefaultUpdateUrl;
}

std::wstring FormatVersionNumber(DWORD mostSignificant, DWORD leastSignificant) {
    std::wstring version = std::to_wstring(HIWORD(mostSignificant));
    version += L".";
    version += std::to_wstring(LOWORD(mostSignificant));
    version += L".";
    version += std::to_wstring(HIWORD(leastSignificant));
    version += L".";
    version += std::to_wstring(LOWORD(leastSignificant));
    return version;
}

std::wstring ModulePath(HINSTANCE instance) {
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

std::optional<std::array<int, 4>> ParseVersion(std::wstring_view text) {
    if (!text.empty() && (text.front() == L'v' || text.front() == L'V')) {
        text.remove_prefix(1);
    }

    std::array<int, 4> parts{};
    size_t start = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        const size_t end = text.find(L'.', start);
        const std::wstring_view part = text.substr(start, end == std::wstring_view::npos ? text.size() - start : end - start);
        if (part.empty()) {
            return std::nullopt;
        }

        int value = 0;
        for (wchar_t ch : part) {
            if (ch < L'0' || ch > L'9') {
                return std::nullopt;
            }
            value = (value * 10) + static_cast<int>(ch - L'0');
            if (value > 65535) {
                return std::nullopt;
            }
        }
        parts[i] = value;

        if (i + 1 < parts.size()) {
            if (end == std::wstring_view::npos) {
                return std::nullopt;
            }
            start = end + 1;
        } else if (end != std::wstring_view::npos) {
            return std::nullopt;
        }
    }

    return parts;
}

int CompareVersions(std::wstring_view left, std::wstring_view right) {
    const auto leftParts = ParseVersion(left);
    const auto rightParts = ParseVersion(right);
    if (!leftParts || !rightParts) {
        return 0;
    }

    for (size_t i = 0; i < leftParts->size(); ++i) {
        if ((*leftParts)[i] < (*rightParts)[i]) {
            return -1;
        }
        if ((*leftParts)[i] > (*rightParts)[i]) {
            return 1;
        }
    }
    return 0;
}

std::optional<std::string> ParseJsonStringAt(std::string_view text, size_t quotePosition) {
    if (quotePosition >= text.size() || text[quotePosition] != '"') {
        return std::nullopt;
    }

    std::string value;
    for (size_t i = quotePosition + 1; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '"') {
            return value;
        }
        if (ch != '\\') {
            value.push_back(ch);
            continue;
        }

        if (++i >= text.size()) {
            return std::nullopt;
        }

        switch (text[i]) {
        case '"':
        case '\\':
        case '/':
            value.push_back(text[i]);
            break;
        case 'b':
            value.push_back('\b');
            break;
        case 'f':
            value.push_back('\f');
            break;
        case 'n':
            value.push_back('\n');
            break;
        case 'r':
            value.push_back('\r');
            break;
        case 't':
            value.push_back('\t');
            break;
        case 'u':
            if (i + 4 >= text.size()) {
                return std::nullopt;
            }
            // Release names and asset URLs are ASCII today. Preserve parser
            // safety without pulling in a full JSON dependency for rare escapes.
            value.push_back('?');
            i += 4;
            break;
        default:
            return std::nullopt;
        }
    }

    return std::nullopt;
}

std::optional<std::string> JsonStringProperty(std::string_view text, std::string_view name) {
    const std::string property = "\"" + std::string(name) + "\"";
    const size_t propertyPosition = text.find(property);
    if (propertyPosition == std::string_view::npos) {
        return std::nullopt;
    }

    const size_t colon = text.find(':', propertyPosition + property.size());
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }

    const size_t quote = text.find('"', colon + 1);
    return quote == std::string_view::npos ? std::nullopt : ParseJsonStringAt(text, quote);
}

std::optional<size_t> FindMatchingJsonToken(std::string_view text, size_t start, char openToken, char closeToken) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (size_t i = start; i < text.size(); ++i) {
        const char ch = text[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
        } else if (ch == openToken) {
            ++depth;
        } else if (ch == closeToken) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }

    return std::nullopt;
}

std::optional<std::string_view> JsonArrayProperty(std::string_view text, std::string_view name) {
    const std::string property = "\"" + std::string(name) + "\"";
    const size_t propertyPosition = text.find(property);
    if (propertyPosition == std::string_view::npos) {
        return std::nullopt;
    }

    const size_t open = text.find('[', propertyPosition + property.size());
    if (open == std::string_view::npos) {
        return std::nullopt;
    }

    const auto close = FindMatchingJsonToken(text, open, '[', ']');
    if (!close) {
        return std::nullopt;
    }

    return text.substr(open + 1, *close - open - 1);
}

std::vector<std::string_view> JsonObjectsInArray(std::string_view arrayText) {
    std::vector<std::string_view> objects;
    size_t position = 0;
    while (position < arrayText.size()) {
        const size_t open = arrayText.find('{', position);
        if (open == std::string_view::npos) {
            break;
        }

        const auto close = FindMatchingJsonToken(arrayText, open, '{', '}');
        if (!close) {
            break;
        }

        objects.push_back(arrayText.substr(open, *close - open + 1));
        position = *close + 1;
    }
    return objects;
}

bool NameLooksLikeInstaller(std::string_view name) {
    return name.starts_with("NativePadSetup-") && name.ends_with("-win-x64.exe");
}

std::optional<URL_COMPONENTS> CrackHttpsUrl(std::wstring_view url, std::wstring& host, std::wstring& path, std::wstring& error) {
    if (url.empty()) {
        error = L"Update URL is empty.";
        return std::nullopt;
    }
    if (url.size() > static_cast<size_t>(std::numeric_limits<DWORD>::max())) {
        error = L"URL is too long.";
        return std::nullopt;
    }

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    // We request pointers back into the original string and require callers to
    // provide an already-escaped HTTPS URL. Passing ICU_ESCAPE causes WinHTTP to
    // reject otherwise valid URLs with ERROR_INVALID_PARAMETER on this path.
    if (!WinHttpCrackUrl(url.data(), static_cast<DWORD>(url.size()), 0, &components)) {
        error = L"Could not parse update URL: " + GetLastErrorText();
        return std::nullopt;
    }

    if (components.nScheme != INTERNET_SCHEME_HTTPS) {
        error = L"Update URL is not HTTPS.";
        return std::nullopt;
    }

    host.assign(components.lpszHostName, components.dwHostNameLength);
    path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (path.empty()) {
        path = L"/";
    }

    return components;
}

std::optional<std::vector<BYTE>> HttpGetBytes(std::wstring_view url, std::wstring& error) {
    std::wstring host;
    std::wstring path;
    const auto components = CrackHttpsUrl(url, host, path, error);
    if (!components) {
        return std::nullopt;
    }

    WinHttpHandle session(WinHttpOpen(
        L"NativePad Update Checker/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session) {
        error = L"Could not initialize WinHTTP: " + GetLastErrorText();
        return std::nullopt;
    }

    WinHttpHandle connection(WinHttpConnect(session.Get(), host.c_str(), components->nPort, 0));
    if (!connection) {
        error = L"Could not connect to update server: " + GetLastErrorText();
        return std::nullopt;
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connection.Get(),
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (!request) {
        error = L"Could not create update request: " + GetLastErrorText();
        return std::nullopt;
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    const wchar_t headers[] =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";
    WinHttpAddRequestHeaders(request.Get(), headers, static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(request.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.Get(), nullptr)) {
        error = L"Update request failed: " + GetLastErrorText();
        return std::nullopt;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(
            request.Get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusCodeSize,
            WINHTTP_NO_HEADER_INDEX) ||
        statusCode < 200 ||
        statusCode >= 300) {
        error = L"Update server returned HTTP status " + std::to_wstring(statusCode) + L".";
        return std::nullopt;
    }

    std::vector<BYTE> bytes;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.Get(), &available)) {
            error = L"Could not read update response: " + GetLastErrorText();
            return std::nullopt;
        }
        if (available == 0) {
            break;
        }

        std::vector<BYTE> buffer(available);
        DWORD read = 0;
        if (!WinHttpReadData(request.Get(), buffer.data(), available, &read)) {
            error = L"Could not read update response: " + GetLastErrorText();
            return std::nullopt;
        }

        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + read);
    }

    return bytes;
}

std::optional<std::string> Sha256Hex(const std::vector<BYTE>& bytes, std::wstring& error) {
    BCRYPT_ALG_HANDLE rawAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&rawAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) {
        error = L"Could not initialize SHA-256.";
        return std::nullopt;
    }
    BcryptAlgHandle alg(rawAlg);

    DWORD objectLength = 0;
    DWORD resultLength = 0;
    status = BCryptGetProperty(alg.Get(), BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0);
    if (status < 0) {
        error = L"Could not query SHA-256 object length.";
        return std::nullopt;
    }

    DWORD hashLength = 0;
    status = BCryptGetProperty(alg.Get(), BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &resultLength, 0);
    if (status < 0 || hashLength == 0) {
        error = L"Could not query SHA-256 hash length.";
        return std::nullopt;
    }

    std::vector<BYTE> hashObject(objectLength);
    BCRYPT_HASH_HANDLE rawHash = nullptr;
    status = BCryptCreateHash(alg.Get(), &rawHash, hashObject.data(), objectLength, nullptr, 0, 0);
    if (status < 0) {
        error = L"Could not create SHA-256 hash.";
        return std::nullopt;
    }
    BcryptHashHandle hash(rawHash);

    size_t offset = 0;
    while (offset < bytes.size()) {
        const ULONG chunk = static_cast<ULONG>(std::min<size_t>(bytes.size() - offset, 1024u * 1024u));
        status = BCryptHashData(hash.Get(), const_cast<PUCHAR>(bytes.data() + offset), chunk, 0);
        if (status < 0) {
            error = L"Could not hash downloaded installer.";
            return std::nullopt;
        }
        offset += chunk;
    }

    std::vector<BYTE> digest(hashLength);
    status = BCryptFinishHash(hash.Get(), digest.data(), hashLength, 0);
    if (status < 0) {
        error = L"Could not finish SHA-256 hash.";
        return std::nullopt;
    }

    constexpr char hex[] = "0123456789abcdef";
    std::string text;
    text.reserve(digest.size() * 2);
    for (BYTE value : digest) {
        text.push_back(hex[(value >> 4) & 0x0f]);
        text.push_back(hex[value & 0x0f]);
    }
    return text;
}

std::string LowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool VerifyDigest(const std::vector<BYTE>& bytes, std::wstring_view expectedDigest, std::wstring& error) {
    if (expectedDigest.empty()) {
        return true;
    }

    std::string expected = LowerAscii(WideToUtf8(expectedDigest));
    constexpr std::string_view prefix = "sha256:";
    if (!expected.starts_with(prefix)) {
        error = L"Release asset digest uses an unsupported format.";
        return false;
    }
    expected.erase(0, prefix.size());

    const auto actual = Sha256Hex(bytes, error);
    if (!actual) {
        return false;
    }

    if (*actual != expected) {
        error = L"Downloaded installer failed SHA-256 verification.";
        return false;
    }

    return true;
}

std::optional<std::wstring> UpdateDirectory(std::wstring& error) {
    PWSTR localAppData = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData);
    if (FAILED(result) || localAppData == nullptr) {
        error = L"Could not locate the local application data folder.";
        return std::nullopt;
    }

    std::filesystem::path path(localAppData);
    CoTaskMemFree(localAppData);
    path /= L"NativePad";
    path /= L"Updates";

    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        error = L"Could not create the update download folder.";
        return std::nullopt;
    }

    return path.wstring();
}

bool WriteBytesToFile(const std::wstring& path, const std::vector<BYTE>& bytes, std::wstring& error) {
    std::ofstream stream(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = L"Could not create the downloaded installer file.";
        return false;
    }

    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        error = L"Could not write the downloaded installer file.";
        return false;
    }

    return true;
}

std::optional<UpdateInfo> ParseRelease(std::string_view json, HINSTANCE instance, std::wstring& message) {
    const auto tagName = JsonStringProperty(json, "tag_name");
    if (!tagName) {
        message = L"Latest release response did not include a tag name.";
        return std::nullopt;
    }

    const std::wstring latestVersion = Utf8ToWide(*tagName);
    const std::wstring currentVersion = CurrentExecutableVersion(instance);
    if (CompareVersions(latestVersion, currentVersion) <= 0) {
        return UpdateInfo{};
    }

    const auto assets = JsonArrayProperty(json, "assets");
    if (!assets) {
        message = L"Latest release response did not include installer assets.";
        return std::nullopt;
    }

    for (std::string_view asset : JsonObjectsInArray(*assets)) {
        const auto name = JsonStringProperty(asset, "name");
        if (!name || !NameLooksLikeInstaller(*name)) {
            continue;
        }

        const auto url = JsonStringProperty(asset, "browser_download_url");
        if (!url) {
            continue;
        }

        UpdateInfo update;
        update.version = latestVersion;
        update.assetName = Utf8ToWide(*name);
        update.downloadUrl = Utf8ToWide(*url);
        if (const auto digest = JsonStringProperty(asset, "digest")) {
            update.digest = Utf8ToWide(*digest);
        }
        if (const auto htmlUrl = JsonStringProperty(json, "html_url")) {
            update.releaseUrl = Utf8ToWide(*htmlUrl);
        }
        return update;
    }

    message = L"A newer release exists, but it does not include a NativePad installer asset.";
    return std::nullopt;
}

template <typename Result>
void PostOwnedResult(HWND hwnd, UINT message, std::unique_ptr<Result> result) {
    if (!PostMessageW(hwnd, message, 0, reinterpret_cast<LPARAM>(result.get()))) {
        return;
    }
    result.release();
}

} // namespace

bool AutomaticUpdateChecksEnabled() {
    if (auto value = ReadSettingsDword(kAutoUpdateSetting)) {
        return *value != 0;
    }
    return false;
}

void SetAutomaticUpdateChecksEnabled(bool enabled) {
    WriteSettingsBool(kAutoUpdateSetting, enabled);
}

bool AutomaticUpdateCheckDue() {
    if (!AutomaticUpdateChecksEnabled()) {
        return false;
    }

    const DWORD now = UnixSecondsNow();
    if (auto lastCheck = ReadSettingsDword(kLastUpdateCheckUtcSetting)) {
        return now < *lastCheck || now - *lastCheck >= kUpdateCheckIntervalSeconds;
    }
    return true;
}

std::wstring CurrentExecutableVersion(HINSTANCE instance) {
    const std::wstring path = ModulePath(instance);
    if (path.empty()) {
        return kVersionFallback;
    }

    DWORD ignoredHandle = 0;
    const DWORD infoSize = GetFileVersionInfoSizeW(path.c_str(), &ignoredHandle);
    if (infoSize == 0) {
        return kVersionFallback;
    }

    std::vector<BYTE> versionInfo(infoSize);
    if (!GetFileVersionInfoW(path.c_str(), 0, infoSize, versionInfo.data())) {
        return kVersionFallback;
    }

    VS_FIXEDFILEINFO* fixedInfo = nullptr;
    UINT fixedInfoSize = 0;
    if (!VerQueryValueW(versionInfo.data(), L"\\", reinterpret_cast<LPVOID*>(&fixedInfo), &fixedInfoSize) ||
        fixedInfo == nullptr ||
        fixedInfoSize < sizeof(VS_FIXEDFILEINFO) ||
        fixedInfo->dwSignature != 0xfeef04bd) {
        return kVersionFallback;
    }

    return FormatVersionNumber(fixedInfo->dwFileVersionMS, fixedInfo->dwFileVersionLS);
}

void StartUpdateCheck(HWND notifyWindow, HINSTANCE instance, UpdateCheckKind kind) {
    WriteSettingsDwordValue(kLastUpdateCheckUtcSetting, UnixSecondsNow());
    const std::wstring updateUrl = UpdateFeedUrl();

    std::thread([notifyWindow, instance, kind, updateUrl]() {
        auto result = std::make_unique<UpdateCheckResult>();
        result->kind = kind;

        std::wstring error;
        const auto bytes = HttpGetBytes(updateUrl, error);
        if (!bytes) {
            result->message = error;
            PostOwnedResult(notifyWindow, WM_NATIVEPAD_UPDATE_CHECK_COMPLETE, std::move(result));
            return;
        }

        const std::string json(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        auto update = ParseRelease(json, instance, result->message);
        if (!update) {
            result->success = false;
            PostOwnedResult(notifyWindow, WM_NATIVEPAD_UPDATE_CHECK_COMPLETE, std::move(result));
            return;
        }

        result->success = true;
        if (!update->version.empty()) {
            result->updateAvailable = true;
            result->update = std::move(*update);
        }

        PostOwnedResult(notifyWindow, WM_NATIVEPAD_UPDATE_CHECK_COMPLETE, std::move(result));
    }).detach();
}

void StartUpdateDownload(HWND notifyWindow, UpdateInfo update) {
    std::thread([notifyWindow, update = std::move(update)]() mutable {
        auto result = std::make_unique<UpdateDownloadResult>();
        result->update = std::move(update);

        std::wstring error;
        const auto bytes = HttpGetBytes(result->update.downloadUrl, error);
        if (!bytes) {
            result->message = error;
            PostOwnedResult(notifyWindow, WM_NATIVEPAD_UPDATE_DOWNLOAD_COMPLETE, std::move(result));
            return;
        }

        if (!VerifyDigest(*bytes, result->update.digest, error)) {
            result->message = error;
            PostOwnedResult(notifyWindow, WM_NATIVEPAD_UPDATE_DOWNLOAD_COMPLETE, std::move(result));
            return;
        }

        const auto directory = UpdateDirectory(error);
        if (!directory) {
            result->message = error;
            PostOwnedResult(notifyWindow, WM_NATIVEPAD_UPDATE_DOWNLOAD_COMPLETE, std::move(result));
            return;
        }

        std::filesystem::path installerPath(*directory);
        installerPath /= result->update.assetName;
        if (!WriteBytesToFile(installerPath.wstring(), *bytes, error)) {
            result->message = error;
            PostOwnedResult(notifyWindow, WM_NATIVEPAD_UPDATE_DOWNLOAD_COMPLETE, std::move(result));
            return;
        }

        result->success = true;
        result->installerPath = installerPath.wstring();
        PostOwnedResult(notifyWindow, WM_NATIVEPAD_UPDATE_DOWNLOAD_COMPLETE, std::move(result));
    }).detach();
}

} // namespace NativePad
