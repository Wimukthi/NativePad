# NativePad Documentation

Reference material for using, building, and extending NativePad. Everything
here describes the implementation as it exists today, not a target state.

## Start Here

**Using NativePad**

1. [Usage](USAGE.md) — menus, keyboard shortcuts, status bar, settings file.
2. [Feature Matrix](FEATURE_MATRIX.md) — what is implemented and what is not.
3. [Large Files](LARGE_FILES.md) — how oversized files are opened and edited.

**Working on NativePad**

1. [Build and Test](BUILD_AND_TEST.md) — toolchain, build commands, test suite.
2. [Architecture](ARCHITECTURE.md) — module map, document backends, threading.
3. [UI and Theming](UI_AND_THEMING.md) — dark mode, DPI, custom painting.
4. [Contributing](../CONTRIBUTING.md) — code style and review expectations.

**Shipping NativePad**

1. [Versioning](VERSIONING.md) — the four-part version and its auto-increment.
2. [Installer](INSTALLER.md) — Inno Setup packaging and install behavior.
3. [Release Checklist](RELEASE_CHECKLIST.md) — manual verification pass.

**Background**

- [Classic Notepad Parity](CLASSIC_NOTEPAD_PARITY.md) — what matches classic
  Notepad, what deliberately differs, and what NativePad adds.

## Design Principles

These constraints explain most of the code you will read.

| Principle | Consequence |
| --- | --- |
| Native | Direct Win32 and Direct2D/DirectWrite calls; no widget framework, no managed runtime |
| Fast | Startup does no scanning, no plugin discovery, and no network I/O |
| Familiar | Command shape and accelerators follow classic Notepad unless there is a concrete reason to differ |
| Honest about size | Files that cannot be held in memory are never silently truncated or partially loaded |
| Conservative | Few dependencies, explicit ownership, and warnings treated as errors |

## Implementation Shape

NativePad has three document backends behind one editor control:

| Backend | Storage | Editable |
| --- | --- | --- |
| `DocumentBuffer` | Piece table over decoded UTF-16 text | Yes |
| `MappedTextDocument` | Memory-mapped file plus a line-start index | No |
| `LargeTextDocument` | Piece table over the memory-mapped original | Yes, opt-in |

`AppWindow` in `src/main.cpp` owns the shell — menus, status bar, command
routing, and document state. `EditorView` owns rendering and navigation and
works against whichever backend is active. Dialogs, file I/O, printing, popup
menus, settings, and crash recovery each live in their own translation unit
under `src/`. See [Architecture](ARCHITECTURE.md).

## Screenshots

Images under [`images/`](images) are captured from a Release build at 100% DPI
with the default Consolas font. Refresh them when a UI change makes them
misleading.
