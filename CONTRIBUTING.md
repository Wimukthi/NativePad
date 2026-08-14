# Contributing to NativePad

NativePad is intentionally conservative C++/Win32 code. Prefer clear ownership,
explicit Win32 behavior, and small focused changes over clever abstraction.

Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) before your first change —
most review comments come from code that ignores the module boundaries it
describes.

## Getting Set Up

Follow [docs/BUILD_AND_TEST.md](docs/BUILD_AND_TEST.md). In short: Visual
Studio 2026 with the `v145` toolset, x64 only, and a
[`Wimukthi.Win32Theme`](https://github.com/Wimukthi/Wimukthi.Win32Theme)
checkout beside this repository.

## Code Style

- C++20, Unicode Win32 APIs, x64 only.
- Warning-clean at level 4, with warnings as errors. Do not suppress a warning
  to make a build pass.
- Prefer RAII for owned resources. Where RAII is impractical, make handle
  ownership explicit in a comment.
- Keep dependencies minimal. A new third-party dependency needs a real
  justification and an entry in
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- Match the surrounding file — naming, comment density, and idiom.

### Comments

Comment the things a careful reader cannot infer:

- Win32 message and lifetime rules.
- Dark-mode painting and palette behavior.
- DPI and layout assumptions.
- Text coordinate systems (UTF-16 code units vs. bytes).
- Large-file performance trade-offs.
- Threading and ownership transfer.
- Data-structure invariants.

Do not comment simple assignments or obvious branches.

## Module Boundaries

These keep the codebase reviewable:

- `DocumentBuffer` stores editable text and nothing else.
- `LineIndex` tracks editable-document line starts and nothing else.
- `MappedTextDocument` stays read-only. Editable large-file work belongs in
  `LargeTextDocument`.
- `EditorView` stays backend-neutral — go through its helper methods rather
  than reaching for a specific backend.
- `DocumentSession` owns per-tab document and interaction state. Keep theme,
  font, wrap, line-number, and rendering resources window-global.
- `TabStrip` remains one lightweight HWND. Do not add per-tab editor or button
  windows.
- `SessionStore` owns normal-exit workspace serialization. `RecoveryJournal`
  remains the separate unclean-exit path for ordinary dirty buffers.
- UI commands route through `AppWindow`.
- Background threads never touch HWND-owned UI state.
- NativePad-specific palette, DPI, and control helpers live in `UiSupport`.
  Reusable Windows dark-mode integration belongs in `Wimukthi.Win32Theme` — do
  not add registry, DWM, or UxTheme workarounds to NativePad.
- Each dialog or self-contained feature gets its own translation unit with a
  minimal header. `main.cpp` stays limited to `AppWindow` and shell wiring.

## Adding a User-Facing Command

1. Add or reuse a command ID in `src/resource.h`.
2. Add the menu text in `AppWindow::MenuTextFor`.
3. Add an accelerator if classic Notepad has one.
4. Route the command through `AppWindow::OnCommand`.
5. Update `UpdateMenuState` so the command enables and disables correctly.
6. For a new dialog or self-contained surface, add a `src/` module exposing a
   single entry point, and register it in `NativePad.vcxproj` and
   `NativePad.vcxproj.filters`.
7. Add tests for the non-UI logic.
8. Update the docs — see below.

## Testing

Before opening a pull request, run both configurations:

```powershell
MSBuild.exe .\NativePad.sln /p:Configuration=Debug /p:Platform=x64 /m
```

```powershell
.\bin\x64\Debug\NativePad.Tests.exe
```

```powershell
MSBuild.exe .\NativePad.sln /p:Configuration=Release /p:Platform=x64 /m
```

```powershell
.\bin\x64\Release\NativePad.Tests.exe
```

The UI has no automated coverage, so changes to dialogs, menus, tabs, theming,
DPI handling, printing, or scrollbars need manual verification. The painting
checklist in [docs/UI_AND_THEMING.md](docs/UI_AND_THEMING.md) is the short
version; [docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md) is the full one.

Say in your pull request what you tested manually and what you did not.

Note that building the app project bumps the version in `src/NativePad.rc`.
Either include that change or revert it — do not leave it half-applied.

## Documentation

Update the docs in the same change as the behavior:

| Change | Update |
| --- | --- |
| User-visible feature or command | [docs/FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md), [docs/USAGE.md](docs/USAGE.md) |
| Component boundaries or data flow | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| Large-file open, search, or edit behavior | [docs/LARGE_FILES.md](docs/LARGE_FILES.md) |
| Custom Win32 UI behavior | [docs/UI_AND_THEMING.md](docs/UI_AND_THEMING.md) |
| Divergence from classic Notepad | [docs/CLASSIC_NOTEPAD_PARITY.md](docs/CLASSIC_NOTEPAD_PARITY.md) |
| Anything worth a release note | [CHANGELOG.md](CHANGELOG.md), under `Unreleased` |

Screenshots in `docs/images/` are captured from a Release build at 100% DPI with
the default Consolas font. Refresh them when a UI change makes them misleading.

## Pull Requests

- One logical change per pull request.
- Write a commit message that explains *why*, not just what.
- Make sure CI is green — it builds and tests Debug and Release x64.

## Reporting Bugs

Open an issue with the NativePad version from **Help > About**, your Windows
version, and the steps to reproduce. For anything involving a specific file,
the file's size and encoding matter — include both.

Security issues go through [SECURITY.md](SECURITY.md), not the public tracker.
