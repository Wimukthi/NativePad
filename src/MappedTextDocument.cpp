#include "MappedTextDocument.h"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <utility>

namespace NativePad {

namespace {

std::wstring LastErrorText(DWORD error = GetLastError()) {
    if (error == ERROR_SUCCESS) {
        return L"No error.";
    }

    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstring message = length > 0 && buffer != nullptr ? std::wstring(buffer, length) : L"Unknown error.";
    if (buffer != nullptr) {
        LocalFree(buffer);
    }

    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
        message.pop_back();
    }

    return message;
}

std::optional<std::wstring> MultiByteToWideRange(UINT codePage, const char* data, std::size_t byteCount, DWORD flags) {
    // Windows conversion APIs take int lengths. Visible editor ranges are small,
    // but keep the guard here because mapped search/copy paths can be arbitrary.
    if (byteCount == 0) {
        return std::wstring();
    }

    if (byteCount > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    const int inputLength = static_cast<int>(byteCount);
    const int required = MultiByteToWideChar(codePage, flags, data, inputLength, nullptr, 0);
    if (required <= 0) {
        return std::nullopt;
    }

    std::wstring text(static_cast<std::size_t>(required), L'\0');
    const int written = MultiByteToWideChar(codePage, flags, data, inputLength, text.data(), required);
    if (written <= 0) {
        return std::nullopt;
    }

    return text;
}

std::optional<std::vector<unsigned char>> WideToMultiByteRange(UINT codePage, std::wstring_view text) {
    if (text.empty()) {
        return std::vector<unsigned char>();
    }

    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    const int inputLength = static_cast<int>(text.size());
    const int required = WideCharToMultiByte(codePage, 0, text.data(), inputLength, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return std::nullopt;
    }

    std::vector<unsigned char> bytes(static_cast<std::size_t>(required));
    const int written = WideCharToMultiByte(codePage, 0, text.data(), inputLength, reinterpret_cast<char*>(bytes.data()), required, nullptr, nullptr);
    if (written <= 0) {
        return std::nullopt;
    }

    return bytes;
}

unsigned char LowerAscii(unsigned char value) noexcept {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<unsigned char>(value - 'A' + 'a');
    }
    return value;
}

bool BytesMatchAt(const unsigned char* haystack, const unsigned char* needle, std::size_t needleLength, bool matchCase) {
    for (std::size_t i = 0; i < needleLength; ++i) {
        const unsigned char left = haystack[i];
        const unsigned char right = needle[i];
        if (matchCase ? left != right : LowerAscii(left) != LowerAscii(right)) {
            return false;
        }
    }
    return true;
}

bool WideMatchesAt(const MappedTextDocument& document, std::wstring_view needle, std::size_t position, bool matchCase) {
    for (std::size_t i = 0; i < needle.size(); ++i) {
        wchar_t left = document.CharAt(position + i);
        wchar_t right = needle[i];
        if (!matchCase) {
            left = static_cast<wchar_t>(std::towlower(left));
            right = static_cast<wchar_t>(std::towlower(right));
        }
        if (left != right) {
            return false;
        }
    }
    return true;
}

} // namespace

MappedTextDocument::~MappedTextDocument() {
    Close();
}

MappedTextDocument::MappedTextDocument(MappedTextDocument&& other) noexcept {
    *this = std::move(other);
}

MappedTextDocument& MappedTextDocument::operator=(MappedTextDocument&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    Close();
    file_ = other.file_;
    mapping_ = other.mapping_;
    data_ = other.data_;
    fileByteCount_ = other.fileByteCount_;
    dataOffset_ = other.dataOffset_;
    contentByteCount_ = other.contentByteCount_;
    length_ = other.length_;
    maxLineLength_ = other.maxLineLength_;
    encoding_ = other.encoding_;
    encodingLabel_ = std::move(other.encodingLabel_);
    lineStarts_ = std::move(other.lineStarts_);

    other.file_ = INVALID_HANDLE_VALUE;
    other.mapping_ = nullptr;
    other.data_ = nullptr;
    other.fileByteCount_ = 0;
    other.dataOffset_ = 0;
    other.contentByteCount_ = 0;
    other.length_ = 0;
    other.maxLineLength_ = 0;
    other.encoding_ = Encoding::Utf8;
    other.encodingLabel_ = L"UTF-8/ANSI";
    other.lineStarts_ = {0};

    return *this;
}

bool MappedTextDocument::Open(const std::wstring& path, std::wstring& error) {
    // Map the entire file into the process address space. This reserves address
    // range, not physical memory; the OS pages data in as the editor touches it.
    Close();

    file_ = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);

    if (file_ == INVALID_HANDLE_VALUE) {
        error = LastErrorText();
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file_, &size)) {
        error = LastErrorText();
        Close();
        return false;
    }

    if (size.QuadPart <= 0) {
        error = L"Cannot memory-map an empty file.";
        Close();
        return false;
    }

    if (static_cast<unsigned long long>(size.QuadPart) > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        error = L"This file is too large for this 64-bit address space.";
        Close();
        return false;
    }

    fileByteCount_ = static_cast<std::uint64_t>(size.QuadPart);
    mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping_ == nullptr) {
        error = LastErrorText();
        Close();
        return false;
    }

    data_ = static_cast<const unsigned char*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
    if (data_ == nullptr) {
        error = LastErrorText();
        Close();
        return false;
    }

    DetectEncoding();
    BuildLineIndex();
    return true;
}

void MappedTextDocument::Close() noexcept {
    if (data_ != nullptr) {
        UnmapViewOfFile(data_);
        data_ = nullptr;
    }

    if (mapping_ != nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }

    if (file_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }

    fileByteCount_ = 0;
    dataOffset_ = 0;
    contentByteCount_ = 0;
    length_ = 0;
    maxLineLength_ = 0;
    encoding_ = Encoding::Utf8;
    encodingLabel_ = L"UTF-8/ANSI";
    lineStarts_.clear();
    lineStarts_.push_back(0);
}

bool MappedTextDocument::IsOpen() const noexcept {
    return data_ != nullptr;
}

std::uint64_t MappedTextDocument::FileByteCount() const noexcept {
    return fileByteCount_;
}

const std::wstring& MappedTextDocument::EncodingLabel() const noexcept {
    return encodingLabel_;
}

std::size_t MappedTextDocument::Length() const noexcept {
    return length_;
}

std::size_t MappedTextDocument::LineCount() const noexcept {
    return lineStarts_.size();
}

std::size_t MappedTextDocument::LineStart(std::size_t line) const {
    if (lineStarts_.empty()) {
        return 0;
    }
    return lineStarts_[std::min(line, lineStarts_.size() - 1)];
}

std::size_t MappedTextDocument::LineFromPosition(std::size_t position) const {
    if (lineStarts_.empty()) {
        return 0;
    }

    const auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), std::min(position, length_));
    if (it == lineStarts_.begin()) {
        return 0;
    }

    return static_cast<std::size_t>((it - lineStarts_.begin()) - 1);
}

std::size_t MappedTextDocument::MaxLineLength() const noexcept {
    return maxLineLength_;
}

wchar_t MappedTextDocument::CharAt(std::size_t position) const {
    if (position >= length_) {
        throw std::out_of_range("MappedTextDocument::CharAt");
    }

    if (IsUtf16()) {
        return Utf16UnitAt(position);
    }

    return static_cast<wchar_t>(data_[dataOffset_ + position]);
}

std::wstring MappedTextDocument::TextRange(std::size_t position, std::size_t length) const {
    if (position >= length_ || length == 0) {
        return {};
    }

    const std::size_t clampedLength = std::min(length, length_ - position);
    return IsUtf16() ? Utf16TextRange(position, clampedLength) : ByteTextRange(position, clampedLength);
}

std::optional<MappedTextDocument::Match> MappedTextDocument::Find(std::wstring_view needle, std::size_t start, bool down, bool matchCase) const {
    if (needle.empty() || length_ == 0) {
        return std::nullopt;
    }

    return IsUtf16() ? FindUtf16(needle, start, down, matchCase) : FindBytes(needle, start, down, matchCase);
}

void MappedTextDocument::DetectEncoding() {
    // BOMs give us exact UTF-16 handling. Without a BOM we render as UTF-8 when
    // possible and fall back per-range to ANSI for legacy files.
    dataOffset_ = 0;
    encoding_ = Encoding::Utf8;
    encodingLabel_ = L"UTF-8/ANSI";

    if (fileByteCount_ >= 2) {
        if (data_[0] == 0xFF && data_[1] == 0xFE) {
            encoding_ = Encoding::Utf16Le;
            encodingLabel_ = L"UTF-16 LE";
            dataOffset_ = 2;
        } else if (data_[0] == 0xFE && data_[1] == 0xFF) {
            encoding_ = Encoding::Utf16Be;
            encodingLabel_ = L"UTF-16 BE";
            dataOffset_ = 2;
        }
    }

    if (dataOffset_ == 0 && fileByteCount_ >= 3 && data_[0] == 0xEF && data_[1] == 0xBB && data_[2] == 0xBF) {
        encoding_ = Encoding::Utf8Bom;
        encodingLabel_ = L"UTF-8 BOM";
        dataOffset_ = 3;
    }

    contentByteCount_ = static_cast<std::size_t>(fileByteCount_) - dataOffset_;
    length_ = IsUtf16() ? contentByteCount_ / sizeof(wchar_t) : contentByteCount_;
}

void MappedTextDocument::BuildLineIndex() {
    if (IsUtf16()) {
        BuildUtf16LineIndex();
    } else {
        BuildByteLineIndex();
    }
}

void MappedTextDocument::BuildByteLineIndex() {
    // For byte-backed text, editor positions are byte offsets from the first
    // content byte. That keeps indexing linear in file size and cheap in memory.
    lineStarts_.clear();
    lineStarts_.push_back(0);
    maxLineLength_ = 0;

    std::size_t lineStart = 0;
    const unsigned char* content = data_ + dataOffset_;
    for (std::size_t i = 0; i < contentByteCount_; ++i) {
        if (content[i] != '\n') {
            continue;
        }

        std::size_t lineEnd = i;
        if (lineEnd > lineStart && content[lineEnd - 1] == '\r') {
            --lineEnd;
        }
        maxLineLength_ = std::max(maxLineLength_, lineEnd - lineStart);
        lineStarts_.push_back(i + 1);
        lineStart = i + 1;
    }

    maxLineLength_ = std::max(maxLineLength_, length_ - lineStart);
}

void MappedTextDocument::BuildUtf16LineIndex() {
    // UTF-16 positions are wchar code-unit offsets after the BOM. This matches
    // the editable buffer's coordinate system and preserves exact columns.
    lineStarts_.clear();
    lineStarts_.push_back(0);
    maxLineLength_ = 0;

    std::size_t lineStart = 0;
    for (std::size_t i = 0; i < length_; ++i) {
        if (Utf16UnitAt(i) != L'\n') {
            continue;
        }

        std::size_t lineEnd = i;
        if (lineEnd > lineStart && Utf16UnitAt(lineEnd - 1) == L'\r') {
            --lineEnd;
        }
        maxLineLength_ = std::max(maxLineLength_, lineEnd - lineStart);
        lineStarts_.push_back(i + 1);
        lineStart = i + 1;
    }

    maxLineLength_ = std::max(maxLineLength_, length_ - lineStart);
}

wchar_t MappedTextDocument::Utf16UnitAt(std::size_t position) const {
    const std::size_t byteIndex = dataOffset_ + (position * sizeof(wchar_t));
    if (byteIndex + 1 >= static_cast<std::size_t>(fileByteCount_)) {
        return L'\0';
    }

    const unsigned char first = data_[byteIndex];
    const unsigned char second = data_[byteIndex + 1];
    if (encoding_ == Encoding::Utf16Le) {
        return static_cast<wchar_t>(first | (second << 8));
    }
    return static_cast<wchar_t>((first << 8) | second);
}

std::wstring MappedTextDocument::Utf16TextRange(std::size_t position, std::size_t length) const {
    std::wstring text;
    text.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        text.push_back(Utf16UnitAt(position + i));
    }
    return text;
}

std::wstring MappedTextDocument::ByteTextRange(std::size_t position, std::size_t length) const {
    // Decode only the requested span. Paint normally asks for a single visible
    // line segment, so this avoids full-file transcoding.
    const char* start = reinterpret_cast<const char*>(data_ + dataOffset_ + position);
    if (encoding_ != Encoding::Ansi) {
        if (auto text = MultiByteToWideRange(CP_UTF8, start, length, MB_ERR_INVALID_CHARS)) {
            return *text;
        }
    }

    if (auto text = MultiByteToWideRange(CP_ACP, start, length, 0)) {
        return *text;
    }

    return {};
}

std::optional<MappedTextDocument::Match> MappedTextDocument::FindBytes(std::wstring_view needle, std::size_t start, bool down, bool matchCase) const {
    // Byte-backed search is intentionally ASCII-case-insensitive for speed and
    // predictability on large logs. Non-ASCII bytes still match exactly.
    const UINT codePage = encoding_ == Encoding::Ansi ? CP_ACP : CP_UTF8;
    auto needleBytes = WideToMultiByteRange(codePage, needle);
    if (!needleBytes || needleBytes->empty() || needleBytes->size() > length_) {
        return std::nullopt;
    }

    const unsigned char* content = data_ + dataOffset_;
    const std::size_t needleLength = needleBytes->size();

    if (down) {
        const std::size_t last = length_ - needleLength;
        for (std::size_t position = std::min(start, length_); position <= last; ++position) {
            if (BytesMatchAt(content + position, needleBytes->data(), needleLength, matchCase)) {
                return Match{position, needleLength};
            }
        }
        return std::nullopt;
    }

    std::size_t position = std::min(start, length_ - needleLength + 1);
    while (position > 0) {
        --position;
        if (BytesMatchAt(content + position, needleBytes->data(), needleLength, matchCase)) {
            return Match{position, needleLength};
        }
    }

    return std::nullopt;
}

std::optional<MappedTextDocument::Match> MappedTextDocument::FindUtf16(std::wstring_view needle, std::size_t start, bool down, bool matchCase) const {
    if (needle.size() > length_) {
        return std::nullopt;
    }

    if (down) {
        const std::size_t last = length_ - needle.size();
        for (std::size_t position = std::min(start, length_); position <= last; ++position) {
            if (WideMatchesAt(*this, needle, position, matchCase)) {
                return Match{position, needle.size()};
            }
        }
        return std::nullopt;
    }

    std::size_t position = std::min(start, length_ - needle.size() + 1);
    while (position > 0) {
        --position;
        if (WideMatchesAt(*this, needle, position, matchCase)) {
            return Match{position, needle.size()};
        }
    }

    return std::nullopt;
}

bool MappedTextDocument::IsUtf16() const noexcept {
    return encoding_ == Encoding::Utf16Le || encoding_ == Encoding::Utf16Be;
}

} // namespace NativePad
