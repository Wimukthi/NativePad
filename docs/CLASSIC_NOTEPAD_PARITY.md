# Classic Notepad Parity

NativePad targets the last classic, menu-driven Windows Notepad — not the newer
tabbed Store app. The goal is that someone who knows classic Notepad can use
NativePad without relearning anything, while getting large-file handling and
dark mode that classic Notepad never had.

This page records where NativePad matches, where it deliberately differs, and
what remains open.

## Command Parity

Every classic Notepad menu command exists in NativePad with the same
accelerator.

| Classic Notepad | NativePad |
| --- | --- |
| File: New, Open, Save, Save As, Page Setup, Print, Exit | Match |
| Edit: Undo, Cut, Copy, Paste, Delete, Find, Find Next, Replace, Go To, Select All, Time/Date | Match |
| Format: Word Wrap, Font | Match, with a custom theme-aware Font dialog instead of `ChooseFont` |
| View: Status Bar | Match |
| Help: About | Match. **View Help** is not implemented — there is no bundled help file |
| Accelerators: `Ctrl+N/O/S/P/Z/X/C/V/F/H/G/A`, `F3`, `Shift+F3`, `F5`, `Del`, `Alt+F4` | Match |

Menu commands and accelerators route through the same handlers, and enabled or
disabled state updates with selection and read-only document state.

## Deliberate Differences

| Behavior | Classic Notepad | NativePad | Why |
| --- | --- | --- | --- |
| Go To under Word Wrap | Disabled | Available | Matches modern Notepad; the line index is valid either way |
| Undo | Single level, no Redo | Multi-level, with Redo on `Ctrl+Y` | Single-level undo is a limitation, not a behavior worth reproducing |
| Find Previous | Direction radio only | Also `Shift+F3` | Faster, and matches modern Notepad |
| Font dialog | `ChooseFont` common dialog | Custom dialog | The common dialog cannot be themed for dark mode |
| Message prompts | System message boxes | Custom `MessageDialog` | System boxes ignore the app's dark theme |
| Very large files | Loads, or fails, slowly | Read-only memory-mapped view | Never silently truncates, never blocks on a full decode |
| Settings storage | Registry | `%LOCALAPPDATA%\NativePad\NativePad.ini` | Portable; a ZIP install leaves no registry footprint |

## NativePad Additions

Beyond classic Notepad, aligned with modern Notepad where an equivalent exists:

- Dark mode with a manual override, plus Windows High Contrast support.
- Per-monitor v2 DPI awareness.
- Line numbers as an optional editor gutter.
- Zoom (`Ctrl`+wheel, `Ctrl+Plus/Minus`, `Ctrl+0`), 10%–500%.
- Recent files in the File menu.
- Duplicate Line, Delete Line, and Move Line Up/Down.
- Follow Tail (`F6`) for logs that are still being written, and a reload prompt
  when the open file changes on disk.
- Crash recovery for unsaved work.
- Read-only memory-mapped viewing above 512 MB, and opt-in editing of those
  files through a piece table over the mapping.
- Save As encoding picker, and an update check against GitHub releases.

## Text Behavior

- Encoding detection covers UTF-8 with BOM, UTF-8 without BOM, UTF-16 LE,
  UTF-16 BE, and ANSI fallback.
- Save preserves the detected encoding; Save As can change it, and refuses an
  ANSI save that would lose characters rather than writing a truncated file.
- Line endings are normalized back to the style the file was opened with.
  Files that were already mixed stay mixed.
- Dirty-state prompts match classic Notepad for New, Open, Exit, drag-and-drop
  open, and command-line open.
- Status bar line and column stay correct across CRLF, LF, long lines,
  selection, and horizontal scrolling.

## Open Items

- Print fidelity — wrapped and unwrapped pagination has not been compared
  against classic Notepad output at the level of detail the rest of the parity
  work has received.
- Printing and encoding conversion are unavailable for editable large files.
- No `Ctrl+Backspace` word delete (a modern-Notepad convenience, absent from
  classic Notepad).
- Startup, load, first-paint, find, and large-file index timings are not yet
  instrumented, so performance claims rest on manual observation.

## Manual Parity Check

Walk this list when validating a release:

- **File:** New, Open, Save, Save As, Recent Files, Page Setup, Print, Exit.
- **Edit:** Undo, Cut, Copy, Paste, Delete, Find, Find Next, Find Previous,
  Replace, Go To, Select All, Time/Date, Duplicate/Delete Line, Move Line
  Up/Down.
- **Format:** Word Wrap, Font.
- **View:** Zoom In/Out/Restore, Line Numbers, Status Bar, Dark Mode, Follow
  Tail.
- **Help:** Set as Default Text Editor, About.
- **Behavior:** dirty prompts, command-line open, drag-and-drop open, dark and
  light mode, read-only mapped viewing, encoding load/save and Save As encoding
  changes, mouse selection, line/column status, keyboard shortcuts.

The full pre-release pass is [Release Checklist](RELEASE_CHECKLIST.md).
