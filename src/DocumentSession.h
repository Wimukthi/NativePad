#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "DocumentBuffer.h"
#include "EditorView.h"
#include "LargeTextDocument.h"
#include "MappedTextDocument.h"
#include "RecoveryJournal.h"
#include "SyntaxHighlighter.h"
#include "TextFormat.h"

namespace NativePad {

using TabId = std::uint64_t;

struct FileStamp {
    ULONGLONG size{0};
    FILETIME writeTime{};
};

// Everything whose lifetime belongs to one tab. The window owns one shared
// EditorView and moves editorState into it only while this session is active.
struct DocumentSession {
    explicit DocumentSession(TabId value)
        : id(value), recovery(value) {}

    DocumentSession(const DocumentSession&) = delete;
    DocumentSession& operator=(const DocumentSession&) = delete;

    [[nodiscard]] bool IsMappedLargeFile() const noexcept {
        return mappedDocument != nullptr;
    }

    [[nodiscard]] bool IsLargeEditable() const noexcept {
        return largeDocument != nullptr;
    }

    [[nodiscard]] std::size_t Length() const noexcept {
        if (mappedDocument != nullptr) {
            return mappedDocument->Length();
        }
        if (largeDocument != nullptr) {
            return largeDocument->Length();
        }
        return document.Length();
    }

    [[nodiscard]] std::wstring Text() const {
        return mappedDocument != nullptr || largeDocument != nullptr ? std::wstring{} : document.Text();
    }

    TabId id{};
    std::uint64_t untitledNumber{1};
    DocumentBuffer document;
    std::unique_ptr<MappedTextDocument> mappedDocument;
    std::unique_ptr<LargeTextDocument> largeDocument;
    EditorViewState editorState;
    std::wstring currentPath;
    // A restored dirty large-file tab maps its normal-exit session snapshot
    // until the tab is saved, explicitly closed, or snapshotted again.
    std::wstring sessionBackingPath;
    std::wstring encodingLabel{L"UTF-8"};
    TextEncoding documentEncoding{TextEncoding::Utf8};
    LineEnding documentLineEnding{LineEnding::CrLf};
    SyntaxLanguage syntaxLanguage{SyntaxLanguage::PlainText};
    std::optional<FileStamp> fileStamp;
    std::uint64_t fileByteCount{0};
    std::size_t previewDecodedByteCount{0};
    RecoveryJournal recovery;
    bool dirty{false};
    bool readOnlyPreview{false};
    bool followTail{false};
    bool checkingExternalChange{false};
    bool recoverySavePending{false};
};

} // namespace NativePad
