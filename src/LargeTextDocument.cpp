#include "LargeTextDocument.h"

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

std::wstring DecodeBytes(UINT codePage, const unsigned char* data, std::size_t byteCount) {
    if (byteCount == 0) {
        return {};
    }
    if (byteCount > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {};
    }

    const auto* chars = reinterpret_cast<const char*>(data);
    const int inputLength = static_cast<int>(byteCount);
    const int required = MultiByteToWideChar(codePage, 0, chars, inputLength, nullptr, 0);
    if (required <= 0) {
        return {};
    }

    std::wstring text(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(codePage, 0, chars, inputLength, text.data(), required);
    return text;
}

std::optional<std::vector<unsigned char>> EncodeBytes(UINT codePage, std::wstring_view text) {
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
    WideCharToMultiByte(codePage, 0, text.data(), inputLength, reinterpret_cast<char*>(bytes.data()), required, nullptr, nullptr);
    return bytes;
}

unsigned char LowerAscii(unsigned char value) noexcept {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<unsigned char>(value - 'A' + 'a');
    }
    return value;
}

bool IsUtf8Continuation(wchar_t unit) noexcept {
    return (static_cast<unsigned char>(unit) & 0xC0) == 0x80;
}

bool WriteAll(HANDLE file, const void* data, std::size_t byteCount) {
    const auto* cursor = static_cast<const unsigned char*>(data);
    std::size_t remaining = byteCount;
    while (remaining > 0) {
        const DWORD toWrite = static_cast<DWORD>(std::min<std::size_t>(remaining, 1024u * 1024u));
        DWORD written = 0;
        if (!WriteFile(file, cursor, toWrite, &written, nullptr) || written == 0) {
            return false;
        }
        cursor += written;
        remaining -= written;
    }
    return true;
}

} // namespace

LargeTextDocument::~LargeTextDocument() {
    Close();
}

LargeTextDocument::LargeTextDocument(LargeTextDocument&& other) noexcept {
    *this = std::move(other);
}

LargeTextDocument& LargeTextDocument::operator=(LargeTextDocument&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    Close();
    file_ = other.file_;
    mapping_ = other.mapping_;
    data_ = other.data_;
    fileByteCount_ = other.fileByteCount_;
    dataOffset_ = other.dataOffset_;
    originalUnitCount_ = other.originalUnitCount_;
    encoding_ = other.encoding_;
    encodingLabel_ = std::move(other.encodingLabel_);
    lineEnding_ = other.lineEnding_;
    pieces_ = std::move(other.pieces_);
    addBytes_ = std::move(other.addBytes_);
    addUnits_ = std::move(other.addUnits_);
    originalNewlines_ = std::move(other.originalNewlines_);
    addNewlines_ = std::move(other.addNewlines_);
    maxLineLength_ = other.maxLineLength_;
    dirty_ = other.dirty_;
    prefixLength_ = std::move(other.prefixLength_);
    prefixNewlines_ = std::move(other.prefixNewlines_);
    prefixValid_ = other.prefixValid_;

    other.file_ = INVALID_HANDLE_VALUE;
    other.mapping_ = nullptr;
    other.data_ = nullptr;
    other.fileByteCount_ = 0;
    other.dataOffset_ = 0;
    other.originalUnitCount_ = 0;
    other.encoding_ = FileEncoding::Utf8;
    other.encodingLabel_ = L"UTF-8/ANSI";
    other.lineEnding_ = LineEnding::CrLf;
    other.maxLineLength_ = 0;
    other.dirty_ = false;
    other.prefixValid_ = false;

    return *this;
}

bool LargeTextDocument::Open(const std::wstring& path, std::wstring& error) {
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
    BuildOriginalNewlineIndex();

    pieces_.clear();
    if (originalUnitCount_ > 0) {
        Piece whole;
        whole.source = Source::Original;
        whole.start = 0;
        whole.length = originalUnitCount_;
        whole.newlines = originalNewlines_.size();
        pieces_.push_back(whole);
    }
    prefixValid_ = false;
    dirty_ = false;
    return true;
}

void LargeTextDocument::Close() noexcept {
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
    originalUnitCount_ = 0;
    encoding_ = FileEncoding::Utf8;
    encodingLabel_ = L"UTF-8/ANSI";
    lineEnding_ = LineEnding::CrLf;
    pieces_.clear();
    addBytes_.clear();
    addUnits_.clear();
    originalNewlines_.clear();
    addNewlines_.clear();
    maxLineLength_ = 0;
    dirty_ = false;
    prefixLength_.clear();
    prefixNewlines_.clear();
    prefixValid_ = false;
}

bool LargeTextDocument::IsOpen() const noexcept {
    return data_ != nullptr;
}

std::uint64_t LargeTextDocument::FileByteCount() const noexcept {
    return fileByteCount_;
}

const std::wstring& LargeTextDocument::EncodingLabel() const noexcept {
    return encodingLabel_;
}

TextEncoding LargeTextDocument::Encoding() const noexcept {
    switch (encoding_) {
    case FileEncoding::Utf8:
        return TextEncoding::Utf8;
    case FileEncoding::Utf8Bom:
        return TextEncoding::Utf8Bom;
    case FileEncoding::Ansi:
        return TextEncoding::Ansi;
    case FileEncoding::Utf16Le:
        return TextEncoding::Utf16Le;
    case FileEncoding::Utf16Be:
        return TextEncoding::Utf16Be;
    }
    return TextEncoding::Utf8;
}

LineEnding LargeTextDocument::DetectedLineEnding() const noexcept {
    return lineEnding_;
}

bool LargeTextDocument::Dirty() const noexcept {
    return dirty_;
}

bool LargeTextDocument::IsUtf16() const noexcept {
    return encoding_ == FileEncoding::Utf16Le || encoding_ == FileEncoding::Utf16Be;
}

std::size_t LargeTextDocument::OriginalUnitCount() const noexcept {
    return originalUnitCount_;
}

wchar_t LargeTextDocument::OriginalUnitAt(std::size_t index) const {
    if (!IsUtf16()) {
        return static_cast<wchar_t>(data_[dataOffset_ + index]);
    }

    const std::size_t byteIndex = dataOffset_ + index * sizeof(wchar_t);
    const unsigned char first = data_[byteIndex];
    const unsigned char second = data_[byteIndex + 1];
    if (encoding_ == FileEncoding::Utf16Le) {
        return static_cast<wchar_t>(first | (second << 8));
    }
    return static_cast<wchar_t>((first << 8) | second);
}

wchar_t LargeTextDocument::AddUnitAt(std::size_t index) const {
    if (IsUtf16()) {
        return addUnits_[index];
    }
    return static_cast<wchar_t>(addBytes_[index]);
}

wchar_t LargeTextDocument::SourceUnitAt(Source source, std::size_t index) const {
    return source == Source::Original ? OriginalUnitAt(index) : AddUnitAt(index);
}

const std::vector<std::size_t>& LargeTextDocument::SourceNewlines(Source source) const noexcept {
    return source == Source::Original ? originalNewlines_ : addNewlines_;
}

std::size_t LargeTextDocument::CountNewlines(Source source, std::size_t start, std::size_t length) const {
    if (length == 0) {
        return 0;
    }
    const std::vector<std::size_t>& newlines = SourceNewlines(source);
    const auto begin = std::lower_bound(newlines.begin(), newlines.end(), start);
    const auto end = std::lower_bound(newlines.begin(), newlines.end(), start + length);
    return static_cast<std::size_t>(end - begin);
}

void LargeTextDocument::DetectEncoding() {
    dataOffset_ = 0;
    encoding_ = FileEncoding::Utf8;
    encodingLabel_ = L"UTF-8/ANSI";

    if (fileByteCount_ >= 2) {
        if (data_[0] == 0xFF && data_[1] == 0xFE) {
            encoding_ = FileEncoding::Utf16Le;
            encodingLabel_ = L"UTF-16 LE";
            dataOffset_ = 2;
        } else if (data_[0] == 0xFE && data_[1] == 0xFF) {
            encoding_ = FileEncoding::Utf16Be;
            encodingLabel_ = L"UTF-16 BE";
            dataOffset_ = 2;
        }
    }

    if (dataOffset_ == 0 && fileByteCount_ >= 3 && data_[0] == 0xEF && data_[1] == 0xBB && data_[2] == 0xBF) {
        encoding_ = FileEncoding::Utf8Bom;
        encodingLabel_ = L"UTF-8 BOM";
        dataOffset_ = 3;
    }

    const std::size_t contentBytes = static_cast<std::size_t>(fileByteCount_) - dataOffset_;
    originalUnitCount_ = IsUtf16() ? contentBytes / sizeof(wchar_t) : contentBytes;
}

void LargeTextDocument::BuildOriginalNewlineIndex() {
    originalNewlines_.clear();
    maxLineLength_ = 0;

    std::size_t lineStart = 0;
    for (std::size_t i = 0; i < originalUnitCount_; ++i) {
        const wchar_t unit = OriginalUnitAt(i);
        if (unit != L'\n') {
            continue;
        }

        std::size_t lineEnd = i;
        if (lineEnd > lineStart && OriginalUnitAt(lineEnd - 1) == L'\r') {
            --lineEnd;
        }
        maxLineLength_ = std::max(maxLineLength_, lineEnd - lineStart);
        originalNewlines_.push_back(i);
        lineStart = i + 1;
    }
    maxLineLength_ = std::max(maxLineLength_, originalUnitCount_ - lineStart);

    // Detect the dominant line ending from the first newline in the file.
    lineEnding_ = LineEnding::CrLf;
    if (!originalNewlines_.empty()) {
        const std::size_t first = originalNewlines_.front();
        if (first > 0 && OriginalUnitAt(first - 1) == L'\r') {
            lineEnding_ = LineEnding::CrLf;
        } else {
            lineEnding_ = LineEnding::Lf;
        }
    }
}

void LargeTextDocument::EnsurePrefixSums() const {
    if (prefixValid_) {
        return;
    }

    prefixLength_.assign(pieces_.size() + 1, 0);
    prefixNewlines_.assign(pieces_.size() + 1, 0);
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        prefixLength_[i + 1] = prefixLength_[i] + pieces_[i].length;
        prefixNewlines_[i + 1] = prefixNewlines_[i] + pieces_[i].newlines;
    }
    prefixValid_ = true;
}

std::size_t LargeTextDocument::Length() const noexcept {
    EnsurePrefixSums();
    return prefixLength_.back();
}

std::size_t LargeTextDocument::LineCount() const noexcept {
    EnsurePrefixSums();
    return prefixNewlines_.back() + 1;
}

std::size_t LargeTextDocument::PieceIndexForPosition(std::size_t position, std::size_t& pieceStart) const {
    EnsurePrefixSums();
    if (pieces_.empty()) {
        pieceStart = 0;
        return 0;
    }

    // First piece whose end is strictly past position owns it.
    const auto it = std::upper_bound(prefixLength_.begin(), prefixLength_.end(), position);
    std::size_t index = static_cast<std::size_t>(it - prefixLength_.begin());
    if (index > 0) {
        --index;
    }
    index = std::min(index, pieces_.size() - 1);
    pieceStart = prefixLength_[index];
    return index;
}

std::size_t LargeTextDocument::LineStart(std::size_t line) const {
    EnsurePrefixSums();
    if (line == 0 || pieces_.empty()) {
        return 0;
    }

    const std::size_t totalNewlines = prefixNewlines_.back();
    if (line > totalNewlines) {
        return prefixLength_.back();
    }

    // Find the piece containing the (line-1)-th newline (0-indexed).
    const std::size_t target = line - 1;
    const auto it = std::upper_bound(prefixNewlines_.begin(), prefixNewlines_.end(), target);
    std::size_t index = static_cast<std::size_t>(it - prefixNewlines_.begin());
    if (index > 0) {
        --index;
    }
    index = std::min(index, pieces_.size() - 1);

    const Piece& piece = pieces_[index];
    const std::size_t localNewline = target - prefixNewlines_[index];
    const std::vector<std::size_t>& newlines = SourceNewlines(piece.source);
    const auto begin = std::lower_bound(newlines.begin(), newlines.end(), piece.start);
    const std::size_t sourceOffset = *(begin + static_cast<std::ptrdiff_t>(localNewline));
    const std::size_t docOffset = prefixLength_[index] + (sourceOffset - piece.start);
    return docOffset + 1;
}

std::size_t LargeTextDocument::LineFromPosition(std::size_t position) const {
    EnsurePrefixSums();
    if (pieces_.empty()) {
        return 0;
    }
    position = std::min(position, prefixLength_.back());

    std::size_t pieceStart = 0;
    const std::size_t index = PieceIndexForPosition(position, pieceStart);
    const Piece& piece = pieces_[index];
    const std::size_t localOffset = position - pieceStart;
    return prefixNewlines_[index] + CountNewlines(piece.source, piece.start, localOffset);
}

std::size_t LargeTextDocument::MaxLineLength() const noexcept {
    return maxLineLength_;
}

wchar_t LargeTextDocument::CharAt(std::size_t position) const {
    EnsurePrefixSums();
    if (position >= prefixLength_.back()) {
        throw std::out_of_range("LargeTextDocument::CharAt");
    }

    std::size_t pieceStart = 0;
    const std::size_t index = PieceIndexForPosition(position, pieceStart);
    const Piece& piece = pieces_[index];
    return SourceUnitAt(piece.source, piece.start + (position - pieceStart));
}

std::wstring LargeTextDocument::DecodeUnits(Source source, std::size_t start, std::size_t length) const {
    if (length == 0) {
        return {};
    }

    if (IsUtf16()) {
        std::wstring text;
        text.reserve(length);
        for (std::size_t i = 0; i < length; ++i) {
            text.push_back(SourceUnitAt(source, start + i));
        }
        return text;
    }

    const unsigned char* bytes =
        source == Source::Original ? data_ + dataOffset_ + start : addBytes_.data() + start;
    if (encoding_ != FileEncoding::Ansi) {
        std::wstring text = DecodeBytes(CP_UTF8, bytes, length);
        if (!text.empty()) {
            return text;
        }
    }
    return DecodeBytes(CP_ACP, bytes, length);
}

std::wstring LargeTextDocument::TextRange(std::size_t position, std::size_t length) const {
    EnsurePrefixSums();
    const std::size_t total = prefixLength_.back();
    if (position >= total || length == 0) {
        return {};
    }
    length = std::min(length, total - position);

    std::wstring result;
    std::size_t pieceStart = 0;
    std::size_t index = PieceIndexForPosition(position, pieceStart);
    std::size_t remaining = length;
    std::size_t cursor = position;

    while (remaining > 0 && index < pieces_.size()) {
        const Piece& piece = pieces_[index];
        const std::size_t local = cursor - pieceStart;
        const std::size_t take = std::min(remaining, piece.length - local);
        result += DecodeUnits(piece.source, piece.start + local, take);
        cursor += take;
        remaining -= take;
        pieceStart += piece.length;
        ++index;
    }
    return result;
}

std::optional<LargeTextDocument::Match> LargeTextDocument::Find(
    std::wstring_view needle, std::size_t start, bool down, bool matchCase) const {
    const std::size_t total = Length();
    if (needle.empty() || total == 0) {
        return std::nullopt;
    }

    // Compare in the document's unit space: bytes for UTF-8/ANSI, wchars for
    // UTF-16. Encode the needle into the same space first.
    std::vector<wchar_t> needleUnits;
    if (IsUtf16()) {
        needleUnits.assign(needle.begin(), needle.end());
    } else {
        const UINT codePage = encoding_ == FileEncoding::Ansi ? CP_ACP : CP_UTF8;
        auto bytes = EncodeBytes(codePage, needle);
        if (!bytes || bytes->empty()) {
            return std::nullopt;
        }
        needleUnits.reserve(bytes->size());
        for (unsigned char byte : *bytes) {
            needleUnits.push_back(static_cast<wchar_t>(byte));
        }
    }

    if (needleUnits.size() > total) {
        return std::nullopt;
    }

    const auto fold = [&](wchar_t value) -> wchar_t {
        if (matchCase) {
            return value;
        }
        return IsUtf16() ? static_cast<wchar_t>(std::towlower(value))
                         : static_cast<wchar_t>(LowerAscii(static_cast<unsigned char>(value)));
    };

    const auto matchesAt = [&](std::size_t position) {
        for (std::size_t i = 0; i < needleUnits.size(); ++i) {
            if (fold(CharAt(position + i)) != fold(needleUnits[i])) {
                return false;
            }
        }
        return true;
    };

    const std::size_t last = total - needleUnits.size();
    if (down) {
        for (std::size_t position = std::min(start, last + 1); position <= last; ++position) {
            if (matchesAt(position)) {
                return Match{position, needleUnits.size()};
            }
        }
        return std::nullopt;
    }

    std::size_t position = std::min(start, last + 1);
    while (position > 0) {
        --position;
        if (matchesAt(position)) {
            return Match{position, needleUnits.size()};
        }
    }
    return std::nullopt;
}

std::size_t LargeTextDocument::SnapLeftToBoundary(std::size_t position) const {
    if (encoding_ != FileEncoding::Utf8 && encoding_ != FileEncoding::Utf8Bom) {
        return position;
    }
    while (position > 0 && position < Length() && IsUtf8Continuation(CharAt(position))) {
        --position;
    }
    return position;
}

void LargeTextDocument::AppendToAddBuffer(
    std::wstring_view text, std::size_t& addStart, std::size_t& addLength, std::size_t& addNewlines) {
    addNewlines = 0;
    if (IsUtf16()) {
        addStart = addUnits_.size();
        for (wchar_t unit : text) {
            if (unit == L'\n') {
                addNewlines_.push_back(addUnits_.size());
                ++addNewlines;
            }
            addUnits_.push_back(unit);
        }
        addLength = addUnits_.size() - addStart;
        return;
    }

    const UINT codePage = encoding_ == FileEncoding::Ansi ? CP_ACP : CP_UTF8;
    auto bytes = EncodeBytes(codePage, text);
    addStart = addBytes_.size();
    if (bytes) {
        for (unsigned char byte : *bytes) {
            if (byte == '\n') {
                addNewlines_.push_back(addBytes_.size());
                ++addNewlines;
            }
            addBytes_.push_back(byte);
        }
    }
    addLength = addBytes_.size() - addStart;
}

void LargeTextDocument::UpdateMaxLineLengthAround(std::size_t position) {
    const std::size_t line = LineFromPosition(std::min(position, Length()));
    const std::size_t start = LineStart(line);
    const std::size_t nextStart = line + 1 < LineCount() ? LineStart(line + 1) : Length();
    // nextStart includes the trailing break for non-final lines; approximate the
    // content length by trimming one break unit. This only feeds the horizontal
    // scrollbar extent, so a small over-estimate is harmless.
    const std::size_t contentLength = nextStart > start && line + 1 < LineCount() ? nextStart - start - 1 : nextStart - start;
    maxLineLength_ = std::max(maxLineLength_, contentLength);
}

LargeTextDocument::EditResult LargeTextDocument::Replace(
    std::size_t position, std::size_t eraseLength, std::wstring_view insertText) {
    EnsurePrefixSums();
    const std::size_t total = prefixLength_.back();
    position = std::min(position, total);
    std::size_t eraseEnd = std::min(position + eraseLength, total);

    // Keep UTF-8 multibyte sequences whole by snapping the edit to code-point
    // boundaries on both sides.
    position = SnapLeftToBoundary(position);
    if (encoding_ == FileEncoding::Utf8 || encoding_ == FileEncoding::Utf8Bom) {
        while (eraseEnd < total && IsUtf8Continuation(CharAt(eraseEnd))) {
            ++eraseEnd;
        }
    }
    if (eraseEnd < position) {
        eraseEnd = position;
    }

    // Capture the erased span (as UTF-16) before the pieces change so the caller
    // can record it for undo.
    EditResult result;
    result.position = position;
    result.erasedUnits = eraseEnd - position;
    result.erased = TextRange(position, result.erasedUnits);

    std::size_t addStart = 0;
    std::size_t addLength = 0;
    std::size_t addNewlines = 0;
    if (!insertText.empty()) {
        AppendToAddBuffer(insertText, addStart, addLength, addNewlines);
    }
    result.insertedUnits = addLength;

    const auto emitClipped = [&](std::vector<Piece>& out, const Piece& piece, std::size_t pieceDocStart,
                                 std::size_t rangeStart, std::size_t rangeEnd) {
        const std::size_t lo = std::max(pieceDocStart, rangeStart);
        const std::size_t hi = std::min(pieceDocStart + piece.length, rangeEnd);
        if (lo >= hi) {
            return;
        }
        Piece clipped;
        clipped.source = piece.source;
        clipped.start = piece.start + (lo - pieceDocStart);
        clipped.length = hi - lo;
        clipped.newlines = CountNewlines(piece.source, clipped.start, clipped.length);
        out.push_back(clipped);
    };

    std::vector<Piece> updated;
    updated.reserve(pieces_.size() + 2);

    std::size_t docStart = 0;
    for (const Piece& piece : pieces_) {
        emitClipped(updated, piece, docStart, 0, position);
        docStart += piece.length;
    }

    if (addLength > 0) {
        Piece added;
        added.source = Source::Add;
        added.start = addStart;
        added.length = addLength;
        added.newlines = addNewlines;
        updated.push_back(added);
    }

    docStart = 0;
    for (const Piece& piece : pieces_) {
        emitClipped(updated, piece, docStart, eraseEnd, total);
        docStart += piece.length;
    }

    pieces_ = std::move(updated);
    prefixValid_ = false;
    dirty_ = true;
    UpdateMaxLineLengthAround(position);
    return result;
}

bool LargeTextDocument::SaveTo(const std::wstring& path, std::wstring& error) const {
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = LastErrorText();
        return false;
    }

    bool ok = true;
    switch (encoding_) {
    case FileEncoding::Utf8Bom: {
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        ok = WriteAll(file, bom, sizeof(bom));
        break;
    }
    case FileEncoding::Utf16Le: {
        const unsigned char bom[] = {0xFF, 0xFE};
        ok = WriteAll(file, bom, sizeof(bom));
        break;
    }
    case FileEncoding::Utf16Be: {
        const unsigned char bom[] = {0xFE, 0xFF};
        ok = WriteAll(file, bom, sizeof(bom));
        break;
    }
    default:
        break;
    }

    for (const Piece& piece : pieces_) {
        if (!ok) {
            break;
        }
        if (piece.length == 0) {
            continue;
        }

        if (!IsUtf16()) {
            const unsigned char* bytes = piece.source == Source::Original
                                             ? data_ + dataOffset_ + piece.start
                                             : addBytes_.data() + piece.start;
            ok = WriteAll(file, bytes, piece.length);
            continue;
        }

        if (piece.source == Source::Original && encoding_ == FileEncoding::Utf16Le) {
            // Original LE bytes are already in file order.
            ok = WriteAll(file, data_ + dataOffset_ + piece.start * sizeof(wchar_t), piece.length * sizeof(wchar_t));
            continue;
        }

        // Re-emit units in the file's endianness for add-buffer content and for
        // big-endian originals.
        std::vector<unsigned char> bytes;
        bytes.reserve(piece.length * sizeof(wchar_t));
        for (std::size_t i = 0; i < piece.length; ++i) {
            const wchar_t unit = SourceUnitAt(piece.source, piece.start + i);
            if (encoding_ == FileEncoding::Utf16Le) {
                bytes.push_back(static_cast<unsigned char>(unit & 0xFF));
                bytes.push_back(static_cast<unsigned char>((unit >> 8) & 0xFF));
            } else {
                bytes.push_back(static_cast<unsigned char>((unit >> 8) & 0xFF));
                bytes.push_back(static_cast<unsigned char>(unit & 0xFF));
            }
        }
        ok = WriteAll(file, bytes.data(), bytes.size());
    }

    if (!ok) {
        error = LastErrorText();
    }

    if (!FlushFileBuffers(file) && ok) {
        ok = false;
        error = LastErrorText();
    }

    CloseHandle(file);
    if (!ok) {
        DeleteFileW(path.c_str());
    }
    return ok;
}

} // namespace NativePad
