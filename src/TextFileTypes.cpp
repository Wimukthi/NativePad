#include "TextFileTypes.h"

#include <algorithm>
#include <array>
#include <cwctype>

namespace NativePad {

namespace {

constexpr std::array<TextFileType, 13> kTextFileTypes{{
    {L".txt", L"Text document", SyntaxLanguage::PlainText},
    {L".log", L"Log file", SyntaxLanguage::PlainText},
    {L".ini", L"INI configuration", SyntaxLanguage::Ini},
    {L".cfg", L"Configuration file", SyntaxLanguage::Ini},
    {L".conf", L"Configuration file", SyntaxLanguage::Ini},
    {L".csv", L"CSV data", SyntaxLanguage::PlainText},
    {L".tsv", L"TSV data", SyntaxLanguage::PlainText},
    {L".md", L"Markdown document", SyntaxLanguage::Markdown},
    {L".json", L"JSON data", SyntaxLanguage::Json},
    {L".xml", L"XML document", SyntaxLanguage::Xml},
    {L".nfo", L"NFO text", SyntaxLanguage::PlainText},
    {L".yaml", L"YAML document", SyntaxLanguage::PlainText},
    {L".yml", L"YAML document", SyntaxLanguage::PlainText},
}};

bool EqualExtension(std::wstring_view left, std::wstring_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }

    return std::equal(left.begin(), left.end(), right.begin(), [](wchar_t a, wchar_t b) {
        return std::towlower(a) == std::towlower(b);
    });
}

} // namespace

std::span<const TextFileType> SupportedTextFileTypes() noexcept {
    return kTextFileTypes;
}

std::wstring_view FileExtension(std::wstring_view path) noexcept {
    const std::wstring_view::size_type separator = path.find_last_of(L"\\/");
    const std::wstring_view::size_type dot = path.find_last_of(L'.');
    if (dot == std::wstring_view::npos || (separator != std::wstring_view::npos && dot <= separator) || dot + 1 >= path.size()) {
        return {};
    }

    return path.substr(dot);
}

const TextFileType* TextFileTypeForExtension(std::wstring_view extension) noexcept {
    if (extension.empty()) {
        return nullptr;
    }

    if (extension.front() != L'.') {
        return nullptr;
    }

    for (const TextFileType& type : kTextFileTypes) {
        if (EqualExtension(extension, type.extension)) {
            return &type;
        }
    }

    return nullptr;
}

const TextFileType* TextFileTypeForPath(std::wstring_view path) noexcept {
    return TextFileTypeForExtension(FileExtension(path));
}

std::wstring PlainTextFilePattern() {
    std::wstring pattern;
    for (size_t i = 0; i < kTextFileTypes.size(); ++i) {
        if (i != 0) {
            pattern += L';';
        }
        pattern += L'*';
        pattern += kTextFileTypes[i].extension;
    }
    return pattern;
}

std::wstring PlainTextFileFilter() {
    const std::wstring pattern = PlainTextFilePattern();
    std::wstring filter = L"Text and data files (";
    filter += pattern;
    filter += L")";
    filter.push_back(L'\0');
    filter += pattern;
    filter.push_back(L'\0');
    filter += L"All Files (*.*)";
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    return filter;
}

std::wstring DefaultExtensionForPath(std::wstring_view path) {
    const std::wstring_view extension = FileExtension(path);
    if (const TextFileType* type = TextFileTypeForExtension(extension); type != nullptr) {
        return std::wstring(type->extension.substr(1));
    }

    return L"txt";
}

} // namespace NativePad
