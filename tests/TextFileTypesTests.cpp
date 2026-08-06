#include "../src/TextFileTypes.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Expect(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAILED: " << name << "\n";
        throw std::runtime_error(name);
    }
}

} // namespace

void RunTextFileTypesTests() {
    using NativePad::FileExtension;
    using NativePad::PlainTextFileFilter;
    using NativePad::PlainTextFilePattern;
    using NativePad::SyntaxLanguage;
    using NativePad::TextFileTypeForExtension;
    using NativePad::TextFileTypeForPath;

    const auto* json = TextFileTypeForPath(L"C:\\work\\settings.JSON");
    Expect(json != nullptr && json->extension == L".json" && json->syntax == SyntaxLanguage::Json, "JSON type lookup");
    const auto* ini = TextFileTypeForExtension(L".INI");
    Expect(ini != nullptr && ini->syntax == SyntaxLanguage::Ini, "case-insensitive extension lookup");
    const auto* nfo = TextFileTypeForPath(L"C:\\art\\README.NFO");
    Expect(nfo != nullptr && nfo->syntax == SyntaxLanguage::PlainText, "NFO type lookup");
    Expect(FileExtension(L"C:\\logs\\nativepad.log") == L".log", "path extension extraction");
    Expect(FileExtension(L"C:\\logs\\README").empty(), "extension missing");

    const std::wstring pattern = PlainTextFilePattern();
    Expect(
        pattern.find(L"*.txt") != std::wstring::npos && pattern.find(L"*.json") != std::wstring::npos &&
            pattern.find(L"*.nfo") != std::wstring::npos,
        "file pattern includes common types");
    const std::wstring filter = PlainTextFileFilter();
    Expect(filter.find(L"Text and data files") != std::wstring::npos && filter.find(L"*.md") != std::wstring::npos, "file dialog filter includes common types");
    Expect(NativePad::DefaultExtensionForPath(L"notes.md") == L"md", "recognized default extension");
    Expect(NativePad::DefaultExtensionForPath(L"art.nfo") == L"nfo", "NFO default extension");
    Expect(NativePad::DefaultExtensionForPath(L"new-file.unknown") == L"txt", "unknown default extension");

    std::cout << "TextFileTypes tests passed\n";
}
