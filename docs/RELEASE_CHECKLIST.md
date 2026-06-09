# Release Checklist

Use this checklist before publishing a NativePad build. It is intentionally
manual-heavy because the highest-risk areas are Win32 UI behavior, common
dialogs, printing, DPI changes, and real-world file handling.

## Version and Build

- Confirm `src/NativePad.rc` has the intended `FILEVERSION`,
  `PRODUCTVERSION`, `FileVersion`, and `ProductVersion`.
- Confirm the About dialog shows the expected version, build timestamp, author,
  and GPL V3 license.
- Build Release x64.
- Run `NativePad.Tests.exe` from the Release output folder.
- Confirm GitHub Actions `CI` passes on `main`.

## File Workflows

- New prompts to save a dirty document.
- Open works from the File menu, `Ctrl+O`, drag/drop, and command-line path.
- Save preserves the detected encoding and line endings.
- Save As can save as UTF-8, UTF-8 BOM, UTF-16 LE, UTF-16 BE, and ANSI.
- ANSI Save As rejects text that cannot be represented by the system ANSI code
  page without truncating the target file.
- Exit prompts for dirty documents and exits cleanly after Save, Don't Save, and
  Cancel paths.

## Editor Behavior

- Typing, delete, cut, copy, paste, undo, and redo work on normal editable files.
- Double-click selects a word/token.
- Triple-click selects the logical line.
- Select All works on editable and mapped documents.
- The caret blinks and remains visible while typing, scrolling, and resizing.
- The mouse cursor is an I-beam only over editable text, not over menus, status
  bar, scrollbars, or dialogs.

## Search and Replace

- Find, Find Next, and Find Previous wrap correctly.
- Match case changes results.
- Up/Down direction works in Find and Replace.
- Replace changes only the selected match.
- Replace All reports the replacement count.
- Esc closes Find/Replace dialogs without losing the editor selection.

## Format and View

- Word Wrap toggles without corrupting scroll position.
- Go To is disabled while Word Wrap is enabled.
- Font dialog can resize without repaint artifacts.
- Line Numbers toggle and persist.
- Status Bar toggle persists and shows line, column, total lines, encoding, and
  character count.

## Dark Mode and DPI

- App follows system dark mode when no manual override is set.
- View > Dark Mode override persists.
- Main menus, editor context menu, status bar, custom dialogs, and scrollbars are
  usable in dark mode.
- Popup menu borders and shadows are subtle and do not steal active-window focus.
- Moving between 100%, 125%, 150%, and 200% DPI monitors keeps text, menus,
  dialogs, and scrollbars correctly sized.

## Large Files

- Files over the editable threshold open through the read-only mapped backend.
- Scrolling remains responsive on multi-million-line files.
- Find remains responsive on large mapped files.
- Save, Save As, Replace, Replace All, Cut, Delete, typing, and Paste are
  disabled for mapped read-only files.
- Copy, Select All, Find, Find Next/Previous, and Go To work on mapped files.

## Printing

- Page Setup opens and persists margins.
- Print opens the native print dialog.
- Printing runs without blocking editor repaint.
- Long files paginate consistently.
- Wrapped and unwrapped output are manually compared against classic Notepad.

## Packaging

- Run the `Release Package` GitHub Actions workflow with the intended version.
- Download and inspect the ZIP artifact.
- Confirm the ZIP contains `NativePad.exe`, `README.md`, `LICENSE`, and `docs`.
- Launch `NativePad.exe` from the extracted ZIP.
- Keep unsigned ZIP releases clearly labeled until code signing is added.
