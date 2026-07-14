# Feature Matrix

This table describes the current implementation state, not the final target.

## File

| Feature | Status | Notes |
| --- | --- | --- |
| New | Done | Prompts to save dirty document |
| Open | Done | Common file dialog, drag/drop, command-line path |
| Save | Done | Preserves detected encoding and line endings for editable documents |
| Save As | Done | Native save dialog with encoding picker |
| Recent Files | Done | Up to 8 most-recently opened files listed inline in the File menu, persisted in the INI; missing files are pruned on use; includes Clear Recent Files |
| Page Setup | Done | Native page setup dialog |
| Print | Done | Native print dialog; pagination/spooling on worker thread |
| Exit | Done | Dirty prompt |

## Edit

| Feature | Status | Notes |
| --- | --- | --- |
| Undo/Redo | Done | Editable documents only |
| Cut/Copy/Paste/Delete | Done | Clipboard uses Unicode text |
| Selection | Done | Drag selection, double-click word/token selection, and triple-click logical line selection |
| Find | Done | Editable and mapped large-file paths |
| Find Next/Previous | Done | Wraps around |
| Replace | Done | Editable documents only |
| Replace All | Done | Editable documents only |
| Go To | Done | Custom dark-aware dialog; available regardless of Word Wrap |
| Select All | Done | Works on editable and mapped documents |
| Time/Date | Done | Inserts localized time/date into editable documents |
| Duplicate/Delete Line | Done | Ctrl+Shift+D duplicates the caret line; Ctrl+Shift+K deletes it; single undo step |
| Move Line Up/Down | Done | Alt+Up / Alt+Down swap the caret line with its neighbor, preserving line endings; single undo step |
| Context menu | Done | Editor right-click menu |

## Format and View

| Feature | Status | Notes |
| --- | --- | --- |
| Word Wrap | Done | Uses visual-row cache |
| Font | Done | Custom dark-aware resizable font dialog |
| Line Numbers | Done | Optional visual gutter; persisted; excluded from save/copy/search/print |
| Status Bar | Done | Toggleable; shows line, column, total lines, encoding, and character count |
| Dark Mode | Done | System default plus manual View toggle |
| Zoom | Done | Ctrl+scroll, Ctrl+Plus/Minus, and Ctrl+0 to restore; 10%–500%; shown as a percentage in the status bar; scales rendering only, not the saved font size |
| Follow Tail | Done | View menu/F6; polls the open file and follows appended content, keeping the caret at the end. Mapped large files refresh incrementally; editable files reload from disk and stay read-only while following |

## Help and System Integration

| Feature | Status | Notes |
| --- | --- | --- |
| About | Done | Custom dark-aware dialog with version, build, author, and licence |
| Set as Default Editor | Done | Registers NativePad per-user as a `.txt` handler, then opens Windows "Default apps" to confirm; Windows requires the user to confirm the default. Help menu shows a check when NativePad is the current default |
| Check for Updates | Done | About dialog command plus optional automatic startup checks; downloads and verifies the Inno installer before launch |

## Text and File Behavior

| Feature | Status | Notes |
| --- | --- | --- |
| UTF-8 load | Done | BOM and no-BOM paths |
| UTF-16 LE/BE load | Done | BOM detected |
| ANSI fallback load | Done | Used when UTF-8 decode fails |
| Save encoding | Done | Preserves UTF-8, UTF-8 BOM, UTF-16 LE/BE, and ANSI where representable; Save As can change encoding |
| Line-ending preservation | Done | CRLF/LF/CR files normalize back to detected style; mixed files are left mixed |
| Preferences | Done | INI-backed persistence for dark override, word wrap, line numbers, status bar, font, window placement, page margins, recent files, update URL, and update-check preference |
| Large-file viewing | Done | Read-only mapped backend above editable limit |
| Large-file editing | Done | Edit > Enable Large-File Editing reopens the file through a piece-table-over-mmap backend; supports typing, paste, undo/redo, find, and save. Printing, crash-recovery journaling, and Save As encoding conversion are not supported for large files |
| External change detection | Done | On window activation, prompts to reload when the open file changed on disk; warns before discarding unsaved edits |
| Crash recovery | Done | Dirty editable documents are journaled to `%LOCALAPPDATA%\NativePad\Recovery` every few seconds; abandoned journals are offered for restore on the next launch |

## Known Limitations

- Large files open read-only; editing is opt-in via Edit > Enable Large-File Editing.
- UTF-8/ANSI mapped and large-file backends use byte offsets internally, so caret
  navigation is not grapheme-aware.
- Editable large files do not support printing, crash-recovery journaling, or
  encoding conversion on Save As.
- Print fidelity needs more manual testing against classic Notepad behavior.
