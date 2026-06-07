#include "../src/LineIndex.h"

#include <iostream>
#include <stdexcept>

namespace {

void ExpectEqual(std::size_t actual, std::size_t expected, const char* name) {
    if (actual != expected) {
        std::cerr << "FAILED: " << name << "\nExpected: " << expected << "\nActual:   " << actual << "\n";
        throw std::runtime_error(name);
    }
}

} // namespace

void RunLineIndexTests() {
    // Exercise the incremental edit path because scroll state depends on these
    // line starts being updated without a full rebuild after every keystroke.
    NativePad::LineIndex index;
    index.Reset(L"one\r\ntwo\r\nthree");
    ExpectEqual(index.LineCount(), 3, "initial line count");
    ExpectEqual(index.LineStart(0), 0, "line 0 start");
    ExpectEqual(index.LineStart(1), 5, "line 1 start");
    ExpectEqual(index.LineStart(2), 10, "line 2 start");
    ExpectEqual(index.LineFromPosition(12), 2, "line from position");
    ExpectEqual(index.MaxLineLength(), 5, "max line length");

    index.ApplyEdit(5, L"", L"inserted\r\n");
    ExpectEqual(index.LineCount(), 4, "line count after insert newline");
    ExpectEqual(index.LineStart(1), 5, "inserted line start");
    ExpectEqual(index.LineStart(2), 15, "shifted original line start");
    if (index.MaxLineLength() < 8) {
        throw std::runtime_error("max line length after insert");
    }

    index.ApplyEdit(3, L"\r\ninserted\r\n", L" ");
    ExpectEqual(index.LineCount(), 2, "line count after deleting multiple newlines");
    ExpectEqual(index.LineStart(1), 9, "line start after delete");

    index.ApplyEdit(4, L"", L"\n");
    ExpectEqual(index.LineCount(), 3, "line count after lf insert");
    ExpectEqual(index.LineStart(1), 5, "lf insert start");

    std::cout << "LineIndex tests passed\n";
}
