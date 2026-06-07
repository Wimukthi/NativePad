#include "../src/TextFormat.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void ExpectLineEnding(NativePad::LineEnding actual, NativePad::LineEnding expected, const char* name) {
    if (actual != expected) {
        std::cerr << "FAILED: " << name << "\n";
        throw std::runtime_error(name);
    }
}

void ExpectWide(const std::wstring& actual, const std::wstring& expected, const char* name) {
    if (actual != expected) {
        std::wcerr << L"FAILED: " << name << L"\nExpected: " << expected << L"\nActual:   " << actual << L"\n";
        throw std::runtime_error(name);
    }
}

void ExpectBytes(const std::vector<char>& actual, const std::vector<unsigned char>& expected, const char* name) {
    if (actual.size() != expected.size()) {
        std::cerr << "FAILED: " << name << "\nExpected size: " << expected.size() << "\nActual size:   " << actual.size() << "\n";
        throw std::runtime_error(name);
    }

    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (static_cast<unsigned char>(actual[i]) != expected[i]) {
            std::cerr << "FAILED: " << name << " at byte " << i << "\n";
            throw std::runtime_error(name);
        }
    }
}

} // namespace

void RunTextFormatTests() {
    using NativePad::LineEnding;
    using NativePad::TextEncoding;

    ExpectLineEnding(NativePad::DetectLineEnding(L"one\r\ntwo\r\n"), LineEnding::CrLf, "detect CRLF");
    ExpectLineEnding(NativePad::DetectLineEnding(L"one\ntwo\n"), LineEnding::Lf, "detect LF");
    ExpectLineEnding(NativePad::DetectLineEnding(L"one\rtwo\r"), LineEnding::Cr, "detect CR");
    ExpectLineEnding(NativePad::DetectLineEnding(L"one\r\ntwo\n"), LineEnding::Mixed, "detect mixed line endings");
    ExpectLineEnding(NativePad::DetectLineEnding(L""), LineEnding::CrLf, "empty defaults to CRLF");

    ExpectWide(NativePad::NormalizeLineEndings(L"a\nb\rc\r\nd", LineEnding::CrLf), L"a\r\nb\r\nc\r\nd", "normalize CRLF");
    ExpectWide(NativePad::NormalizeLineEndings(L"a\r\nb\rc", LineEnding::Lf), L"a\nb\nc", "normalize LF");
    ExpectWide(NativePad::NormalizeLineEndings(L"a\r\nb\n", LineEnding::Mixed), L"a\r\nb\n", "mixed leaves text unchanged");

    std::wstring error;
    auto utf8Bom = NativePad::EncodeTextBytes(L"A\n", TextEncoding::Utf8Bom, LineEnding::CrLf, error);
    if (!utf8Bom) {
        throw std::runtime_error("UTF-8 BOM encode failed");
    }
    ExpectBytes(*utf8Bom, {0xEF, 0xBB, 0xBF, 'A', '\r', '\n'}, "UTF-8 BOM encode");

    auto utf16Be = NativePad::EncodeTextBytes(L"A\n", TextEncoding::Utf16Be, LineEnding::Lf, error);
    if (!utf16Be) {
        throw std::runtime_error("UTF-16 BE encode failed");
    }
    ExpectBytes(*utf16Be, {0xFE, 0xFF, 0x00, 'A', 0x00, '\n'}, "UTF-16 BE encode");

    auto ansi = NativePad::EncodeTextBytes(L"ABC", TextEncoding::Ansi, LineEnding::CrLf, error);
    if (!ansi) {
        throw std::runtime_error("ANSI encode failed");
    }
    ExpectBytes(*ansi, {'A', 'B', 'C'}, "ANSI encode");

    std::cout << "TextFormat tests passed\n";
}
