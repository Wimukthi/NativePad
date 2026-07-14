#include "../src/RecoveryJournal.h"

#include <windows.h>

#include <filesystem>
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

std::wstring TestRootDirectory() {
    wchar_t tempDirectory[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempDirectory) == 0) {
        throw std::runtime_error("GetTempPathW");
    }

    std::filesystem::path root(tempDirectory);
    root /= L"NativePadRecoveryTests-" + std::to_wstring(GetCurrentProcessId());
    return root.wstring();
}

// A process id that is guaranteed dead: spawn a trivial process and wait for
// it to exit, then reuse its id before Windows recycles it.
DWORD DeadProcessId() {
    wchar_t comspec[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH) == 0) {
        throw std::runtime_error("GetEnvironmentVariableW ComSpec");
    }

    std::wstring commandLine = L"\"" + std::wstring(comspec) + L"\" /c exit";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        throw std::runtime_error("CreateProcessW");
    }

    WaitForSingleObject(process.hProcess, 10000);
    const DWORD processId = process.dwProcessId;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return processId;
}

void RunAbandonedJournalRoundTrip(const std::wstring& root) {
    NativePad::RecoverySnapshot snapshot;
    snapshot.originalPath = L"C:\\logs\\notes.txt";
    snapshot.encoding = NativePad::TextEncoding::Utf16Le;
    snapshot.lineEnding = NativePad::LineEnding::Lf;
    snapshot.text = L"first line\nsecond line with unicode \x00E9\x4F60\n";

    NativePad::RecoveryJournal journal(root, DeadProcessId());
    Expect(journal.Save(snapshot), "abandoned journal save");

    auto claimed = NativePad::RecoveryJournal::ClaimAbandoned(root);
    Expect(claimed.has_value(), "abandoned journal claimed");
    Expect(claimed->originalPath == snapshot.originalPath, "claimed path matches");
    Expect(claimed->encoding == snapshot.encoding, "claimed encoding matches");
    Expect(claimed->lineEnding == snapshot.lineEnding, "claimed line ending matches");
    Expect(claimed->text == snapshot.text, "claimed text matches");

    // A claim removes the journal, so a second scan finds nothing.
    Expect(!NativePad::RecoveryJournal::ClaimAbandoned(root).has_value(), "claim removes journal");
}

void RunLiveJournalIsNotClaimed(const std::wstring& root) {
    NativePad::RecoverySnapshot snapshot;
    snapshot.text = L"untitled work in progress";

    NativePad::RecoveryJournal journal(root, GetCurrentProcessId());
    Expect(journal.Save(snapshot), "live journal save");
    Expect(!NativePad::RecoveryJournal::ClaimAbandoned(root).has_value(), "live journal is not claimed");

    journal.Clear();
    Expect(!NativePad::RecoveryJournal::ClaimAbandoned(root).has_value(), "cleared journal leaves nothing");
    Expect(std::filesystem::is_empty(root), "clear removes journal files");
}

void RunUntitledJournalRoundTrip(const std::wstring& root) {
    NativePad::RecoverySnapshot snapshot;
    snapshot.text = L"never saved anywhere\r\nline two";

    NativePad::RecoveryJournal journal(root, DeadProcessId());
    Expect(journal.Save(snapshot), "untitled journal save");

    auto claimed = NativePad::RecoveryJournal::ClaimAbandoned(root);
    Expect(claimed.has_value(), "untitled journal claimed");
    Expect(claimed->originalPath.empty(), "untitled journal has no path");
    Expect(claimed->text == snapshot.text, "untitled text matches");
}

} // namespace

void RunRecoveryJournalTests() {
    const std::wstring root = TestRootDirectory();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    try {
        RunAbandonedJournalRoundTrip(root);
        RunLiveJournalIsNotClaimed(root);
        RunUntitledJournalRoundTrip(root);
    } catch (...) {
        std::filesystem::remove_all(root, ec);
        throw;
    }

    std::filesystem::remove_all(root, ec);
    std::cout << "RecoveryJournal tests passed\n";
}
