# NativePad Documentation

This folder documents the current NativePad implementation and the development
rules for extending it.

## Reading Order

1. [Architecture](ARCHITECTURE.md)
2. [Build and Test](BUILD_AND_TEST.md)
3. [Feature Matrix](FEATURE_MATRIX.md)
4. [Large Files](LARGE_FILES.md)
5. [UI and Theming](UI_AND_THEMING.md)
6. [Versioning](VERSIONING.md)
7. [Release Checklist](RELEASE_CHECKLIST.md)
8. [Contributing](CONTRIBUTING.md)
9. [Classic Notepad Parity](CLASSIC_NOTEPAD_PARITY.md)

## Project Goals

NativePad aims to be:

- Native: compiled C++/Win32, no managed runtime dependency.
- Fast: immediate startup and responsive editing.
- Familiar: classic Notepad command shape and shortcuts.
- Modern enough: dark mode, high-DPI correctness, and robust large-file viewing.
- Conservative: small dependencies, direct Win32 APIs, and clear ownership.

## Current Implementation Shape

NativePad has two document paths:

- Editable documents use `DocumentBuffer`, a piece-table buffer backed by UTF-16
  text.
- Oversized documents use `MappedTextDocument`, a read-only memory-mapped backend
  that indexes line starts without loading the whole file into a `std::wstring`.

The UI shell is `main.cpp`. The editor surface is `EditorView`, which knows how
to render and navigate either backend.
