#include "../src/DocumentBuffer.h"

#include <cstddef>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

void ExpectEqual(const std::wstring& actual, const std::wstring& expected, const char* name) {
    if (actual != expected) {
        std::wcerr << L"FAILED: " << name << L"\nExpected: " << expected << L"\nActual:   " << actual << L"\n";
        throw std::runtime_error(name);
    }
}

void ExpectEqualSize(std::size_t actual, std::size_t expected, const char* name) {
    if (actual != expected) {
        std::cerr << "FAILED: " << name << "\nExpected: " << expected << "\nActual:   " << actual << "\n";
        throw std::runtime_error(name);
    }
}

} // namespace

int main() {
    // Keep the test runner dependency-free so it can be built by MSBuild on a
    // clean Windows machine without adding a unit test framework.
    void RunLineIndexTests();
    void RunRecoveryJournalTests();
    void RunMappedTextDocumentTests();
    void RunTextFormatTests();

    using NativePad::DocumentBuffer;

    DocumentBuffer buffer(L"hello world");
    buffer.Insert(5, L", native");
    ExpectEqual(buffer.Text(), L"hello, native world", "insert in middle");

    buffer.Erase(5, 8);
    ExpectEqual(buffer.Text(), L"hello world", "erase inserted text");

    buffer.Replace(6, 5, L"NativePad");
    ExpectEqual(buffer.Text(), L"hello NativePad", "replace tail");
    ExpectEqual(buffer.TextRange(6, 9), L"NativePad", "text range");
    ExpectEqual(std::wstring(1, buffer.CharAt(1)), L"e", "char at");

    buffer.Insert(0, L"> ");
    buffer.Insert(buffer.Length(), L"\nsecond line\nthird line");
    ExpectEqual(buffer.Text(), L"> hello NativePad\nsecond line\nthird line", "insert at both ends");
    ExpectEqualSize(buffer.LineCount(), 3, "line count");

    buffer.Erase(0, buffer.Length());
    ExpectEqual(buffer.Text(), L"", "erase all");
    ExpectEqualSize(buffer.LineCount(), 1, "empty document line count");
    ExpectEqualSize(buffer.PieceCount(), 0, "empty document pieces");

    bool threw = false;
    try {
        buffer.Insert(1, L"x");
    } catch (const std::out_of_range&) {
        threw = true;
    }
    if (!threw) {
        throw std::runtime_error("insert outside document");
    }

    auto expectFind = [](std::optional<std::size_t> actual, std::size_t expected, const char* name) {
        if (actual.value_or(static_cast<std::size_t>(-1)) != expected) {
            throw std::runtime_error(name);
        }
    };
    auto expectNoFind = [](std::optional<std::size_t> actual, const char* name) {
        if (actual) {
            throw std::runtime_error(name);
        }
    };

    DocumentBuffer search(L"abcabc");
    expectFind(search.Find(L"bc", 0, true, true), 1, "find forward first");
    expectFind(search.Find(L"bc", 2, true, true), 4, "find forward second");
    expectFind(search.Find(L"bc", 6, false, true), 4, "find backward from end");
    expectFind(search.Find(L"bc", 4, false, true), 1, "find backward exclusive of start");
    expectFind(search.Find(L"BC", 0, true, false), 1, "find case-insensitive");
    expectNoFind(search.Find(L"BC", 0, true, true), "find case-sensitive should miss");
    expectNoFind(search.Find(L"xyz", 0, true, true), "find missing needle");
    expectNoFind(search.Find(L"", 0, true, true), "find empty needle");

    // Search must span pieces created by an insert, not just the original buffer.
    DocumentBuffer spanning(L"helloworld");
    spanning.Insert(5, L"-");
    ExpectEqual(spanning.Text(), L"hello-world", "spanning insert");
    expectFind(spanning.Find(L"o-w", 0, true, true), 4, "find across piece boundary");
    expectFind(spanning.Find(L"world", 0, true, true), 6, "find after piece boundary");
    expectFind(spanning.Find(L"o", 5, true, true), 7, "find forward resumes after cache");

    std::cout << "DocumentBuffer tests passed\n";
    RunLineIndexTests();
    RunMappedTextDocumentTests();
    RunRecoveryJournalTests();
    RunTextFormatTests();
    return 0;
}
