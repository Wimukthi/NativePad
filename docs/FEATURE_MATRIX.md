# Feature Matrix

This table describes the current implementation state, not the final target.

## File

| Feature | Status | Notes |
| --- | --- | --- |
| New | Done | Prompts to save dirty document |
| Open | Done | Common file dialog, drag/drop, command-line path |
| Save | Done | Preserves detected encoding and line endings for editable documents |
| Save As | Done | Common save dialog |
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
| Go To | Done | Custom dark-aware dialog; disabled when Word Wrap is on |
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

## Text and File Behavior

| Feature | Status | Notes |
| --- | --- | --- |
| UTF-8 load | Done | BOM and no-BOM paths |
| UTF-16 LE/BE load | Done | BOM detected |
| ANSI fallback load | Done | Used when UTF-8 decode fails |
| Save encoding | Done | Preserves UTF-8, UTF-8 BOM, UTF-16 LE/BE, and ANSI where representable |
| Line-ending preservation | Done | CRLF/LF/CR files normalize back to detected style; mixed files are left mixed |
| Preferences | Done | Persists dark override, word wrap, line numbers, status bar, font, window placement, and page margins |
| Large-file viewing | Done | Read-only mapped backend above editable limit |
| Large-file editing | Not done | Requires new storage model |

## Known Limitations

- Mapped large files are read-only.
- UTF-8/ANSI mapped files use byte offsets internally.
- Save As does not yet expose a classic-style encoding picker; it preserves the
  current document encoding.
- Print fidelity needs more manual testing against classic Notepad behavior.
