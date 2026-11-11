#include "../src/AppDataPaths.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

namespace fs = std::filesystem;

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        throw std::runtime_error(message);
    }
}

void WriteText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

std::string ReadText(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

} // namespace

void RunAppDataPathsTests() {
    const fs::path root = fs::temp_directory_path() /
                          (L"NativePadAppDataTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(GetTickCount64()));
    std::error_code ec;
    fs::remove_all(root, ec);

    try {
        const fs::path exeDir = root / L"usb" / L"NativePad";
        const fs::path local = root / L"LocalAppData" / L"NativePad";
        fs::create_directories(exeDir);

        // No marker: installed behaviour, nothing created beside the exe.
        auto info = NativePad::ResolveDataRoot(exeDir.wstring(), local.wstring());
        Expect(info.location == NativePad::DataLocation::LocalAppData, "no marker uses local app data");
        Expect(info.root == local.wstring(), "no marker root");
        Expect(!info.portableFallback, "no marker is not a fallback");
        Expect(!fs::exists(exeDir / NativePad::kPortableDataFolderName), "no marker creates no Data folder");

        // Marker present: Data beside the exe, created on first run only.
        WriteText(exeDir / NativePad::kPortableMarkerFileName, "");
        info = NativePad::ResolveDataRoot(exeDir.wstring(), local.wstring());
        Expect(info.location == NativePad::DataLocation::Portable, "marker selects portable mode");
        Expect(info.root == (exeDir / L"Data").wstring(), "portable root is Data beside the exe");
        Expect(info.portableFolderCreated, "first portable run creates Data");
        Expect(fs::is_directory(exeDir / L"Data"), "Data folder exists");
        Expect(fs::is_empty(exeDir / L"Data"), "write probe leaves no file behind");

        info = NativePad::ResolveDataRoot(exeDir.wstring(), local.wstring());
        Expect(info.location == NativePad::DataLocation::Portable && !info.portableFolderCreated, "existing Data is reused");

        // A Data path that cannot be a folder falls back to local app data.
        const fs::path blockedExe = root / L"blocked";
        WriteText(blockedExe / NativePad::kPortableMarkerFileName, "");
        WriteText(blockedExe / L"Data", "not a folder");
        info = NativePad::ResolveDataRoot(blockedExe.wstring(), local.wstring());
        Expect(info.location == NativePad::DataLocation::LocalAppData, "unusable Data falls back");
        Expect(info.portableFallback, "fallback is reported");
        Expect(info.root == local.wstring(), "fallback root is local app data");

        // Copying installed data into a new portable folder.
        Expect(!NativePad::LocalDataWorthCopying(local.wstring()), "empty local data is not worth copying");
        WriteText(local / L"NativePad.ini", "[Settings]\nWordWrap=1\n");
        WriteText(local / L"Session" / L"session.bin", "manifest");
        WriteText(local / L"Session" / L"tab-g1-0.txt", "unsaved note");
        WriteText(local / L"Session" / L"session-g2.tmp", "staging leftover");
        Expect(NativePad::LocalDataWorthCopying(local.wstring()), "settings and session are worth copying");

        const fs::path portable = exeDir / L"Data";
        WriteText(portable / L"Session" / L"tab-g1-0.txt", "already portable");
        Expect(NativePad::CopyLocalDataToPortable(local.wstring(), portable.wstring()), "copy succeeds");
        Expect(ReadText(portable / L"NativePad.ini") == "[Settings]\nWordWrap=1\n", "settings copied");
        Expect(ReadText(portable / L"Session" / L"session.bin") == "manifest", "manifest copied");
        Expect(ReadText(portable / L"Session" / L"tab-g1-0.txt") == "already portable", "existing portable file kept");
        Expect(!fs::exists(portable / L"Session" / L"session-g2.tmp"), "staging files are not copied");
        Expect(fs::exists(local / L"Session" / L"session.bin"), "originals are left in place");
    } catch (...) {
        fs::remove_all(root, ec);
        throw;
    }

    fs::remove_all(root, ec);
    std::cout << "AppDataPaths tests passed\n";
}
