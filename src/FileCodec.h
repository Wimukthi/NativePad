#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "TextFormat.h"

// Text file loading and saving. Editable-sized files are decoded fully into
// UTF-16 (with BOM/encoding detection); files above the editable limit are read
// as a truncated read-only preview. Saving encodes into one buffer before the
// target file is touched. The open/save pickers live here too because they
// produce and consume the same encoding metadata.

namespace NativePad {

// Files larger than this are opened through the read-only memory-mapped backend
// instead of being decoded into the editable piece table. Shared so the loader
// and the application shell agree on where the editable path ends.
inline constexpr unsigned long long kReadChunkLimit = 512ull * 1024ull * 1024ull;

struct DecodedFile {
    std::wstring text;
    std::wstring encodingLabel;
    TextEncoding encoding{TextEncoding::Utf8};
    LineEnding lineEnding{LineEnding::CrLf};
    uint64_t fileByteCount{0};
    size_t decodedByteCount{0};
    bool readOnlyPreview{false};
    bool truncated{false};
};

struct SaveDialogResult {
    std::wstring path;
    TextEncoding encoding{TextEncoding::Utf8};
};

[[nodiscard]] std::optional<DecodedFile> ReadTextFile(const std::wstring& path, std::wstring& error);
[[nodiscard]] bool WriteTextFile(
    const std::wstring& path,
    std::wstring_view text,
    TextEncoding encoding,
    LineEnding lineEnding,
    std::wstring& error);
[[nodiscard]] std::optional<uint64_t> FileByteCountForPath(const std::wstring& path);

[[nodiscard]] std::optional<std::wstring> ShowOpenDialog(HWND owner);
[[nodiscard]] std::optional<SaveDialogResult> ShowSaveDialog(
    HWND owner,
    const std::wstring& currentPath,
    TextEncoding currentEncoding);

} // namespace NativePad
