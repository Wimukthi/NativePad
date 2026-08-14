# Release Checklist

Run this before publishing a NativePad build. It is deliberately manual-heavy:
the highest-risk surfaces — Win32 painting, common dialogs, printing, DPI
changes, and real-world files — have no automated coverage.

Work top to bottom. Anything that fails blocks the release.

## 1. Version and Build

- [ ] `src/NativePad.rc` has the intended `major.minor.patch` before the Release
      build. The build increments the fourth component itself.
- [ ] `FILEVERSION`, `PRODUCTVERSION`, `FileVersion`, and `ProductVersion`
      still agree after the build.
- [ ] Release x64 builds clean, with no warnings.
- [ ] `bin\x64\Release\NativePad.Tests.exe` passes, exit code `0`.
- [ ] The release package's `NativePad.exe` passes `upx --test` after packing;
      code signing, if added, happens after UPX.
- [ ] GitHub Actions `CI` is green on `main`.
- [ ] The About dialog shows the expected version, build timestamp, author, and
      GPL V3 licence.
- [ ] [CHANGELOG.md](../CHANGELOG.md) has an entry for this version.

## 2. File Workflows

- [ ] New Tab (`Ctrl+N` and `+`) opens an empty tab without disturbing modified
      tabs.
- [ ] Open works from the File menu and `Ctrl+O`; multi-file drag-and-drop and
      multiple command-line paths each open all files as tabs.
- [ ] Opening the same canonical path twice activates its existing tab.
- [ ] Open/Save filters include the supported plain-text/data extensions, and
      JSON, INI, Markdown, and XML files receive readable highlighting.
- [ ] A CP437 `.nfo` file preserves box-drawing characters on open, search, and
      save; UTF-8 and UTF-16 NFO files remain readable.
- [ ] Save preserves the detected encoding and line endings.
- [ ] Save As writes UTF-8, UTF-8 BOM, UTF-16 LE, UTF-16 BE, ANSI, and OEM 437 (NFO).
- [ ] An ANSI Save As of unrepresentable text is refused without truncating the
      target file.
- [ ] Exit closes without per-tab save prompts and leaves the complete workspace
      available for the next launch.

## 3. Editing

- [ ] Typing, delete, cut, copy, paste, undo, and redo work.
- [ ] Double-click selects a word; triple-click selects the logical line.
- [ ] Dragging a selection above, below, left, or right of the editor scrolls at
      a bounded rate and continues while the pointer remains outside.
- [ ] Mouse wheel and scrollbar input leave vertical position at zero when the
      complete document fits in the editor.
- [ ] Select All works on editable and mapped documents.
- [ ] The caret blinks and stays visible while typing, scrolling, and resizing.
- [ ] The I-beam appears only over editable text — not over menus, the status
      bar, scrollbars, or dialogs.
- [ ] `Ctrl+Shift+D`, `Ctrl+Shift+K`, `Alt+Up`, and `Alt+Down` each form a
      single undo step and preserve line endings.

### Tabs

- [ ] Switching tabs preserves each document, undo/redo history, caret,
      selection, vertical/horizontal scroll, encoding, and dirty state.
- [ ] `Ctrl+W`, the close button, and middle-click close the intended tab and
      prompt only when that tab is dirty.
- [ ] `Ctrl+Tab`, `Ctrl+Shift+Tab`, `Ctrl+PageDown`, and `Ctrl+PageUp` cycle in
      the expected direction.
- [ ] Closing the final tab creates a fresh `Untitled`; repeating the action
      never increments its number.
- [ ] At narrow widths and with many tabs, overflow arrows keep every tab
      reachable and the active tab visible.
- [ ] The active tab is immediately distinguishable in light, dark, and High
      Contrast modes; the `+` button follows the visible tabs.
- [ ] Tabs have smooth rounded top edges and a compact 30-DIP height at 100%
      scaling, with proportional geometry at other DPIs.
- [ ] Close and New Tab glyphs are centered and antialiased at 100%, 125%, 150%,
      and 200% DPI, with compact rounded hover feedback and no clipping.
- [ ] Process inspection shows one `NativePadEditorView` and one
      `NativePadTabStrip` regardless of tab count.
- [ ] **View > Tab Bar** hides the strip, recovers its full height for the
      editor, persists across restart, and leaves keyboard tab switching usable.
- [ ] A normal exit and relaunch restores tab order, active tab, paths,
      encoding/line-ending metadata, Follow Tail state, and exact dirty or
      untitled content.
- [ ] Clean file-backed tabs reopen from disk, while a dirty editable-large tab
      restores from its session snapshot and can still be saved to its original
      path.
- [ ] Explicitly closing a tab keeps it out of the next saved session; canceling
      the dirty-tab prompt leaves it open.
- [ ] A session-write failure reports the error and leaves NativePad open.

## 4. Search and Replace

- [ ] Find, Find Next, and Find Previous wrap correctly.
- [ ] Match case changes the results.
- [ ] Up/Down direction works in both Find and Replace.
- [ ] Replace changes only the selected match; Replace All reports its count.
- [ ] `Esc` closes the dialogs without losing the editor selection.

## 5. Recent Files

- [ ] Opening files fills the list, most recent first, no duplicates, capped at
      eight.
- [ ] The list survives a restart.
- [ ] A recent entry opens or activates its tab without disturbing modified
      tabs; an entry whose file no longer exists is removed after the failed open.
- [ ] Clear Recent Files empties the list.

## 6. Format and View

- [ ] Word Wrap toggles without corrupting the scroll position.
- [ ] Go To and the status-bar line count stay available with Word Wrap on.
- [ ] The Font dialog resizes without repaint artifacts.
- [ ] Tab Bar, Line Numbers, and Status Bar toggle and persist.
- [ ] With line numbers on and Word Wrap off, long pasted lines do not paint
      into the gutter while horizontally scrolled.
- [ ] `Ctrl`+wheel, `Ctrl+Plus/Minus`, and `Ctrl+0` zoom; the status bar shows
      the percentage and the saved font size is unchanged.

## 7. Theme and DPI

- [ ] The app follows system dark mode when no override is set, and the View
      override persists.
- [ ] Menus, the editor context menu, status bar, custom dialogs, and
      scrollbars are all usable in dark mode.
- [ ] Windows High Contrast disables custom dark styling and every custom
      surface uses readable system colours.
- [ ] NativePad-owned save confirmations, errors, and information prompts use
      the custom message dialog.
- [ ] Message prompt icons stay crisp at 150% and 200%.
- [ ] `Alt`/`F10` reveals mnemonic underlines; `Esc` returns focus to the
      editor.
- [ ] Popup menu borders and shadows are subtle and do not steal activation.
- [ ] Popup and context menus show the arrow cursor, not the I-beam.
- [ ] Moving between 100%, 125%, 150%, and 200% monitors keeps text, menus,
      dialogs, and scrollbars correctly sized.
- [ ] Disconnecting the monitor holding the window moves it to the primary
      monitor — normal and maximized — and launching with a saved position on
      an absent monitor also lands on the primary.
- [ ] Startup and wake-from-sleep repaint straight into the active theme, with
      no lingering white editor surface.

## 8. Large Files

- [ ] Files over 512 MB open through the read-only mapped backend.
- [ ] Scrolling and Find stay responsive on multi-million-line files.
- [ ] Save, Save As, Replace, Replace All, Cut, Delete, typing, and Paste are
      disabled for mapped files.
- [ ] Copy, Select All, Find, Find Next/Previous, and Go To work on them.

### Editable large files

- [ ] **Edit > Enable Large-File Editing** is enabled only for a read-only
      mapped file; switching shows `[Large file]` in the title and `LARGE FILE`
      in the status bar.
- [ ] Typing, paste, cut, delete, undo/redo, find, and replace all work.
- [ ] Save writes the edited content back, reopening shows the change, and the
      file is replaced atomically — an interrupted save leaves no truncated
      file.
- [ ] Save As writes a new file in the document's encoding.
- [ ] Editing near multibyte UTF-8 characters never corrupts them.
- [ ] Printing and Save As encoding conversion are disabled.
- [ ] Switching tabs retains the mapping and state; closing its tab releases it.

## 9. External Changes and Follow Tail

- [ ] Editing the open file elsewhere and re-activating NativePad prompts to
      reload; declining does not re-prompt until the file changes again.
- [ ] The reload prompt warns explicitly when unsaved changes would be lost.
- [ ] `F6` and **View > Follow Tail** toggle following; the title shows
      `[Tail]` and the status bar shows `FOLLOW TAIL`.
- [ ] Appended lines from a live writer appear within about a second and the
      view stays pinned to the end.
- [ ] Following a multi-GB mapped file does not trigger a full rescan —
      scrolling stays responsive as the file grows.
- [ ] Editing commands and Save are disabled while following, and return
      afterwards.
- [ ] Log rotation (delete + rename) reloads the new file.
- [ ] Switching away suspends polling without clearing Follow Tail; switching
      back catches up immediately and resumes polling.

## 10. Crash Recovery

- [ ] Normal-exit session restore uses `%LOCALAPPDATA%\NativePad\Session` and
      does not produce an abandoned-journal prompt on the next launch.

- [ ] Typing into a document and killing the process from Task Manager leaves a
      journal pair under `%LOCALAPPDATA%\NativePad\Recovery`.
- [ ] Multiple dirty tabs create independent `session-<pid>-<tab-id>` journal
      pairs. The next launch offers all of them and restores each accepted
      snapshot as a dirty tab with the original path and encoding.
- [ ] Declining discards the journal without re-prompting on the next launch.
- [ ] Save, Save As, closing that tab, and a clean exit remove the appropriate
      journal; switching or opening another tab leaves it intact.
- [ ] Two NativePad instances do not claim each other's journals.

## 11. Printing

- [ ] Page Setup opens and its margins persist.
- [ ] Print opens the native dialog and runs without blocking editor repaint.
- [ ] Long files paginate consistently.
- [ ] Wrapped and unwrapped output are compared against classic Notepad.

## 12. Updates

- [ ] The Help menu shows no update-specific items — updates live in About.
- [ ] About > Check for Updates reports the current version when nothing newer
      exists.
- [ ] The **Check automatically** checkbox toggles the persisted setting.
- [ ] `UpdateUrl`, `CheckForUpdates`, and `LastUpdateCheckUtc` are written to
      `%LOCALAPPDATA%\NativePad\NativePad.ini`.
- [ ] A downloaded installer lands in `%LOCALAPPDATA%\NativePad\Updates`.
- [ ] The current tab session is stored before the downloaded installer
      launches, without per-tab save prompts.

## 13. Packaging

- [ ] Run the `Release Package` workflow with the version committed in
      `src/NativePad.rc`.
- [ ] Leave `pack_with_upx` enabled for the normal release; use the uncompressed
      option only when documenting a compatibility or antivirus exception.
- [ ] The ZIP contains `NativePad.exe`, `README.md`, `LICENSE`,
      `THIRD_PARTY_NOTICES.md`, `CHANGELOG.md`, `licenses`, and `docs`.
- [ ] `licenses\source\darkmodelib` holds the complete corresponding
      Darkmodelib source, in both the ZIP and the installed application.
- [ ] `NativePad.exe` launches from the extracted ZIP.
- [ ] Install from the installer, launch from the Start Menu shortcut, then
      uninstall from Windows Settings.
- [ ] Setup and Uninstall follow the Windows light/dark app mode.
- [ ] Running the installer over an older version offers the update option.
- [ ] Running it over the same version offers repair/reinstall.
- [ ] The maintenance page can launch the existing uninstaller.
- [ ] The ZIP and installer are clearly labelled unsigned on the release page.
