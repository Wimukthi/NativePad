#pragma once

#include <windows.h>

#include <string>

// Printing runs on a worker thread so pagination and blocking printer-DC calls
// never freeze the UI. The application shell builds a PrintJob, hands ownership
// of the printer DC to StartPrintWorker, and later receives a heap-allocated
// PrintResult through WM_NATIVEPAD_PRINT_COMPLETE (the LPARAM owns the result).

namespace NativePad {

constexpr UINT WM_NATIVEPAD_PRINT_COMPLETE = WM_APP + 302;

struct PrintResult {
    bool success{false};
    std::wstring message;
};

struct PrintJob {
    HWND owner{};
    HDC printerDc{};
    std::wstring documentName;
    std::wstring text;
    LOGFONTW font{};
    RECT marginsThousandths{1000, 1000, 1000, 1000};
    bool wordWrap{false};
};

void StartPrintWorker(PrintJob job);

} // namespace NativePad
