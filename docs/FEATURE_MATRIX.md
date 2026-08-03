# Feature Matrix

What NativePad implements today. Keyboard shortcuts and day-to-day behavior are
in [Usage](USAGE.md); this page is the inventory.

## File

| Feature | Notes |
| --- | --- |
| New | Prompts to save a modified document |
| Open | Common file dialog, drag-and-drop, and command-line path |
| Save | Preserves the detected encoding and line endings |
| Save As | Native dialog with an encoding picker |
| Recent Files | Eight entries in the File menu, persisted; missing files are pruned when opened |
| Page Setup | Native dialog; margins persist |
| Print | Native dialog; pagination and spooling on a worker thread |
| Exit | Prompts to save a modified document |

## Edit

| Feature | Notes |
| --- | --- |
| Undo / Redo | Editable documents only |
| Cut / Copy / Paste / Delete | Unicode clipboard text |
| Selection | Drag, double-click word, triple-click logical line |
| Find | Editable and mapped documents |
| Find Next / Previous | Wraps around |
| Replace / Replace All | Editable documents only; Replace All reports its count |
| Go To | Available regardless of Word Wrap |
| Select All | Editable and mapped documents |
| Time/Date | Inserts the localized time and date |
| Duplicate / Delete Line | `Ctrl+Shift+D` and `Ctrl+Shift+K`, one undo step each |
| Move Line Up / Down | `Alt+Up` and `Alt+Down`, preserving line endings, one undo step |
| Context menu | Editor right-click menu |
| Enable Large-File Editing | Switches a mapped file to the editable large-file backend |

## Format and View

| Feature | Notes |
| --- | --- |
| Word Wrap | Backed by a lazy visual-row cache |
| Font | Custom resizable, theme-aware dialog |
| Line Numbers | Visual gutter only; excluded from save, copy, search, and print |
| Status Bar | Line, column, total lines, encoding, mode, size, character count, zoom |
| Dark Mode | Follows the system theme, with a persisted manual override |
| Zoom | 10%–500% in 10% steps via `Ctrl`+wheel, `Ctrl+Plus/Minus`, `Ctrl+0`; scales rendering only, not the saved font size |
| Follow Tail | `F6`; follows appended content and keeps the caret at the end |

## Help and System Integration

| Feature | Notes |
| --- | --- |
| About | Version, build timestamp, author, and licence |
| Set as Default Editor | Registers a per-user `.txt` handler, then opens Windows Default Apps for confirmation — Windows requires the user to confirm. The menu shows a check when NativePad is the current default |
| Check for Updates | About-dialog command plus optional automatic startup checks; downloads and verifies the installer before launching it |

## Text and File Behavior

| Feature | Notes |
| --- | --- |
| UTF-8 load | With and without BOM |
| UTF-16 LE/BE load | Detected by BOM |
| ANSI fallback load | Used when UTF-8 decoding fails |
| Save encoding | Preserves UTF-8, UTF-8 BOM, UTF-16 LE/BE, and ANSI where representable; Save As can change it |
| Line-ending preservation | CRLF/LF/CR files normalize back to the detected style; mixed files stay mixed |
| Preferences | INI-backed: theme override, word wrap, line numbers, status bar, font, window placement, page margins, recent files, update URL and update-check preference |
| Large-file viewing | Read-only mapped backend above 512 MB |
| Large-file editing | Opt-in piece-table-over-mmap backend supporting typing, paste, undo/redo, find, and atomic save |
| External change detection | On window activation, prompts to reload a file changed on disk, warning before discarding unsaved edits |
| Crash recovery | Dirty documents are journaled to `%LOCALAPPDATA%\NativePad\Recovery`; abandoned journals are offered for restore on the next launch |

## Known Limitations

- Large files open read-only; editing is opt-in through **Edit > Enable
  Large-File Editing**.
- Byte-backed mapped and large-file backends report byte columns, and caret
  navigation over them is not grapheme-aware.
- Editable large files support neither printing, nor crash-recovery journaling,
  nor encoding conversion on Save As.
- Print fidelity against classic Notepad still needs broader manual testing.
- Releases are unsigned, so SmartScreen warns on first run.
