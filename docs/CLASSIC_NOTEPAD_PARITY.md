# Classic Notepad Feature Parity Plan

NativePad targets feature parity with the last classic, menu-driven Windows Notepad experience rather than the newer tabbed Store Notepad. The compatibility goal is a fast native editor that behaves predictably for users who know classic Notepad, while keeping NativePad's performance work for large files and modern dark mode.

## Current NativePad baseline

- Native C++20 Win32 executable with common file dialogs.
- Dark/light window frame, DPI-aware menu strip, status bar, and DirectWrite editor.
- New, Open, Save, Save As, Exit, drag-and-drop open, and command-line file open.
- Classic File, Edit, Format, View, and Help menu layout with command IDs and accelerators.
- Undo, redo, cut, copy, paste, delete, select all, drag selection,
  double-click word selection, triple-click line selection, caret movement, and
  scrolling.
- Find, Find Next, Find Previous, Replace, Go To, Time/Date, Word Wrap, Font,
  Line Numbers, Page Setup, Print, Status Bar toggle, editor right-click context
  menu, and About.
- UTF-8, UTF-8 BOM, UTF-16 LE/BE, and ANSI fallback loading; Save preserves
  detected encoding and line endings where representable.
- Memory-mapped normal file loading and a read-only mapped backend for files above the editable limit.
- Remaining gaps are the explicit Save As encoding picker, editable large-file
  storage, and broader print fidelity validation against classic Notepad.

## Phase 1: Core classic command parity

Goal: make the menus and shortcut surface recognizable as classic Notepad.

- File: add Page Setup and Print using the native Win32 common dialogs.
- Edit: add Delete, Find, Find Next, Find Previous, Replace, Go To, and Time/Date.
- Format: add Word Wrap and Font.
- View: add Status Bar toggle and preserve Dark Mode as a NativePad extension.
- Help: add About NativePad.
- Keyboard: match classic accelerators where practical, including F3, Shift+F3, Ctrl+F, Ctrl+H, Ctrl+G, F5, Del, Ctrl+P, and Alt+F4.

Acceptance:

- Every visible classic Notepad menu item exists and either works or is intentionally disabled only when classic Notepad would disable it.
- Shortcuts route through the same command handlers as menu clicks.
- Commands update enabled/disabled state when selection, read-only document state, or word wrap status changes.

## Phase 2: Text behavior fidelity

Goal: reduce surprises when opening, editing, and saving ordinary text files.

- Done: preserve detected line endings when saving unless the file was mixed-line
  content or the document was newly created.
- Done: preserve encoding choice where possible, with explicit Save As encoding
  UI still pending.
- Detect UTF-8 with BOM, UTF-8 without BOM, UTF-16 LE, UTF-16 BE, and ANSI consistently.
- Keep dirty-state prompts aligned with classic Notepad for New, Open, Exit, drag/drop, and command-line open.
- Implement Go To behavior that is disabled while Word Wrap is enabled, matching classic Notepad behavior.
- Keep status bar line/column updates correct with CRLF, LF, long lines, selection, and horizontal scrolling.

Acceptance:

- Round-trip smoke files keep expected line endings and encoding.
- Dirty prompt decisions are covered by manual tests and small unit tests where possible.
- Go To lands on the expected line for CRLF, LF, and mixed-line sample files.

## Phase 3: Find and replace quality

Goal: match classic Notepad search workflows while staying responsive on larger files.

- Implement Find and Replace modeless dialogs.
- Support match case and wrap-around search.
- Support Find Next and Find Previous from caret/selection.
- Highlight the active match through the existing editor selection path.
- Keep search incremental enough that large documents do not freeze the UI.

Acceptance:

- Find/replace works across line breaks, document boundaries, and repeated matches.
- Replace All reports the replacement count.
- Esc closes dialogs without losing the main editor selection.

## Phase 4: Print and page setup

Goal: provide the classic print workflow using Windows native APIs.

- Use Page Setup and Print common dialogs.
- Render text through DirectWrite/GDI print-friendly pagination.
- Respect selected font, margins, page orientation, and word wrap.
- Add document title/page header handling only if matching the target classic Notepad behavior requires it.

Acceptance:

- Print preview is not required for classic parity, but printed output must paginate correctly.
- Page Setup settings persist between launches with the rest of the user
  preferences.
- Printing a long wrapped file produces stable line and page breaks.

## Phase 5: Preferences and persistence

Goal: keep user choices between launches without adding unnecessary UI.

- Persist dark mode override, word wrap, line numbers, status bar visibility,
  window placement, and font.
- Let system dark mode remain the default when no explicit NativePad preference exists.
- Keep settings in a simple per-user file or registry location with versioned schema.
- Current implementation stores preferences under `HKCU\Software\NativePad`.

Acceptance:

- Relaunch restores the same visual/editor preferences.
- Bad settings data falls back cleanly to defaults.

## Phase 6: Performance beyond classic Notepad

Goal: keep NativePad materially faster and more robust than classic Notepad for large text files.

- Keep memory-mapped load for editable files.
- Keep read-only mapped viewing for files over the editable limit and make that state impossible to modify or save over the source file.
- Keep scalable mapped find behavior for very large documents and expand it only with measurable latency targets.
- Add instrumentation for cold startup, load time, first paint, find latency, and mapped large-file open/index time.

Acceptance:

- Startup remains effectively instant on typical text files.
- Very large files open into read-only mapped viewing without allocating a full decoded copy.
- Large-file commands never silently truncate or overwrite the source.

## Suggested implementation order

1. Done: add missing menus, command IDs, accelerators, and disabled-state plumbing.
2. Done: implement Find/Find Next/Find Previous, validating editor selection, caret, and modeless dialog ownership.
3. Done: add Replace, Go To, and the editor right-click context menu.
4. Done: add Word Wrap and Status Bar toggle, with Go To disabled while wrapping is enabled.
5. Done: add Font dialog and editor rendering updates.
6. Done: add Page Setup and Print, with print rendering/spooling on a worker thread.
7. Done: add line-ending and encoding preservation.
8. Done: add persisted preferences for the main visual/editor choices.
9. Done: add optional persisted line numbers as a NativePad view extension.
10. Expand smoke tests and manual parity checklist.

## Manual parity checklist

- File: New, Open, Save, Save As, Page Setup, Print, Exit.
- Edit: Undo, Cut, Copy, Paste, Delete, Find, Find Next, Find Previous, Replace, Go To, Select All, Time/Date.
- Format: Word Wrap, Font.
- View: Line Numbers, Status Bar.
- Help: About.
- Behavior: dirty prompts, command-line open, drag/drop open, dark/light mode,
  read-only mapped viewing, encoding load/save, mouse selection, line/column
  status, keyboard shortcuts.
