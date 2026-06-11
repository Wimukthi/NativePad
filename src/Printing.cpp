#include "Printing.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <thread>

namespace NativePad {

namespace {

bool NextPrintLine(std::wstring_view text, size_t& offset, std::wstring_view& line) {
    if (offset > text.size()) {
        return false;
    }

    const size_t start = offset;
    while (offset < text.size() && text[offset] != L'\r' && text[offset] != L'\n') {
        ++offset;
    }

    line = text.substr(start, offset - start);
    if (offset < text.size()) {
        if (text[offset] == L'\r' && offset + 1 < text.size() && text[offset + 1] == L'\n') {
            offset += 2;
        } else {
            ++offset;
        }
    } else {
        offset = text.size() + 1;
    }

    return true;
}

bool StartNextPrintPage(HDC dc, bool& pageOpen, std::wstring& error) {
    if (pageOpen && EndPage(dc) <= 0) {
        error = L"Could not finish the current print page.";
        pageOpen = false;
        return false;
    }

    if (StartPage(dc) <= 0) {
        error = L"Could not start a print page.";
        pageOpen = false;
        return false;
    }

    pageOpen = true;
    return true;
}

bool PrintDocument(PrintJob& job, std::wstring& error) {
    // Pagination and printer DC calls can block. This function runs on the print
    // worker thread; the UI only receives the final PrintResult.
    HFONT font = CreateFontIndirectW(&job.font);
    if (font == nullptr) {
        error = L"Could not create the selected printer font.";
        return false;
    }

    HGDIOBJ oldFont = SelectObject(job.printerDc, font);
    SetBkMode(job.printerDc, TRANSPARENT);
    SetTextColor(job.printerDc, RGB(0, 0, 0));

    const int dpiX = std::max(1, GetDeviceCaps(job.printerDc, LOGPIXELSX));
    const int dpiY = std::max(1, GetDeviceCaps(job.printerDc, LOGPIXELSY));
    RECT pageRect{
        MulDiv(job.marginsThousandths.left, dpiX, 1000),
        MulDiv(job.marginsThousandths.top, dpiY, 1000),
        GetDeviceCaps(job.printerDc, HORZRES) - MulDiv(job.marginsThousandths.right, dpiX, 1000),
        GetDeviceCaps(job.printerDc, VERTRES) - MulDiv(job.marginsThousandths.bottom, dpiY, 1000),
    };
    if (pageRect.right <= pageRect.left || pageRect.bottom <= pageRect.top) {
        pageRect = {dpiX / 2, dpiY / 2, GetDeviceCaps(job.printerDc, HORZRES) - (dpiX / 2), GetDeviceCaps(job.printerDc, VERTRES) - (dpiY / 2)};
    }

    TEXTMETRICW metrics{};
    GetTextMetricsW(job.printerDc, &metrics);
    const int lineHeight = std::max<int>(1, static_cast<int>(metrics.tmHeight + metrics.tmExternalLeading));

    DOCINFOW docInfo{};
    docInfo.cbSize = sizeof(docInfo);
    docInfo.lpszDocName = job.documentName.empty() ? L"NativePad Document" : job.documentName.c_str();
    if (StartDocW(job.printerDc, &docInfo) <= 0) {
        error = L"Could not start the print job.";
        SelectObject(job.printerDc, oldFont);
        DeleteObject(font);
        return false;
    }

    bool pageOpen = false;
    bool ok = StartNextPrintPage(job.printerDc, pageOpen, error);
    int y = pageRect.top;

    size_t offset = 0;
    std::wstring_view line;
    while (ok && NextPrintLine(job.text, offset, line)) {
        RECT measureRect{pageRect.left, y, pageRect.right, pageRect.bottom};
        const UINT format = DT_NOPREFIX | DT_EXPANDTABS | (job.wordWrap ? DT_WORDBREAK : DT_SINGLELINE);
        int textHeight = lineHeight;
        if (!line.empty()) {
            RECT calcRect{pageRect.left, 0, pageRect.right, pageRect.bottom};
            DrawTextW(job.printerDc, line.data(), static_cast<int>(line.size()), &calcRect, format | DT_CALCRECT);
            textHeight = std::max<int>(lineHeight, calcRect.bottom - calcRect.top);
        }

        if (y + textHeight > pageRect.bottom) {
            ok = StartNextPrintPage(job.printerDc, pageOpen, error);
            y = pageRect.top;
            measureRect.top = y;
            measureRect.bottom = pageRect.bottom;
        }

        if (ok && !line.empty()) {
            DrawTextW(job.printerDc, line.data(), static_cast<int>(line.size()), &measureRect, format);
        }
        y += textHeight;
    }

    if (pageOpen && EndPage(job.printerDc) <= 0) {
        ok = false;
        error = L"Could not finish the final print page.";
    }

    if (ok) {
        EndDoc(job.printerDc);
    } else {
        AbortDoc(job.printerDc);
    }

    SelectObject(job.printerDc, oldFont);
    DeleteObject(font);
    return ok;
}

} // namespace

void StartPrintWorker(PrintJob job) {
    // Ownership of the printer DC transfers here. The worker posts back to the
    // window and releases the result pointer only if the message is queued.
    std::thread([job = std::move(job)]() mutable {
        std::unique_ptr<PrintResult> result = std::make_unique<PrintResult>();
        result->success = PrintDocument(job, result->message);
        DeleteDC(job.printerDc);
        if (!PostMessageW(job.owner, WM_NATIVEPAD_PRINT_COMPLETE, 0, reinterpret_cast<LPARAM>(result.get()))) {
            return;
        }
        result.release();
    }).detach();
}

} // namespace NativePad
