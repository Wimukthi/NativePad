# Usage

NativePad opens files passed on the command line, dropped onto its window, or
chosen from **File > Open**. Every supplied file gets a tab. With no argument it
restores the previous tab session, or starts with `Untitled` when no session
exists.

```powershell
NativePad.exe "C:\logs\server.log" "C:\logs\errors.log"
```

## Menus

### File

| Command | Shortcut | Notes |
| --- | --- | --- |
| New Tab | `Ctrl+N` | Opens an empty tab without disturbing existing work |
| Open | `Ctrl+O` | Opens a tab; reuses a clean empty tab and activates a file that is already open |
| Save | `Ctrl+S` | Keeps the encoding and line endings detected on open |
| Save As | `Ctrl+Shift+S` | Encoding picker: UTF-8, UTF-8 BOM, UTF-16 LE, UTF-16 BE, ANSI, OEM 437 (NFO) |
| Close Tab | `Ctrl+W` | Prompts only when that tab has unsaved changes |
| Recent files | — | Up to eight entries, most recent first; missing files are dropped when opened |
| Clear Recent Files | — | Empties the list |
| Page Setup | — | Native dialog; margins persist between sessions |
| Print | `Ctrl+P` | Native dialog; pagination and spooling run off the UI thread |
| Exit | `Alt+F4` | Remembers the complete tab session and closes without save prompts |

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
| Tab Bar | — | Hides the tab strip for a classic Notepad layout; the setting persists |
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

## Tabs

The tab strip uses one shared editor surface, so opening more tabs does not
create more editor windows or DirectWrite/Direct2D render targets. Each tab
still retains its own document backend, undo/redo history, caret, selection,
scroll position, encoding, dirty state, Follow Tail mode, and recovery journal.

The selected tab uses a contrasting rounded surface and accent line. The
compact 30-DIP strip leaves more room for text, and the `+` button sits directly
after the visible tabs. A close button remains visible on the selected tab and
appears on inactive tabs when hovered; the surfaces and controls use
antialiased, DPI-scaled rendering.

| Action | Shortcut |
| --- | --- |
| New tab | `Ctrl+N` or the `+` button |
| Close tab | `Ctrl+W`, its close button, or middle-click |
| Next tab | `Ctrl+Tab` or `Ctrl+PageDown` |
| Previous tab | `Ctrl+Shift+Tab` or `Ctrl+PageUp` |

When the strip cannot show every tab at its minimum width, arrow buttons scroll
the visible tab range. Hover a tab to see its full path, or hover the `+` button
to see its `Ctrl+N` shortcut. Closing the final tab creates a fresh `Untitled`
tab; repeating that action does not advance the title to `Untitled 2`,
`Untitled 3`, and so on.

Clear **View > Tab Bar** to place the editor directly below the menu bar for a
classic Notepad appearance. Hiding the bar does not discard documents: tab
switching shortcuts, session restore, and other tab commands continue to work.

Closing a tab is an explicit discard action, so a dirty tab asks whether to
save first. Closing the window is different: NativePad stores the workspace and
restores it on the next launch without asking about each tab.

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
- Switching tabs suspends polling without turning Follow Tail off. Returning to
  the tab catches up immediately and resumes its one-second polling timer.

Independently of Follow Tail, NativePad re-checks the file whenever its window
is activated and offers to reload if it changed on disk, warning first when
unsaved edits would be lost.

## Session Restore

On a normal exit, NativePad stores the tab order, active tab, paths, formats,
Follow Tail state, and unsaved content under
`%LOCALAPPDATA%\NativePad\Session`. Dirty and untitled tabs are snapshotted;
clean file-backed tabs reopen from disk. Edited large-file tabs use an on-disk
snapshot so they do not have to be decoded into memory.

The next launch restores that workspace automatically. **Close Tab** remains
intentional: it prompts for a dirty tab, and a tab that you close is not part of
the next saved session. If the session cannot be written, NativePad reports the
error and remains open rather than discarding the workspace.

## Crash Recovery

While a tab has unsaved changes, NativePad journals a snapshot to
`%LOCALAPPDATA%\NativePad\Recovery` at most once every three seconds. The
journal is deleted when that tab is saved or intentionally closed, and all
journals are deleted on a normal exit. Switching tabs does not touch another
tab's recovery data, so a journal that survives means the process died.

The next launch offers to restore every abandoned journal, one tab per accepted
snapshot. Declining discards it. Two NativePad instances running at once never
claim each other's journals. Normal-exit session restore and crash recovery are
separate: mapped, read-only, and editable-large documents are not crash
journaled.

## Settings

Preferences live in `%LOCALAPPDATA%\NativePad\NativePad.ini` under a single
`[Settings]` section. Delete the file to return to defaults.

| Key | Meaning |
| --- | --- |
| `DarkModeForced`, `DarkMode` | Whether a manual theme override is set, and which theme it selects |
| `WordWrap`, `LineNumbers`, `StatusBarVisible`, `TabBarVisible` | View toggles |
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
publishes one. NativePad stores the current tab session before launching the
installer elevated.
