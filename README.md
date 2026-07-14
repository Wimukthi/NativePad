# NativePad

NativePad is a native C++ Win32 notepad replacement focused on fast startup,
classic Notepad familiarity, dark mode, high-DPI correctness, and safe handling
of very large text files.

## Current Status

- Native C++20 Win32 desktop application.
- Visual Studio solution with app and test projects.
- Custom DirectWrite editor view.
- Native Win32 menu/dialog/window surface with custom dark-mode painting where
  Windows controls do not theme cleanly.
- Per-monitor v2 DPI awareness.
- Classic Notepad-style menus, accelerators, status bar, context menu, find,
  replace, go to, font, line numbers, page setup, and print workflows.
- Recent files list, editor zoom (Ctrl+scroll, Ctrl+Plus/Minus, Ctrl+0), and
  line operations (duplicate, delete, move up/down).
- Editable normal-file path using a piece-table document buffer.
- Read-only memory-mapped large-file path for files above the editable limit,
  with opt-in editing through a piece-table-over-mmap backend.
- External change detection with a reload prompt, and a Follow Tail mode (F6)
  that follows growing files such as live logs.
- Crash recovery: unsaved work is journaled in the background and offered for
  restore on the next launch after an unclean exit.
- Unit coverage for the piece table, line index, mapped document backend, and
  text-format save helpers.

## Documentation

Start with [docs/INDEX.md](docs/INDEX.md).

Key docs:

- [Architecture](docs/ARCHITECTURE.md)
- [Build and Test](docs/BUILD_AND_TEST.md)
- [Feature Matrix](docs/FEATURE_MATRIX.md)
- [Large Files](docs/LARGE_FILES.md)
- [UI and Theming](docs/UI_AND_THEMING.md)
- [Versioning](docs/VERSIONING.md)
- [Release Checklist](docs/RELEASE_CHECKLIST.md)
- [Contributing](docs/CONTRIBUTING.md)
- [Classic Notepad Parity](docs/CLASSIC_NOTEPAD_PARITY.md)

## Quick Build

From a Visual Studio Developer PowerShell:

```powershell
MSBuild.exe .\NativePad.sln /p:Configuration=Release /p:Platform=x64 /m
```

The executable is written to:

```text
bin\x64\Release\NativePad.exe
```

Run tests:

```powershell
.\bin\x64\Release\NativePad.Tests.exe
```

Debug uses the same commands with `Configuration=Debug`.

## License

NativePad is licensed under the GNU General Public License v3.0. See
[LICENSE](LICENSE).
