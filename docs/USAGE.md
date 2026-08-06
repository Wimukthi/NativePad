# Usage

NativePad opens a file passed on the command line, dropped onto its window, or
chosen from **File > Open**. With no argument it starts on an empty `Untitled`
document.

```powershell
NativePad.exe "C:\logs\server.log"
```

## Menus

### File

| Command | Shortcut | Notes |
| --- | --- | --- |
| New | `Ctrl+N` | Prompts to save a modified document first |
| Open | `Ctrl+O` | Also accepts drag-and-drop onto the window |
| Save | `Ctrl+S` | Keeps the encoding and line endings detected on open |
| Save As | `Ctrl+Shift+S` | Encoding picker: UTF-8, UTF-8 BOM, UTF-16 LE, UTF-16 BE, ANSI, OEM 437 (NFO) |
| Recent files | — | Up to eight entries, most recent first; missing files are dropped when opened |
| Clear Recent Files | — | Empties the list |
| Page Setup | — | Native dialog; margins persist between sessions |
| Print | `Ctrl+P` | Native dialog; pagination and spooling run off the UI thread |
| Exit | `Alt+F4` | Prompts to save a modified document |

### Edit

| Command | Shortcut | Notes |
| --- | --- | --- |
| Undo / Redo | `Ctrl+Z` / `Ctrl+Y` | Editable documents only |
| Cut / Copy / Paste | `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Clipboard uses Unicode text |
| Delete | `Del` | |
| Find | `Ctrl+F` | Wrap-around, match case, up/down direction |
| Find Next / Previous | `F3` / `Shift+F3` | |
| Replace | `Ctrl+H` | Replace All reports the number of replacements |
| Go To | `Ctrl+G` | Available even with Word Wrap on |
| Select All | `Ctrl+A` | Works on read-only mapped documents too |
| Time/Date | `F5` | Inserts the localized time and date |
| Duplicate Line | `Ctrl+Shift+D` | One undo step |
| Delete Line | `Ctrl+Shift+K` | One undo step |
| Move Line Up / Down | `Alt+Up` / `Alt+Down` | Preserves line endings; one undo step |
| Enable Large-File Editing | — | Only enabled for a read-only mapped file — see [Large Files](LARGE_FILES.md) |

### Format

| Command | Notes |
| --- | --- |
| Word Wrap | Wraps to the window width; the setting persists |
| Font | Family, style, and size for the editor; the setting persists |

### View

| Command | Shortcut | Notes |
| --- | --- | --- |
| Zoom In / Out | `Ctrl+Plus` / `Ctrl+Minus` | Also `Ctrl`+mouse wheel; 10% steps between 10% and 500% |
| Restore Default Zoom | `Ctrl+0` | Zoom scales rendering only — the saved font size does not change |
| Line Numbers | — | Visual gutter only; never saved, copied, searched, or printed |
| Status Bar | — | Toggles the bar described below |
| Dark Mode | — | Overrides the system app theme; the override persists |
| Follow Tail | `F6` | Follows a file that is still being written |

### Help

| Command | Notes |
| --- | --- |
| Set as Default Text Editor | Registers NativePad as a per-user plain-text handler, then opens Windows Default Apps so you can confirm. A check mark shows when NativePad is the current `.txt` default |
| About NativePad | Version, build timestamp, author, and licence — plus **Check for Updates** and the **Check automatically** toggle |

Right-clicking the editor opens a context menu with Undo, Redo, Cut, Copy,
Paste, Delete, Find, Replace, and Select All. `Alt` or `F10` reveals the
menu-bar mnemonics and moves focus into menu navigation; `Esc` returns focus to
the editor.

## File Types and Highlighting

Open and Save As dialogs group common text and data files together. NativePad
registers the following per-user Open With associations when you choose **Help
> Set as Default Text Editor**:

`.txt`, `.log`, `.ini`, `.cfg`, `.conf`, `.csv`, `.tsv`, `.md`, `.json`, `.xml`,
`.nfo`, `.yaml`, and `.yml`.

Ordinary editable JSON, INI/configuration, Markdown, and XML files receive
lightweight color highlighting based on their extension. NFO files are treated
as plain text and legacy DOS NFO bytes are decoded as OEM code page 437 when
the file contains CP437 artwork; UTF-8 and Unicode NFO files remain supported.
The highlighting is deliberately line-local and does not change editing,
searching, saving, or large-file behavior; unknown extensions and data formats
remain plain text.

## Status Bar

```text
Ln 1, Col 1    Lines 6400001    UTF-8/ANSI    READ-ONLY MAPPED    588 MB    617599993 chars    100%
```

| Field | Meaning |
| --- | --- |
| `Ln`, `Col` | Caret position, 1-based |
| `Lines` | Logical line count, unaffected by word wrap |
| Encoding | Encoding detected on open |
| Mode | `READ-ONLY MAPPED`, `LARGE FILE`, or `READ-ONLY PREVIEW`; absent for ordinary editable files |
| Size | File size in MB, shown for the large-file modes only |
| `chars` | Document length in the backend's own units — UTF-16 code units for editable documents, bytes for byte-backed mapped files |
| Zoom | Current zoom percentage |
| `FOLLOW TAIL` | Appended while Follow Tail is active |

## Encodings and Line Endings

On open NativePad detects UTF-8 with BOM, UTF-16 LE, UTF-16 BE, UTF-8 without
BOM, and falls back to the system ANSI code page when UTF-8 decoding fails.
Files with the `.nfo` extension additionally use OEM code page 437 when their
bytes look like legacy DOS NFO artwork or are not valid UTF-8.

**Save** writes the file back in the encoding it was opened with. **Save As**
lets you choose a different one, including **OEM 437 (NFO)**; saving as ANSI or
OEM 437 is refused rather than truncated if the text contains characters that
the selected code page cannot represent.

Line endings work the same way. A file that was consistently CRLF, LF, or CR
is normalized back to that style on save. A file that was already mixed is left
mixed.

## Follow Tail

`F6` (or **View > Follow Tail**) polls the open file once a second and keeps the
view pinned to the end as new content arrives. The title bar shows `[Tail]` and
the status bar shows `FOLLOW TAIL`.

- Editing commands and Save are disabled while following, so your edits cannot
  race the external writer.
- Mapped large files refresh incrementally — a multi-gigabyte log is never
  rescanned from the start.
- Log rotation by delete-and-rename is detected and reloads the new file.
- Opening a different file or **File > New** turns Follow Tail off.

Independently of Follow Tail, NativePad re-checks the file whenever its window
is activated and offers to reload if it changed on disk, warning first when
unsaved edits would be lost.

## Crash Recovery

While a document has unsaved changes, NativePad journals a snapshot to
`%LOCALAPPDATA%\NativePad\Recovery` at most once every three seconds. The
journal is deleted on save, on opening another file, on **File > New**, and on
a normal exit — so a journal that survives means the process died.

The next launch offers to restore it. Declining discards it. Two NativePad
instances running at once never claim each other's journals. Read-only and
large-file documents are not journaled, because they hold no unsaved edits.

## Settings

Preferences live in `%LOCALAPPDATA%\NativePad\NativePad.ini` under a single
`[Settings]` section. Delete the file to return to defaults.

| Key | Meaning |
| --- | --- |
| `DarkModeForced`, `DarkMode` | Whether a manual theme override is set, and which theme it selects |
| `WordWrap`, `LineNumbers`, `StatusBarVisible` | View toggles |
| `FontFamily`, `FontSizeTenths`, `FontWeight`, `FontItalic` | Editor font (default: Consolas, 15 DIP) |
| `WindowLeft/Top/Right/Bottom`, `WindowMaximized` | Window placement |
| `MarginLeft/Top/Right/Bottom` | Page Setup margins, in thousandths of an inch |
| `RecentFile0` … `RecentFile7` | Recent files list |
| `CheckForUpdates` | Automatic update checks; off by default |
| `LastUpdateCheckUtc` | Rate-limits the automatic check |
| `UpdateUrl` | Release feed; defaults to the NativePad GitHub releases API |

If the window's saved monitor is gone at launch, NativePad places the window on
the primary monitor instead of restoring it off-screen.

## Updates

Automatic update checks are **off** by default. Enable them with **Check
automatically** in the About dialog, or run a one-off check with **Check for
Updates**.

A check reads the latest GitHub release, compares its tag against the running
executable's file version, and — if newer — downloads the
`NativePadSetup-*-win-x64.exe` asset to `%LOCALAPPDATA%\NativePad\Updates`. The
download is verified against the release asset's SHA-256 digest when GitHub
publishes one. NativePad prompts about unsaved work before launching the
installer elevated.
