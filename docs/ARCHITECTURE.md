# Architecture

NativePad is a single-process C++20 Win32 application. It avoids framework-level
UI abstractions so startup, native widgets, dark-mode handling, and large-file
behavior remain explicit.

## Main Components

| Component | Files | Responsibility |
| --- | --- | --- |
| Application shell | `src/main.cpp` | `AppWindow` main window: menu strip, status bar, command routing, document/dirty state, preference load/save |
| Shared UI support | `src/UiSupport.h`, `src/UiSupport.cpp` | Theme palettes, DPI scaling, dark-mode framing, and common control helpers used by the shell and every dialog |
| Popup menus | `src/PopupMenu.h`, `src/PopupMenu.cpp` | Owner-drawn dark menu and context-menu windows plus the drop shadow |
| File codec | `src/FileCodec.h`, `src/FileCodec.cpp` | Encoding detection, file read/write, large-file preview, and open/save pickers |
| Printing | `src/Printing.h`, `src/Printing.cpp` | Threaded pagination/spooling worker |
| Dialogs | `src/FontDialog.*`, `src/FindReplaceDialog.*`, `src/GoToDialog.*`, `src/AboutDialog.*` | Custom dark-mode modal/modeless dialogs, each exposing a single entry point |
| Settings | `src/Settings.h`, `src/Settings.cpp` | Registry preference read/write under `HKCU\Software\NativePad` |
| Default editor | `src/DefaultEditor.h`, `src/DefaultEditor.cpp` | Per-user file-association registration for `.txt` and the Windows "Default apps" hand-off |
| Editor control | `src/EditorView.h`, `src/EditorView.cpp` | DirectWrite rendering, caret/selection, scrolling, input, clipboard, undo/redo |
| Editable document | `src/DocumentBuffer.h`, `src/DocumentBuffer.cpp` | Piece-table storage for normal editable files |
| Line index | `src/LineIndex.h`, `src/LineIndex.cpp` | Logical line starts for editable documents |
| Large-file document | `src/MappedTextDocument.h`, `src/MappedTextDocument.cpp` | Read-only memory-mapped text access and line/search indexing |
| Text format helpers | `src/TextFormat.h`, `src/TextFormat.cpp` | Encoding labels, line-ending detection, normalization, and save encoding |
| Resources/manifest | `src/NativePad.rc`, `src/resource.h`, `src/app.manifest` | Version metadata, command IDs, visual styles, DPI awareness |
| Tests | `tests/*.cpp` | Dependency-free executable tests |

The application shell is split into focused translation units rather than one
large `main.cpp`. Each dialog and feature area lives in its own file and exposes
a minimal header (for example, `ShowFontDialog`, `ShowGoToLineDialog`,
`StartPrintWorker`). Cross-cutting Win32 helpers — theming, DPI scaling, and
control styling — are shared through `UiSupport`. `main.cpp` retains only
`AppWindow`, `wWinMain`, and shell-only helpers.

## UI Ownership

`AppWindow` in `main.cpp` owns:

- Top-level HWND lifetime.
- Custom menu strip and popup menus.
- Status bar.
- Dialog creation.
- File/open/save/print commands.
- Dirty state and document metadata.
- Preference load/save under `HKCU\Software\NativePad`.

`EditorView` owns:

- The child editor HWND.
- Direct2D/DirectWrite resources.
- Caret blink timer, selection, scroll state, and word wrap state.
- Visual-only editor options such as line numbers.
- Undo/redo stacks for editable documents.
- Backend-neutral text access through helper methods.

The split keeps Win32 command handling out of the editor while keeping editor
painting and navigation out of the app shell.

## Document Backends

Normal files are decoded into UTF-16 and stored in `DocumentBuffer`. This path is
editable and supports undo/redo, replace, save, print, and normal find.

Open records the detected encoding and line-ending policy. Save preserves that
encoding where possible and normalizes CRLF/LF/CR-only files back to their
detected line ending. Mixed-line files are left mixed.

Large files above `kReadChunkLimit` currently open through `MappedTextDocument`.
That backend is read-only. It maps the file, builds a line-start table, and
serves visible ranges on demand.

`EditorView` can point at either:

- `DocumentBuffer* document_`
- `MappedTextDocument* mappedDocument_`

All paint, hit-test, caret, selection, and scroll code goes through helper
methods such as `DocumentLength`, `DocumentTextRange`, `IndexedLineCount`, and
`IndexedMaxLineLength`.

## Text Coordinates

Editable documents and UTF-16 mapped files use UTF-16 code-unit offsets.

Byte-backed mapped files use byte offsets. This keeps large ASCII/log files
fast and memory-efficient. Visible ranges are decoded when painted.

This matters for future features:

- Selection/caret positions in mapped UTF-8/ANSI files are byte positions.
- Exact Unicode grapheme navigation is not implemented.
- Editable large-file support will need a more complete storage model.

## Rendering

The editor uses Direct2D for the render target and DirectWrite for text metrics
and drawing. It renders only visible rows.

The caret is custom-drawn and blinked with a timer using the system caret blink
interval, because the editor does not use the native Win32 caret APIs.

Line numbers are drawn as an editor gutter. They are not part of document text,
so save, copy, search, replace, and print paths never see them. With word wrap
enabled, only logical line starts receive a number; wrapped continuation rows
stay blank in the gutter.

Word wrap uses a lazy visual-row prefix cache:

- Logical lines come from `LineIndex` or `MappedTextDocument`.
- Visual row counts depend on wrap width and font metrics.
- Changing font, DPI, window width, or word-wrap state invalidates the cache.

## Threading

Most application work stays on the UI thread. Printing is the current exception:

- The native Print dialog runs on the UI thread.
- After the printer DC is returned, pagination/spooling runs on a worker thread.
- Completion is posted back with `WM_NATIVEPAD_PRINT_COMPLETE`.

Do not access HWND-owned UI state from worker threads.
