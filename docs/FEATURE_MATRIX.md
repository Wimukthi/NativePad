# Feature Matrix

This table describes the current implementation state, not the final target.

## File

| Feature | Status | Notes |
| --- | --- | --- |
| New | Done | Prompts to save dirty document |
| Open | Done | Common file dialog, drag/drop, command-line path |
| Save | Done | Preserves detected encoding and line endings for editable documents |
| Save As | Done | Native save dialog with encoding picker |
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
| Context menu | Done | Editor right-click menu |

## Format and View

| Feature | Status | Notes |
| --- | --- | --- |
| Word Wrap | Done | Uses visual-row cache |
| Font | Done | Custom dark-aware resizable font dialog |
| Line Numbers | Done | Optional visual gutter; persisted; excluded from save/copy/search/print |
| Status Bar | Done | Toggleable; shows line, column, total lines, encoding, and character count |
| Dark Mode | Done | System default plus manual View toggle |
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
| Preferences | Done | INI-backed persistence for dark override, word wrap, line numbers, status bar, font, window placement, page margins, update URL, and update-check preference |
| Large-file viewing | Done | Read-only mapped backend above editable limit |
| Large-file editing | Not done | Requires new storage model |
| External change detection | Done | On window activation, prompts to reload when the open file changed on disk; warns before discarding unsaved edits |

## Known Limitations

- Mapped large files are read-only.
- UTF-8/ANSI mapped files use byte offsets internally.
- Print fidelity needs more manual testing against classic Notepad behavior.
