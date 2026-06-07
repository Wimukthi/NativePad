# Contributing Notes

NativePad is intentionally conservative C++/Win32 code. Prefer clear ownership,
explicit Win32 behavior, and small focused changes.

## Code Style

- Use C++20.
- Keep code warning-clean with warnings as errors.
- Prefer RAII for owned resources when practical.
- Keep Win32 handles and ownership comments clear when RAII is not used.
- Use Unicode Win32 APIs.
- Keep dependencies minimal.

## Comments

Use comments for non-obvious behavior:

- Win32 message/lifetime rules.
- Dark-mode/theming workarounds.
- DPI/layout assumptions.
- Text coordinate systems.
- Large-file performance tradeoffs.
- Threading and ownership transfer.
- Data structure invariants.

Avoid comments that only restate simple assignments or obvious branches.

## Editing Guidelines

- Keep `DocumentBuffer` focused on editable text storage.
- Keep `LineIndex` focused on editable document line starts.
- Keep `MappedTextDocument` read-only until a real editable large-file design is
  implemented.
- Keep `EditorView` backend-neutral through helper methods.
- Route UI commands through `AppWindow`.
- Do not let background threads touch HWND-owned UI state.

## Adding Features

For user-facing commands:

1. Add or reuse a command ID in `src/resource.h`.
2. Add menu text in command-label handling.
3. Add accelerator if classic Notepad has one.
4. Route the command through `AppWindow::OnCommand`.
5. Update `UpdateMenuState`.
6. Add tests for non-UI logic.
7. Update docs and parity status.

## Testing Expectations

At minimum, run:

```powershell
MSBuild.exe .\NativePad.sln /p:Configuration=Debug /p:Platform=x64 /m
.\bin\x64\Debug\NativePad.Tests.exe
MSBuild.exe .\NativePad.sln /p:Configuration=Release /p:Platform=x64 /m
.\bin\x64\Release\NativePad.Tests.exe
```

Manual UI testing is expected for dialog, menu, theme, DPI, printing, and
scrollbar changes.

## Documentation Updates

Update relevant docs whenever behavior changes:

- `FEATURE_MATRIX.md` for user-visible feature state.
- `ARCHITECTURE.md` for component boundaries.
- `LARGE_FILES.md` for open/search/edit behavior on large files.
- `UI_AND_THEMING.md` for custom Win32 UI behavior.
- `CLASSIC_NOTEPAD_PARITY.md` for parity roadmap changes.
