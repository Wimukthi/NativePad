#include "FileCodec.h"

#include <commdlg.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <strsafe.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "UiSupport.h"

namespace NativePad {

namespace {

constexpr size_t kLargeFilePreviewBytes = 16u * 1024u * 1024u;
constexpr DWORD kSaveEncodingComboId = 50301;

struct SaveEncodingOption {
    DWORD id{};
    NativePad::TextEncoding encoding{NativePad::TextEncoding::Utf8};
    const wchar_t* label{};
};

constexpr std::array<SaveEncodingOption, 5> kSaveEncodingOptions{{
    {1, NativePad::TextEncoding::Utf8, L"UTF-8"},
    {2, NativePad::TextEncoding::Utf8Bom, L"UTF-8 with BOM"},
    {3, NativePad::TextEncoding::Utf16Le, L"UTF-16 LE"},
    {4, NativePad::TextEncoding::Utf16Be, L"UTF-16 BE"},
    {5, NativePad::TextEncoding::Ansi, L"ANSI"},
}};

const SaveEncodingOption& SaveEncodingOptionForEncoding(NativePad::TextEncoding encoding) noexcept {
    for (const auto& option : kSaveEncodingOptions) {
        if (option.encoding == encoding) {
            return option;
        }
    }

    return kSaveEncodingOptions.front();
}

const SaveEncodingOption& SaveEncodingOptionForId(DWORD id) noexcept {
    for (const auto& option : kSaveEncodingOptions) {
        if (option.id == id) {
            return option;
        }
    }

    return kSaveEncodingOptions.front();
}

std::optional<std::wstring> MultiByteToWide(UINT codePage, const char* data, int byteCount, DWORD flags) {
    if (byteCount == 0) {
        return std::wstring();
    }

    const int required = MultiByteToWideChar(codePage, flags, data, byteCount, nullptr, 0);
    if (required <= 0) {
        return std::nullopt;
    }

    std::wstring text(static_cast<size_t>(required), L'\0');
    const int written = MultiByteToWideChar(codePage, flags, data, byteCount, text.data(), required);
    if (written <= 0) {
        return std::nullopt;
    }

    return text;
}

std::wstring Utf16FromLeBytes(const char* bytes, size_t size, size_t offset) {
    const size_t byteCount = size - offset;
    const size_t wcharCount = byteCount / sizeof(wchar_t);
    std::wstring text(wcharCount, L'\0');
    if (wcharCount > 0) {
        memcpy(text.data(), bytes + offset, wcharCount * sizeof(wchar_t));
    }
    return text;
}

std::wstring Utf16FromBeBytes(const char* bytes, size_t size, size_t offset) {
    const size_t byteCount = size - offset;
    const size_t wcharCount = byteCount / sizeof(wchar_t);
    std::wstring text;
    text.reserve(wcharCount);

    for (size_t i = 0; i + 1 < byteCount; i += 2) {
        const auto high = static_cast<unsigned char>(bytes[offset + i]);
        const auto low = static_cast<unsigned char>(bytes[offset + i + 1]);
        text.push_back(static_cast<wchar_t>((high << 8) | low));
    }

    return text;
}

DecodedFile MakeDecodedFile(std::wstring text, NativePad::TextEncoding encoding, size_t decodedByteCount) {
    DecodedFile decoded;
    decoded.text = std::move(text);
    decoded.encoding = encoding;
    decoded.encodingLabel = NativePad::EncodingLabel(encoding);
    decoded.lineEnding = NativePad::DetectLineEnding(decoded.text);
    decoded.decodedByteCount = decodedByteCount;
    return decoded;
}

std::optional<std::wstring> DecodeUtf8BestEffort(const char* bytes, size_t size) {
    for (size_t trim = 0; trim <= 3 && trim <= size; ++trim) {
        if (auto text = MultiByteToWide(CP_UTF8, bytes, static_cast<int>(size - trim), MB_ERR_INVALID_CHARS)) {
            return text;
        }
    }

    return std::nullopt;
}

DecodedFile DecodeBytes(const char* bytes, size_t size, bool allowTruncatedUtf8 = false) {
    // Small/editable files are decoded completely into UTF-16 for the piece table.
    // Large files bypass this path and use MappedTextDocument instead.
    if (size >= 2) {
        const auto b0 = static_cast<unsigned char>(bytes[0]);
        const auto b1 = static_cast<unsigned char>(bytes[1]);

        if (b0 == 0xFF && b1 == 0xFE) {
            return MakeDecodedFile(Utf16FromLeBytes(bytes, size, 2), NativePad::TextEncoding::Utf16Le, size);
        }

        if (b0 == 0xFE && b1 == 0xFF) {
            return MakeDecodedFile(Utf16FromBeBytes(bytes, size, 2), NativePad::TextEncoding::Utf16Be, size);
        }
    }

    if (size >= 3) {
        const auto b0 = static_cast<unsigned char>(bytes[0]);
        const auto b1 = static_cast<unsigned char>(bytes[1]);
        const auto b2 = static_cast<unsigned char>(bytes[2]);

        if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) {
            if (auto text = MultiByteToWide(CP_UTF8, bytes + 3, static_cast<int>(size - 3), MB_ERR_INVALID_CHARS)) {
                return MakeDecodedFile(*text, NativePad::TextEncoding::Utf8Bom, size);
            }
            if (allowTruncatedUtf8) {
                if (auto text = DecodeUtf8BestEffort(bytes + 3, size - 3)) {
                    return MakeDecodedFile(*text, NativePad::TextEncoding::Utf8Bom, size);
                }
            }
        }
    }

    if (auto text = MultiByteToWide(CP_UTF8, bytes, static_cast<int>(size), MB_ERR_INVALID_CHARS)) {
        return MakeDecodedFile(*text, NativePad::TextEncoding::Utf8, size);
    }

    if (allowTruncatedUtf8) {
        if (auto text = DecodeUtf8BestEffort(bytes, size)) {
            return MakeDecodedFile(*text, NativePad::TextEncoding::Utf8, size);
        }
    }

    if (auto text = MultiByteToWide(CP_ACP, bytes, static_cast<int>(size), 0)) {
        return MakeDecodedFile(*text, NativePad::TextEncoding::Ansi, size);
    }

    return MakeDecodedFile(L"", NativePad::TextEncoding::Utf8, size);
}

std::optional<DecodedFile> ReadTextFileWithReadFile(HANDLE file, size_t byteCount, std::wstring& error) {
    std::vector<char> bytes(byteCount);
    char* cursor = bytes.data();
    size_t remaining = bytes.size();

    while (remaining > 0) {
        const DWORD toRead = static_cast<DWORD>(std::min<size_t>(remaining, 16u * 1024u * 1024u));
        DWORD read = 0;
        if (!ReadFile(file, cursor, toRead, &read, nullptr)) {
            error = GetLastErrorText();
            return std::nullopt;
        }

        if (read == 0) {
            break;
        }

        cursor += read;
        remaining -= read;
    }

    return DecodeBytes(bytes.data(), bytes.size());
}

std::optional<DecodedFile> ReadLargeFilePreview(HANDLE file, uint64_t fileByteCount, std::wstring& error) {
    const size_t previewBytes = static_cast<size_t>(std::min<uint64_t>(fileByteCount, kLargeFilePreviewBytes));
    std::vector<char> bytes(previewBytes);
    char* cursor = bytes.data();
    size_t remaining = bytes.size();

    while (remaining > 0) {
        const DWORD toRead = static_cast<DWORD>(std::min<size_t>(remaining, 4u * 1024u * 1024u));
        DWORD read = 0;
        if (!ReadFile(file, cursor, toRead, &read, nullptr)) {
            error = GetLastErrorText();
            return std::nullopt;
        }

        if (read == 0) {
            break;
        }

        cursor += read;
        remaining -= read;
    }

    DecodedFile decoded = DecodeBytes(bytes.data(), bytes.size(), true);
    decoded.fileByteCount = fileByteCount;
    decoded.decodedByteCount = bytes.size();
    decoded.readOnlyPreview = true;
    decoded.truncated = fileByteCount > bytes.size();

    if (decoded.truncated) {
        decoded.text += L"\r\n\r\n--- NativePad read-only preview: file is larger than the editable limit; showing the first ";
        decoded.text += std::to_wstring(decoded.decodedByteCount / (1024u * 1024u));
        decoded.text += L" MB of ";
        decoded.text += std::to_wstring(fileByteCount / (1024u * 1024u));
        decoded.text += L" MB. ---\r\n";
    }

    return decoded;
}

std::optional<SaveDialogResult> ShowLegacySaveDialog(
    HWND owner,
    const std::wstring& currentPath,
    NativePad::TextEncoding currentEncoding) {
    std::array<wchar_t, 32768> buffer{};
    if (!currentPath.empty()) {
        StringCchCopyW(buffer.data(), buffer.size(), currentPath.c_str());
    }

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrDefExt = L"txt";
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;

    if (!GetSaveFileNameW(&ofn)) {
        return std::nullopt;
    }

    return SaveDialogResult{std::wstring(buffer.data()), currentEncoding};
}

std::wstring FileNamePart(const std::wstring& path) {
    const std::wstring::size_type separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

std::wstring ParentDirectoryPart(const std::wstring& path) {
    const std::wstring::size_type separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return {};
    }

    if (separator == 2 && path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) {
        return path.substr(0, 3);
    }

    return path.substr(0, separator);
}

} // namespace

std::optional<DecodedFile> ReadTextFile(const std::wstring& path, std::wstring& error) {
    // This loader is intentionally capped by kReadChunkLimit. Files above that
    // threshold are opened through the read-only memory-mapped backend.
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastErrorText();
        return std::nullopt;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        error = GetLastErrorText();
        CloseHandle(file);
        return std::nullopt;
    }

    if (size.QuadPart > kReadChunkLimit) {
        auto preview = ReadLargeFilePreview(file, static_cast<uint64_t>(size.QuadPart), error);
        CloseHandle(file);
        return preview;
    }

    const size_t byteCount = static_cast<size_t>(size.QuadPart);
    if (byteCount == 0) {
        CloseHandle(file);
        DecodedFile decoded = DecodeBytes(nullptr, 0);
        decoded.fileByteCount = 0;
        decoded.decodedByteCount = 0;
        return decoded;
    }

    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        auto fallback = ReadTextFileWithReadFile(file, byteCount, error);
        CloseHandle(file);
        return fallback;
    }

    const auto* mapped = static_cast<const char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    if (mapped == nullptr) {
        CloseHandle(mapping);
        SetFilePointer(file, 0, nullptr, FILE_BEGIN);
        auto fallback = ReadTextFileWithReadFile(file, byteCount, error);
        CloseHandle(file);
        return fallback;
    }

    DecodedFile decoded = DecodeBytes(mapped, byteCount);
    decoded.fileByteCount = byteCount;
    decoded.decodedByteCount = byteCount;
    UnmapViewOfFile(mapped);
    CloseHandle(mapping);
    CloseHandle(file);
    return decoded;
}

std::optional<uint64_t> FileByteCountForPath(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        return std::nullopt;
    }

    ULARGE_INTEGER size{};
    size.HighPart = attributes.nFileSizeHigh;
    size.LowPart = attributes.nFileSizeLow;
    return size.QuadPart;
}

bool WriteTextFile(
    const std::wstring& path,
    std::wstring_view text,
    NativePad::TextEncoding encoding,
    NativePad::LineEnding lineEnding,
    std::wstring& error) {
    // Encode into one contiguous buffer first so the file is only truncated
    // after NativePad knows the target format can represent the document.
    auto bytes = NativePad::EncodeTextBytes(text, encoding, lineEnding, error);
    if (!bytes) {
        return false;
    }

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastErrorText();
        return false;
    }

    const char* cursor = bytes->data();
    size_t remaining = bytes->size();

    while (remaining > 0) {
        const DWORD toWrite = static_cast<DWORD>(std::min<size_t>(remaining, 16u * 1024u * 1024u));
        DWORD written = 0;
        if (!WriteFile(file, cursor, toWrite, &written, nullptr)) {
            error = GetLastErrorText();
            CloseHandle(file);
            return false;
        }

        cursor += written;
        remaining -= written;
    }

    CloseHandle(file);
    return true;
}

std::optional<std::wstring> ShowOpenDialog(HWND owner) {
    std::array<wchar_t, 32768> buffer{};

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&ofn)) {
        return std::nullopt;
    }

    return std::wstring(buffer.data());
}

std::optional<SaveDialogResult> ShowSaveDialog(
    HWND owner,
    const std::wstring& currentPath,
    NativePad::TextEncoding currentEncoding) {
    using Microsoft::WRL::ComPtr;

    ComPtr<IFileSaveDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) {
        return ShowLegacySaveDialog(owner, currentPath, currentEncoding);
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
    }

    COMDLG_FILTERSPEC filters[] = {
        {L"Text Documents (*.txt)", L"*.txt"},
        {L"All Files (*.*)", L"*.*"},
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetFileTypeIndex(1);
    dialog->SetDefaultExtension(L"txt");
    dialog->SetTitle(L"Save As");

    if (!currentPath.empty()) {
        const std::wstring fileName = FileNamePart(currentPath);
        if (!fileName.empty()) {
            dialog->SetFileName(fileName.c_str());
        }

        const std::wstring parent = ParentDirectoryPart(currentPath);
        if (!parent.empty()) {
            ComPtr<IShellItem> folder;
            if (SUCCEEDED(SHCreateItemFromParsingName(parent.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                dialog->SetFolder(folder.Get());
            }
        }
    }

    ComPtr<IFileDialogCustomize> customize;
    const bool hasEncodingPicker = SUCCEEDED(dialog.As(&customize));
    if (hasEncodingPicker) {
        // The modern file dialog owns layout and accessibility; NativePad only
        // contributes the classic Notepad-style encoding choice.
        customize->AddComboBox(kSaveEncodingComboId);
        customize->SetControlLabel(kSaveEncodingComboId, L"Encoding:");
        for (const auto& option : kSaveEncodingOptions) {
            customize->AddControlItem(kSaveEncodingComboId, option.id, option.label);
        }
        customize->SetSelectedControlItem(kSaveEncodingComboId, SaveEncodingOptionForEncoding(currentEncoding).id);
    }

    hr = dialog->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return std::nullopt;
    }
    if (FAILED(hr)) {
        return std::nullopt;
    }

    DWORD selectedEncodingId = SaveEncodingOptionForEncoding(currentEncoding).id;
    if (hasEncodingPicker) {
        customize->GetSelectedControlItem(kSaveEncodingComboId, &selectedEncodingId);
    }

    ComPtr<IShellItem> result;
    if (FAILED(dialog->GetResult(&result))) {
        return std::nullopt;
    }

    PWSTR rawPath = nullptr;
    const HRESULT pathHr = result->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
    if (FAILED(pathHr) || rawPath == nullptr) {
        if (rawPath != nullptr) {
            CoTaskMemFree(rawPath);
        }
        return std::nullopt;
    }

    SaveDialogResult saveResult{std::wstring(rawPath), SaveEncodingOptionForId(selectedEncodingId).encoding};
    CoTaskMemFree(rawPath);
    return saveResult;
}

} // namespace NativePad
