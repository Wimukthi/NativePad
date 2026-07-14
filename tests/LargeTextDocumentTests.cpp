#include "../src/LargeTextDocument.h"

#include <windows.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void ExpectSize(std::size_t actual, std::size_t expected, const char* name) {
    if (actual != expected) {
        std::cerr << "FAILED: " << name << "\nExpected: " << expected << "\nActual:   " << actual << "\n";
        throw std::runtime_error(name);
    }
}

void ExpectText(const std::wstring& actual, const std::wstring& expected, const char* name) {
    if (actual != expected) {
        std::wcerr << L"FAILED: " << name << L"\nExpected: [" << expected << L"]\nActual:   [" << actual << L"]\n";
        throw std::runtime_error(name);
    }
}

void ExpectTrue(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAILED: " << name << "\n";
        throw std::runtime_error(name);
    }
}

std::wstring TempFilePath() {
    wchar_t tempDirectory[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempDirectory) == 0) {
        throw std::runtime_error("GetTempPathW");
    }
    wchar_t tempFile[MAX_PATH]{};
    if (GetTempFileNameW(tempDirectory, L"npl", 0, tempFile) == 0) {
        throw std::runtime_error("GetTempFileNameW");
    }
    return tempFile;
}

void WriteBytes(const std::wstring& path, const void* data, std::size_t byteCount) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("CreateFileW");
    }
    DWORD written = 0;
    if (byteCount > 0 && (!WriteFile(file, data, static_cast<DWORD>(byteCount), &written, nullptr) || written != byteCount)) {
        CloseHandle(file);
        throw std::runtime_error("WriteFile");
    }
    CloseHandle(file);
}

std::vector<unsigned char> ReadBytes(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("CreateFileW read");
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    if (!bytes.empty()) {
        ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    }
    CloseHandle(file);
    return bytes;
}

std::wstring EntireText(const NativePad::LargeTextDocument& doc) {
    return doc.TextRange(0, doc.Length());
}

void RunUtf8EditTest() {
    const std::string bytes = "alpha\r\nbeta\r\ngamma";
    const std::wstring path = TempFilePath();
    WriteBytes(path, bytes.data(), bytes.size());

    NativePad::LargeTextDocument doc;
    std::wstring error;
    if (!doc.Open(path, error)) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("open utf8 large doc");
    }

    try {
        ExpectSize(doc.Length(), bytes.size(), "utf8 initial length");
        ExpectSize(doc.LineCount(), 3, "utf8 initial line count");
        ExpectSize(doc.LineStart(1), 7, "utf8 initial line 1 start");
        ExpectSize(doc.LineStart(2), 13, "utf8 initial line 2 start");
        ExpectText(doc.TextRange(7, 4), L"beta", "utf8 initial range");
        ExpectTrue(!doc.Dirty(), "utf8 not dirty after open");

        // Insert at the start of line 2 ("beta" -> "XXbeta").
        doc.Replace(7, 0, L"XX");
        ExpectTrue(doc.Dirty(), "utf8 dirty after insert");
        ExpectSize(doc.Length(), bytes.size() + 2, "utf8 length after insert");
        ExpectText(EntireText(doc), L"alpha\r\nXXbeta\r\ngamma", "utf8 text after insert");
        ExpectSize(doc.LineCount(), 3, "utf8 line count after insert");
        ExpectSize(doc.LineStart(2), 15, "utf8 line 2 start after insert");
        ExpectSize(doc.LineFromPosition(9), 1, "utf8 line from position after insert");

        // Insert a newline so a piece boundary splits a line.
        doc.Replace(9, 0, L"\r\n");
        ExpectText(EntireText(doc), L"alpha\r\nXX\r\nbeta\r\ngamma", "utf8 text after newline insert");
        ExpectSize(doc.LineCount(), 4, "utf8 line count after newline insert");
        ExpectSize(doc.LineStart(2), 11, "utf8 line 2 start after newline insert");
        ExpectText(doc.TextRange(doc.LineStart(2), 4), L"beta", "utf8 line 2 text after newline insert");

        // Erase the inserted "XX\r\n" (positions 7..11).
        doc.Replace(7, 4, L"");
        ExpectText(EntireText(doc), L"alpha\r\nbeta\r\ngamma", "utf8 text after erase");
        ExpectSize(doc.LineCount(), 3, "utf8 line count after erase");

        // CharAt returns raw bytes in UTF-8 space.
        ExpectSize(static_cast<std::size_t>(doc.CharAt(0)), static_cast<std::size_t>(L'a'), "utf8 char at 0");

        auto match = doc.Find(L"gamma", 0, true, true);
        ExpectTrue(match.has_value() && match->position == 13, "utf8 find original");
        doc.Replace(doc.Length(), 0, L"\r\ndelta");
        match = doc.Find(L"delta", 0, true, true);
        ExpectTrue(match.has_value() && match->position == 20, "utf8 find in add buffer");
    } catch (...) {
        doc.Close();
        DeleteFileW(path.c_str());
        throw;
    }

    doc.Close();
    DeleteFileW(path.c_str());
}

void RunUtf8MultibyteSnapTest() {
    // "e" + U+00E9 (c3 a9) + "\n" + "z". Erasing one byte at the start of the
    // multibyte sequence must snap to remove the whole code point.
    const unsigned char raw[] = {'e', 0xC3, 0xA9, '\n', 'z'};
    const std::wstring path = TempFilePath();
    WriteBytes(path, raw, sizeof(raw));

    NativePad::LargeTextDocument doc;
    std::wstring error;
    if (!doc.Open(path, error)) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("open utf8 multibyte doc");
    }

    try {
        ExpectText(doc.TextRange(1, 2), std::wstring(1, static_cast<wchar_t>(0x00E9)), "multibyte decode");

        // Erase a single byte at position 2 (the trailing continuation byte);
        // it must snap left to position 1 and remove both bytes of the char.
        doc.Replace(2, 1, L"");
        ExpectText(EntireText(doc), L"e\nz", "multibyte snap erase");
        ExpectSize(doc.LineCount(), 2, "multibyte line count after erase");
    } catch (...) {
        doc.Close();
        DeleteFileW(path.c_str());
        throw;
    }

    doc.Close();
    DeleteFileW(path.c_str());
}

void RunUtf16EditTest() {
    const std::wstring content = L"one\r\ntwo";
    std::vector<unsigned char> bytes;
    bytes.push_back(0xFF);
    bytes.push_back(0xFE);
    const auto* raw = reinterpret_cast<const unsigned char*>(content.data());
    bytes.insert(bytes.end(), raw, raw + content.size() * sizeof(wchar_t));

    const std::wstring path = TempFilePath();
    WriteBytes(path, bytes.data(), bytes.size());

    NativePad::LargeTextDocument doc;
    std::wstring error;
    if (!doc.Open(path, error)) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("open utf16 large doc");
    }

    try {
        ExpectSize(doc.Length(), content.size(), "utf16 initial length");
        ExpectSize(doc.LineCount(), 2, "utf16 initial line count");
        ExpectSize(doc.LineStart(1), 5, "utf16 initial line 1 start");
        ExpectText(doc.TextRange(5, 3), L"two", "utf16 initial range");

        doc.Replace(5, 0, L"XY");
        ExpectText(EntireText(doc), L"one\r\nXYtwo", "utf16 text after insert");
        ExpectSize(doc.LineStart(1), 5, "utf16 line 1 start after insert");
        ExpectSize(doc.Length(), content.size() + 2, "utf16 length after insert");

        doc.Replace(5, 0, L"A\r\nB");
        ExpectText(EntireText(doc), L"one\r\nA\r\nBXYtwo", "utf16 text after newline insert");
        ExpectSize(doc.LineCount(), 3, "utf16 line count after newline insert");

        auto match = doc.Find(L"XY", 0, true, true);
        ExpectTrue(match.has_value(), "utf16 find add content");
    } catch (...) {
        doc.Close();
        DeleteFileW(path.c_str());
        throw;
    }

    doc.Close();
    DeleteFileW(path.c_str());
}

void RunSaveRoundTripTest() {
    const std::string bytes = "line one\nline two\nline three";
    const std::wstring path = TempFilePath();
    WriteBytes(path, bytes.data(), bytes.size());

    NativePad::LargeTextDocument doc;
    std::wstring error;
    if (!doc.Open(path, error)) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("open save round-trip doc");
    }

    const std::wstring outPath = TempFilePath();
    try {
        doc.Replace(0, 0, L"HEAD ");
        doc.Replace(doc.Length(), 0, L"\ntail");
        if (!doc.SaveTo(outPath, error)) {
            throw std::runtime_error("SaveTo failed");
        }

        const std::vector<unsigned char> written = ReadBytes(outPath);
        const std::string expected = "HEAD line one\nline two\nline three\ntail";
        const std::string actual(written.begin(), written.end());
        if (actual != expected) {
            std::cerr << "FAILED: save round-trip content\nExpected: [" << expected << "]\nActual:   [" << actual << "]\n";
            throw std::runtime_error("save round-trip content");
        }
    } catch (...) {
        doc.Close();
        DeleteFileW(path.c_str());
        DeleteFileW(outPath.c_str());
        throw;
    }

    doc.Close();
    DeleteFileW(path.c_str());
    DeleteFileW(outPath.c_str());
}

void RunUtf8BomSavePreservesBomTest() {
    const unsigned char raw[] = {0xEF, 0xBB, 0xBF, 'h', 'i'};
    const std::wstring path = TempFilePath();
    WriteBytes(path, raw, sizeof(raw));

    NativePad::LargeTextDocument doc;
    std::wstring error;
    if (!doc.Open(path, error)) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("open utf8 bom doc");
    }

    const std::wstring outPath = TempFilePath();
    try {
        // Content excludes the BOM; document sees only "hi".
        ExpectText(EntireText(doc), L"hi", "utf8 bom content excludes bom");
        doc.Replace(2, 0, L"!");
        if (!doc.SaveTo(outPath, error)) {
            throw std::runtime_error("SaveTo bom failed");
        }
        const std::vector<unsigned char> written = ReadBytes(outPath);
        const std::vector<unsigned char> expected = {0xEF, 0xBB, 0xBF, 'h', 'i', '!'};
        ExpectTrue(written == expected, "utf8 bom save preserves bom");
    } catch (...) {
        doc.Close();
        DeleteFileW(path.c_str());
        DeleteFileW(outPath.c_str());
        throw;
    }

    doc.Close();
    DeleteFileW(path.c_str());
    DeleteFileW(outPath.c_str());
}

} // namespace

void RunLargeTextDocumentTests() {
    RunUtf8EditTest();
    RunUtf8MultibyteSnapTest();
    RunUtf16EditTest();
    RunSaveRoundTripTest();
    RunUtf8BomSavePreservesBomTest();
    std::cout << "LargeTextDocument tests passed\n";
}
