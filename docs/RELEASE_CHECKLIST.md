# Release Checklist

Use this checklist before publishing a NativePad build. It is intentionally
manual-heavy because the highest-risk areas are Win32 UI behavior, common
dialogs, printing, DPI changes, and real-world file handling.

## Version and Build

- Confirm `src/NativePad.rc` has the intended `major.minor.patch` before the
  Release build. The build step increments the fourth component automatically.
- Confirm `FILEVERSION`, `PRODUCTVERSION`, `FileVersion`, and `ProductVersion`
  stay aligned after the build.
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
- Go To and status-bar line count remain available while Word Wrap is enabled.
- Font dialog can resize without repaint artifacts.
- Line Numbers toggle and persist.
- Status Bar toggle persists and shows line, column, total lines, encoding, and
  character count.
- With line numbers enabled and Word Wrap disabled, long pasted lines do not
  paint into the gutter while horizontally scrolled.

## Dark Mode and DPI

- App follows system dark mode when no manual override is set.
- View > Dark Mode override persists.
- Main menus, editor context menu, status bar, custom dialogs, and scrollbars are
  usable in dark mode.
- NativePad-owned save confirmations, errors, and informational prompts use the
  custom dark message dialog.
- Custom message prompt icons remain crisp at 150% and 200% scaling.
- Alt/F10 reveals top-level menu mnemonic underlines, and Esc returns focus to
  the editor.
- Popup menu borders and shadows are subtle and do not steal active-window focus.
- Custom popup menus and the editor context menu show the arrow cursor, not the
  editor I-beam.
- Moving between 100%, 125%, 150%, and 200% DPI monitors keeps text, menus,
  dialogs, and scrollbars correctly sized.
- Startup and wake-from-sleep repaint directly to the active theme without a
  white editor surface persisting.

## Updates

- Help does not show update-specific menu items.
- About dialog Check for Updates reports the current version when no newer
  release is available.
- About dialog Check automatically checkbox toggles the persisted setting.
- Update settings are written to `%LOCALAPPDATA%\NativePad\NativePad.ini`,
  including `UpdateUrl`, `CheckForUpdates`, and `LastUpdateCheckUtc`.
- A downloaded installer is stored under `%LOCALAPPDATA%\NativePad\Updates`.
- Dirty documents prompt before the downloaded installer is launched.

## External Changes and Follow Tail

- Editing the open file in another program and re-activating NativePad prompts
  to reload; declining does not re-prompt until the file changes again.
- The reload prompt warns explicitly when unsaved changes would be lost.
- View > Follow Tail and F6 toggle tail following; the title shows `[Tail]` and
  the status bar shows `FOLLOW TAIL`.
- While following, appended lines from a live writer appear within about a
  second and the view stays pinned to the end of the document.
- Follow Tail on a mapped multi-GB file follows appends without a full rescan
  (scrolling stays responsive while the file grows).
- Editing commands and Save are disabled while Follow Tail is active and come
  back when it is turned off.
- Log rotation (delete + rename) while following reloads the new file.
- Opening a different file or File > New turns Follow Tail off.

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

- Run the `Release Package` GitHub Actions workflow with the expected version
  after automatic build increment.
- Download and inspect the ZIP artifact.
- Confirm the ZIP contains `NativePad.exe`, `README.md`, `LICENSE`, and `docs`.
- Launch `NativePad.exe` from the extracted ZIP.
- Download and inspect the Inno Setup installer artifact.
- Install NativePad from the installer, launch it from the Start Menu shortcut,
  then uninstall it from Windows Settings.
- Confirm Setup and Uninstall follow Windows light/dark app mode.
- Run the installer over an older version and confirm the update option is
  shown.
- Run the installer over the same version and confirm the repair/reinstall
  option is shown.
- Confirm the installer maintenance page can launch the existing uninstaller.
- Keep unsigned ZIP and installer releases clearly labeled until code signing is
  added.
