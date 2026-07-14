#include "../src/MappedTextDocument.h"

#include <windows.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void ExpectEqualSize(std::size_t actual, std::size_t expected, const char* name) {
    if (actual != expected) {
        std::cerr << "FAILED: " << name << "\nExpected: " << expected << "\nActual:   " << actual << "\n";
        throw std::runtime_error(name);
    }
}

void ExpectEqualText(const std::wstring& actual, const std::wstring& expected, const char* name) {
    if (actual != expected) {
        std::wcerr << L"FAILED: " << name << L"\nExpected: " << expected << L"\nActual:   " << actual << L"\n";
        throw std::runtime_error(name);
    }
}

std::wstring TempFilePath() {
    wchar_t tempDirectory[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempDirectory) == 0) {
        throw std::runtime_error("GetTempPathW");
    }

    wchar_t tempFile[MAX_PATH]{};
    if (GetTempFileNameW(tempDirectory, L"npd", 0, tempFile) == 0) {
        throw std::runtime_error("GetTempFileNameW");
    }

    return tempFile;
}

void WriteBytes(const std::wstring& path, const void* data, std::size_t byteCount) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("CreateFileW");
    }

    const auto* cursor = static_cast<const unsigned char*>(data);
    std::size_t remaining = byteCount;
    while (remaining > 0) {
        const DWORD toWrite = static_cast<DWORD>(std::min<std::size_t>(remaining, 1024u * 1024u));
        DWORD written = 0;
        if (!WriteFile(file, cursor, toWrite, &written, nullptr) || written == 0) {
            CloseHandle(file);
            throw std::runtime_error("WriteFile");
        }

        cursor += written;
        remaining -= written;
    }

    CloseHandle(file);
}

void AppendBytes(const std::wstring& path, const void* data, std::size_t byteCount) {
    // Share read/write so the append succeeds while the document under test
    // still holds its own read handle on the same file.
    HANDLE file = CreateFileW(
        path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("CreateFileW append");
    }

    DWORD written = 0;
    if (!WriteFile(file, data, static_cast<DWORD>(byteCount), &written, nullptr) || written != byteCount) {
        CloseHandle(file);
        throw std::runtime_error("WriteFile append");
    }

    CloseHandle(file);
}

void OverwriteBytesInPlace(const std::wstring& path, const void* data, std::size_t byteCount) {
    // Truncation is blocked while a section maps the file, so external rewrites
    // that keep the size can only be seen through the last write time. Bump the
    // write time explicitly so the test does not depend on timer resolution.
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("CreateFileW overwrite");
    }

    DWORD written = 0;
    if (!WriteFile(file, data, static_cast<DWORD>(byteCount), &written, nullptr) || written != byteCount) {
        CloseHandle(file);
        throw std::runtime_error("WriteFile overwrite");
    }

    FILETIME writeTime{};
    if (!GetFileTime(file, nullptr, nullptr, &writeTime)) {
        CloseHandle(file);
        throw std::runtime_error("GetFileTime overwrite");
    }

    ULARGE_INTEGER bumped{};
    bumped.LowPart = writeTime.dwLowDateTime;
    bumped.HighPart = writeTime.dwHighDateTime;
    bumped.QuadPart += 10'000'000ull; // one second in 100ns units
    writeTime.dwLowDateTime = bumped.LowPart;
    writeTime.dwHighDateTime = bumped.HighPart;
    if (!SetFileTime(file, nullptr, nullptr, &writeTime)) {
        CloseHandle(file);
        throw std::runtime_error("SetFileTime overwrite");
    }

    CloseHandle(file);
}

void ExpectRefreshStatus(
    NativePad::MappedTextDocument::RefreshStatus actual,
    NativePad::MappedTextDocument::RefreshStatus expected,
    const char* name) {
    if (actual != expected) {
        std::cerr << "FAILED: " << name << "\nExpected status: " << static_cast<int>(expected)
                  << "\nActual status:   " << static_cast<int>(actual) << "\n";
        throw std::runtime_error(name);
    }
}

void RunUtf8MappedDocumentTest() {
    // The mapped backend uses byte offsets for UTF-8/ANSI files; this verifies
    // line starts, range decoding, and search positions all use that convention.
    const std::string bytes = "alpha\r\nbeta line\r\ngamma";
    const std::wstring path = TempFilePath();
    WriteBytes(path, bytes.data(), bytes.size());

    NativePad::MappedTextDocument document;
    std::wstring error;
    if (!document.Open(path, error)) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("open utf8 mapped document");
    }

    ExpectEqualSize(document.Length(), bytes.size(), "mapped utf8 length");
    ExpectEqualSize(document.LineCount(), 3, "mapped utf8 line count");
    ExpectEqualSize(document.LineStart(1), 7, "mapped utf8 line 1 start");
    ExpectEqualSize(document.LineStart(2), 18, "mapped utf8 line 2 start");
    ExpectEqualSize(document.MaxLineLength(), 9, "mapped utf8 max line length");
    ExpectEqualText(document.TextRange(7, 9), L"beta line", "mapped utf8 text range");

    auto match = document.Find(L"line", 0, true, true);
    if (!match) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("mapped utf8 find forward");
    }
    ExpectEqualSize(match->position, 12, "mapped utf8 find position");
    ExpectEqualSize(match->length, 4, "mapped utf8 find length");

    match = document.Find(L"ALPHA", document.Length(), false, false);
    if (!match) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("mapped utf8 find backward case-insensitive");
    }
    ExpectEqualSize(match->position, 0, "mapped utf8 backward find position");

    document.Close();
    DeleteFileW(path.c_str());
}

void RunUtf16MappedDocumentTest() {
    // UTF-16 mapped files use wchar offsets, matching the editable document path.
    const std::wstring text = L"one\r\ntwo";
    std::vector<unsigned char> bytes;
    bytes.push_back(0xFF);
    bytes.push_back(0xFE);
    const auto* raw = reinterpret_cast<const unsigned char*>(text.data());
    bytes.insert(bytes.end(), raw, raw + (text.size() * sizeof(wchar_t)));

    const std::wstring path = TempFilePath();
    WriteBytes(path, bytes.data(), bytes.size());

    NativePad::MappedTextDocument document;
    std::wstring error;
    if (!document.Open(path, error)) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("open utf16 mapped document");
    }

    ExpectEqualSize(document.Length(), text.size(), "mapped utf16 length");
    ExpectEqualSize(document.LineCount(), 2, "mapped utf16 line count");
    ExpectEqualSize(document.LineStart(1), 5, "mapped utf16 line 1 start");
    ExpectEqualText(document.TextRange(5, 3), L"two", "mapped utf16 text range");

    auto match = document.Find(L"TWO", document.Length(), false, false);
    if (!match) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("mapped utf16 find backward case-insensitive");
    }
    ExpectEqualSize(match->position, 5, "mapped utf16 backward find position");

    document.Close();
    DeleteFileW(path.c_str());
}

void RunUtf8RefreshTest() {
    using RefreshStatus = NativePad::MappedTextDocument::RefreshStatus;

    // The initial content ends mid-line so the append both extends an open line
    // across the old mapping boundary and adds new lines behind it.
    const std::string bytes = "alpha\r\nbet";
    const std::wstring path = TempFilePath();
    WriteBytes(path, bytes.data(), bytes.size());

    NativePad::MappedTextDocument document;
    std::wstring error;
    if (!document.Open(path, error)) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("open utf8 refresh document");
    }

    try {
        ExpectRefreshStatus(document.Refresh(error), RefreshStatus::Unchanged, "utf8 refresh unchanged");

        const std::string appended = "a line\r\ngamma delta\r\n";
        AppendBytes(path, appended.data(), appended.size());

        ExpectRefreshStatus(document.Refresh(error), RefreshStatus::Appended, "utf8 refresh appended");
        ExpectEqualSize(document.Length(), bytes.size() + appended.size(), "utf8 refresh length");
        ExpectEqualSize(document.LineCount(), 4, "utf8 refresh line count");
        ExpectEqualSize(document.LineStart(1), 7, "utf8 refresh line 1 start");
        ExpectEqualSize(document.LineStart(2), 18, "utf8 refresh line 2 start");
        ExpectEqualText(document.TextRange(7, 9), L"beta line", "utf8 refresh boundary line");
        ExpectEqualText(document.TextRange(18, 11), L"gamma delta", "utf8 refresh appended line");
        ExpectEqualSize(document.MaxLineLength(), 11, "utf8 refresh max line length");

        auto match = document.Find(L"delta", 0, true, true);
        if (!match) {
            throw std::runtime_error("utf8 refresh find in appended tail");
        }
        ExpectEqualSize(match->position, 24, "utf8 refresh find position");

        // An in-place rewrite keeps the size but must be reported as Replaced,
        // and the document must stay readable at its previously mapped size.
        const std::string replaced = "ALPHA";
        OverwriteBytesInPlace(path, replaced.data(), replaced.size());
        ExpectRefreshStatus(document.Refresh(error), RefreshStatus::Replaced, "utf8 refresh replaced");
        ExpectEqualSize(document.Length(), bytes.size() + appended.size(), "utf8 refresh length after replaced");
    } catch (...) {
        document.Close();
        DeleteFileW(path.c_str());
        throw;
    }

    document.Close();
    DeleteFileW(path.c_str());
}

void RunUtf16RefreshTest() {
    using RefreshStatus = NativePad::MappedTextDocument::RefreshStatus;

    const std::wstring text = L"one\r\ntw";
    std::vector<unsigned char> bytes;
    bytes.push_back(0xFF);
    bytes.push_back(0xFE);
    const auto* raw = reinterpret_cast<const unsigned char*>(text.data());
    bytes.insert(bytes.end(), raw, raw + (text.size() * sizeof(wchar_t)));

    const std::wstring path = TempFilePath();
    WriteBytes(path, bytes.data(), bytes.size());

    NativePad::MappedTextDocument document;
    std::wstring error;
    if (!document.Open(path, error)) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("open utf16 refresh document");
    }

    try {
        const std::wstring appended = L"o\r\nthree";
        AppendBytes(path, appended.data(), appended.size() * sizeof(wchar_t));

        ExpectRefreshStatus(document.Refresh(error), RefreshStatus::Appended, "utf16 refresh appended");
        ExpectEqualSize(document.Length(), text.size() + appended.size(), "utf16 refresh length");
        ExpectEqualSize(document.LineCount(), 3, "utf16 refresh line count");
        ExpectEqualSize(document.LineStart(1), 5, "utf16 refresh line 1 start");
        ExpectEqualSize(document.LineStart(2), 10, "utf16 refresh line 2 start");
        ExpectEqualText(document.TextRange(5, 3), L"two", "utf16 refresh boundary line");
        ExpectEqualText(document.TextRange(10, 5), L"three", "utf16 refresh appended line");
    } catch (...) {
        document.Close();
        DeleteFileW(path.c_str());
        throw;
    }

    document.Close();
    DeleteFileW(path.c_str());
}

} // namespace

void RunMappedTextDocumentTests() {
    RunUtf8MappedDocumentTest();
    RunUtf16MappedDocumentTest();
    RunUtf8RefreshTest();
    RunUtf16RefreshTest();
    std::cout << "MappedTextDocument tests passed\n";
}
