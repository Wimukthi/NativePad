#include "../src/SessionStore.h"

#include <windows.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void RunSessionStoreTests() {
    const std::wstring root =
        (std::filesystem::temp_directory_path() /
         (L"NativePadSessionTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())))
            .wstring();
    std::error_code filesystemError;
    std::filesystem::remove_all(root, filesystemError);

    try {
        NativePad::SessionState original;
        original.activeIndex = 1;

        NativePad::SessionTabState untitled;
        untitled.untitledNumber = 3;
        untitled.dirty = true;
        untitled.encoding = NativePad::TextEncoding::Utf8Bom;
        untitled.lineEnding = NativePad::LineEnding::Lf;
        untitled.text = L"unsaved\nworkspace \u03b2";
        original.tabs.push_back(untitled);

        NativePad::SessionTabState mapped;
        mapped.kind = NativePad::SessionDocumentKind::Mapped;
        mapped.currentPath = L"C:\\logs\\server.log";
        mapped.readOnlyPreview = true;
        mapped.followTail = true;
        original.tabs.push_back(mapped);

        NativePad::SessionTabState cleanFile;
        cleanFile.currentPath = L"C:\\notes\\saved.txt";
        cleanFile.text = L"clean file content should reopen from disk";
        original.tabs.push_back(cleanFile);

        NativePad::SessionTabState large;
        large.kind = NativePad::SessionDocumentKind::LargeEditable;
        large.currentPath = L"C:\\logs\\editable.log";
        large.dirty = true;
        original.tabs.push_back(large);

        NativePad::SessionStore store(root);
        std::wstring error;
        std::size_t largeWriterCalls = 0;
        const bool saved = store.Save(
            original,
            [&](std::size_t tabIndex, const std::wstring& targetPath, std::wstring& writerError) {
                ++largeWriterCalls;
                Expect(tabIndex == 3, "large writer receives its tab index");
                HANDLE file = CreateFileW(
                    targetPath.c_str(),
                    GENERIC_WRITE,
                    0,
                    nullptr,
                    CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr);
                if (file == INVALID_HANDLE_VALUE) {
                    writerError = L"Could not create the large test snapshot.";
                    return false;
                }
                constexpr char bytes[] = "large edited content";
                DWORD written = 0;
                const bool result = WriteFile(file, bytes, sizeof(bytes) - 1, &written, nullptr) &&
                                    written == sizeof(bytes) - 1;
                CloseHandle(file);
                if (!result) {
                    writerError = L"Could not write the large test snapshot.";
                }
                return result;
            },
            error);
        Expect(saved, "session saves");
        Expect(error.empty(), "session save has no error");
        Expect(largeWriterCalls == 1, "only a dirty large tab uses the large writer");

        NativePad::SessionState loaded;
        const auto status = store.LoadAndConsume(loaded, error);
        Expect(status == NativePad::SessionLoadStatus::Loaded, "session loads");
        Expect(loaded.activeIndex == 1, "active tab index round trips");
        Expect(loaded.tabs.size() == 4, "tab count round trips");
        Expect(loaded.tabs[0].hasTextSnapshot, "untitled content is snapshotted");
        Expect(loaded.tabs[0].text == untitled.text, "unsaved text round trips exactly");
        Expect(loaded.tabs[0].dirty, "dirty state round trips");
        Expect(loaded.tabs[0].untitledNumber == 3, "untitled number round trips");
        Expect(loaded.tabs[0].encoding == NativePad::TextEncoding::Utf8Bom, "encoding round trips");
        Expect(loaded.tabs[0].lineEnding == NativePad::LineEnding::Lf, "line ending round trips");
        Expect(loaded.tabs[1].kind == NativePad::SessionDocumentKind::Mapped, "backend kind round trips");
        Expect(loaded.tabs[1].currentPath == mapped.currentPath, "path round trips");
        Expect(loaded.tabs[1].followTail, "follow-tail state round trips");
        Expect(!loaded.tabs[2].hasTextSnapshot, "clean file-backed tabs do not duplicate content");
        Expect(loaded.tabs[2].text.empty(), "clean file-backed tabs reopen from disk");
        Expect(loaded.tabs[3].kind == NativePad::SessionDocumentKind::LargeEditable, "large backend kind round trips");
        Expect(loaded.tabs[3].dirty, "large dirty state round trips");
        Expect(!loaded.tabs[3].largeSnapshotPath.empty(), "large snapshot path is restored");
        Expect(
            GetFileAttributesW(loaded.tabs[3].largeSnapshotPath.c_str()) != INVALID_FILE_ATTRIBUTES,
            "large snapshot remains available after the manifest is consumed");

        NativePad::SessionState consumed;
        Expect(
            store.LoadAndConsume(consumed, error) == NativePad::SessionLoadStatus::NotFound,
            "loaded session is consumed");
        const std::wstring largeSnapshotPath = loaded.tabs[3].largeSnapshotPath;
        store.Clear();
        Expect(
            GetFileAttributesW(largeSnapshotPath.c_str()) == INVALID_FILE_ATTRIBUTES,
            "clearing the session removes retained large snapshots");
    } catch (...) {
        std::filesystem::remove_all(root, filesystemError);
        throw;
    }

    std::filesystem::remove_all(root, filesystemError);
    std::cout << "SessionStore tests passed\n";
}
