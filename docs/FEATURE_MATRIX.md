# Feature Matrix

What NativePad implements today. Keyboard shortcuts and day-to-day behavior are
in [Usage](USAGE.md); this page is the inventory.

## File

| Feature | Notes |
| --- | --- |
| New Tab | `Ctrl+N` or the tab-strip `+` button; existing work remains open |
| Open | Common file dialog, multi-file drag-and-drop, and multiple command-line paths; duplicate paths activate their existing tab |
| Text file associations | Open With registration for common plain-text/data extensions; Windows still requires confirmation for the default choice |
| Save | Preserves the detected encoding and line endings |
| Save As | Native dialog with an encoding picker |
| Close Tab | `Ctrl+W`, close button, or middle-click; prompts only for that tab's unsaved work |
| Recent Files | Eight entries in the File menu, persisted; missing files are pruned when opened |
| Page Setup | Native dialog; margins persist |
| Print | Native dialog; pagination and spooling on a worker thread |
| Exit | Saves the tab session, including unsaved content, and closes without per-tab prompts |

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
| Syntax highlighting | Line-local color highlighting for JSON, INI/configuration, Markdown, and XML; plain text and large-file backends use the fast single-brush renderer |
| Tabs | One shared editor/render target; compact rounded surfaces, adjacent New Tab control, antialiased controls, optional persisted tab-bar visibility, and per-tab backend, undo/redo, caret, selection, scroll, dirty state, Follow Tail, and recovery |
| Drag-selection autoscroll | Captured selections continue scrolling vertically or horizontally while the pointer is held outside the editor |

## Help and System Integration

| Feature | Notes |
| --- | --- |
| About | Version, build timestamp, author, and licence |
| Set as Default Text Editor | Registers common plain-text handlers, then opens Windows Default Apps for confirmation — Windows requires the user to confirm. The menu shows a check when NativePad is the current `.txt` default |
| Check for Updates | About-dialog command plus optional automatic startup checks; downloads and verifies the installer before launching it |

## Text and File Behavior

| Feature | Notes |
| --- | --- |
| UTF-8 load | With and without BOM |
| UTF-16 LE/BE load | Detected by BOM |
| ANSI fallback load | Used when UTF-8 decoding fails; `.nfo` files use OEM 437 for legacy DOS artwork |
| Save encoding | Preserves UTF-8, UTF-8 BOM, UTF-16 LE/BE, ANSI, and OEM 437 where representable; Save As can change it |
| Line-ending preservation | CRLF/LF/CR files normalize back to the detected style; mixed files stay mixed |
| Preferences | INI-backed: theme override, word wrap, line numbers, status bar, font, window placement, page margins, recent files, update URL and update-check preference |
| Large-file viewing | Read-only mapped backend above 512 MB |
| Large-file editing | Opt-in piece-table-over-mmap backend supporting typing, paste, undo/redo, find, and atomic save |
| External change detection | On window activation, prompts to reload a file changed on disk, warning before discarding unsaved edits |
| Session restore | Normal exit persists tab order, active tab, paths, formats, Follow Tail state, and unsaved normal/large-file content under `%LOCALAPPDATA%\NativePad\Session` |
| Crash recovery | Dirty tabs have independent journals under `%LOCALAPPDATA%\NativePad\Recovery`; every abandoned journal can restore into its own tab |

## Known Limitations

- Large files open read-only; editing is opt-in through **Edit > Enable
  Large-File Editing**.
- Byte-backed mapped and large-file backends report byte columns, and caret
  navigation over them is not grapheme-aware.
- Editable large files support neither printing, nor crash-recovery journaling,
  nor encoding conversion on Save As. Their edits are preserved by normal-exit
  session restore.
- Print fidelity against classic Notepad still needs broader manual testing.
- Releases are unsigned, so SmartScreen warns on first run.
