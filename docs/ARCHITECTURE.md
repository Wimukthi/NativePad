# Architecture

NativePad is a single-process C++20 Win32 application. It deliberately avoids a
widget framework so startup cost, native control behavior, custom painting, and
large-file handling all stay explicit and inspectable.

The one external component is
[`Wimukthi.Win32Theme`](https://github.com/Wimukthi/Wimukthi.Win32Theme), a
sibling repository that supplies the reusable Windows dark-mode and High
Contrast integration layer.

## Module Map

| Module | Files | Responsibility |
| --- | --- | --- |
| Application shell | `main.cpp` | `AppWindow`: menu/tab/status chrome, command routing, active-session binding, preference load/save |
| Document session | `DocumentSession.h` | Per-tab backend, path/format metadata, dirty state, editor state, file stamp, Follow Tail, and recovery journal |
| Tab strip | `TabStrip.*` | One custom-painted HWND for tab titles, close/new controls, overflow scrolling, hit testing, and tooltips |
| Editor control | `EditorView.*` | Shared DirectWrite rendering surface, input, clipboard, and movable per-tab interaction state |
| Editable document | `DocumentBuffer.*` | Piece table over decoded UTF-16 text |
| Line index | `LineIndex.*` | Logical line starts for editable documents, updated incrementally |
| Mapped document | `MappedTextDocument.*` | Read-only memory-mapped text access, line indexing, and find |
| Editable large document | `LargeTextDocument.*` | Piece table over the memory-mapped original plus an in-memory add buffer |
| File codec | `FileCodec.*` | Encoding detection, read/write, large-file preview, open/save pickers |
| Text format helpers | `TextFormat.*` | Encoding labels, line-ending detection and normalization, save encoding |
| Text file catalog | `TextFileTypes.*` | Canonical plain-text extensions, dialog filters, default extensions, and per-extension syntax-language selection |
| Syntax highlighter | `SyntaxHighlighter.*` | Line-local JSON, INI, Markdown, and XML tokenization for visible editor rows |
| Shared UI support | `UiSupport.*` | NativePad palettes, DPI scaling, owner-draw helpers |
| Popup menus | `PopupMenu.*` | Owner-drawn menu and context-menu windows, plus the drop shadow |
| Dialogs | `FontDialog.*`, `FindReplaceDialog.*`, `GoToDialog.*`, `AboutDialog.*`, `MessageDialog.*` | Custom theme-aware modal and modeless dialogs |
| Printing | `Printing.*` | Pagination and spooling worker |
| Settings | `Settings.*` | INI read/write under `%LOCALAPPDATA%\NativePad\NativePad.ini` |
| Session restore | `SessionStore.*` | Versioned normal-exit manifest and content snapshots under `%LOCALAPPDATA%\NativePad\Session` |
| Crash recovery | `RecoveryJournal.*` | Journaling of unsaved documents and recovery of journals abandoned by a crashed process |
| Update checker | `UpdateChecker.*` | GitHub release discovery, installer download, SHA-256 verification |
| Default editor | `DefaultEditor.*` | Per-user plain-text associations and the Windows Default Apps hand-off |
| Resources | `NativePad.rc`, `resource.h`, `app.manifest` | Version metadata, command IDs, visual styles, DPI awareness |
| Theme framework | sibling `Wimukthi.Win32Theme` | Process and per-window dark mode, native control theming, system-theme changes, High Contrast fallback |
| Tests | `tests/*.cpp` | Dependency-free console test runner |

All paths are relative to `src/` unless noted.

Each dialog and feature area is its own translation unit exposing a minimal
header — `ShowFontDialog`, `ShowGoToLineDialog`, `StartPrintWorker`, and so on.
`main.cpp` keeps only `AppWindow`, `wWinMain`, and shell-level helpers.
Cross-cutting helpers (DPI scaling, palette selection, control styling) belong
in `UiSupport`; generic Windows theming belongs in `Wimukthi.Win32Theme`.

## Ownership

`AppWindow` owns the top-level HWND, one menu strip, one `TabStrip`, one
`EditorView`, the status bar, dialogs, commands, and a stable collection of
`DocumentSession` objects. A tab id, rather than a vector index, is used by UI
notifications so closing or reordering sessions cannot redirect an action.

Each `DocumentSession` owns one document backend and all file-specific state.
Only the active session is attached to `EditorView`. Switching tabs moves an
`EditorViewState` (line index, undo/redo stacks, caret, selection, and scroll
positions) out of the shared editor and moves the destination state in. The
document text is never copied and the normal-document line index is not rebuilt
on a tab switch.

`EditorView` owns the sole child editor HWND, Direct2D/DirectWrite resources,
the caret blink timer, and active-only visual caches. Font, word wrap, line
numbers, theme, and zoom remain window-global, so inactive tabs allocate no
render targets or UI controls.

The split keeps Win32 command handling out of the editor and editor painting
out of the shell.

## Session Persistence

Normal exit and explicit tab closure have different semantics. `WM_CLOSE`
serializes one versioned `SessionStore` manifest for the current tab order and
active tab, then clears crash journals and destroys the window. It does not
walk dirty tabs with save prompts. **Close Tab** still calls the normal
save/discard/cancel path and removes that tab from the in-memory workspace.

Clean file-backed tabs store metadata only and reopen from their paths. Dirty
or untitled `DocumentBuffer` tabs store exact UTF-16 snapshots. Dirty
`LargeTextDocument` tabs stream their current bytes to a snapshot through
`SaveTo`, avoiding a full decoded copy. The manifest is staged and atomically
replaced only after every required snapshot succeeds; a failure leaves the
window open.

Startup consumes the manifest after validating all records and loading normal
snapshots. This prevents an explicitly closed tab from returning after a later
crash in the new process. Large snapshots remain mapped until their tabs are
saved, explicitly closed, or snapshotted on the next normal exit.

## Document Backends

`EditorView` can point at exactly one of three backends:

| Field | Backend | Coordinates | Editable |
| --- | --- | --- | --- |
| `document` | `DocumentBuffer` | UTF-16 code units | Yes |
| `mappedDocument` | `MappedTextDocument` | UTF-16 code units, or bytes for UTF-8/ANSI/OEM 437 | No |
| `largeDocument` | `LargeTextDocument` | Bytes | Yes |

Ordinary files are decoded into UTF-16 and stored in `DocumentBuffer`, which
supports editing, undo/redo, replace, save, and print. The open path records
the detected encoding and line-ending policy so save can preserve both. NFO
paths use OEM code page 437 for legacy DOS artwork when the content is not
valid UTF-8 (or contains strong CP437 box-drawing signals), and that encoding
is carried through the editable and large-file backends for round-trip saves.

Files above `kReadChunkLimit` (512 MB, in `FileCodec.h`) open through
`MappedTextDocument`, which maps the file, builds a line-start table, and serves
visible ranges on demand. **Edit > Enable Large-File Editing** reopens the same
file through `LargeTextDocument`; the read-only view stays the default so the
fast viewing and Follow Tail paths are unaffected. See
[Large Files](LARGE_FILES.md).

Because backends differ in coordinate units, every edit funnels through
`EditorView::BackendReplace`, which records undo/redo in the backend's own units
rather than assuming UTF-16. Paint, hit-test, caret, selection, and scroll code
goes through backend-neutral helpers such as `DocumentLength`,
`DocumentTextRange`, `IndexedLineCount`, and `IndexedMaxLineLength`.

Byte coordinates are deliberate: they keep large ASCII logs fast and
memory-efficient, at the cost of caret navigation that is not grapheme-aware.
UTF-8 edits do snap to code-point boundaries, so multibyte characters are never
split.

## Rendering

The single shared editor draws into one Direct2D render target and measures text
with DirectWrite, painting only the active tab's visible rows. The tab strip is
one double-buffered child window with a small Direct2D overlay for antialiased
controls; it does not create a child HWND per tab.

`TextFileTypes` maps a recognized extension to an optional `SyntaxLanguage`.
`AppWindow` updates that language whenever a normal document is opened, saved,
restored, or reset. `EditorView` keeps the highlighter line-local: it tokenizes
only visible logical lines and applies DirectWrite drawing effects to those
ranges. Plain text, mapped large files, and editable large files stay on the
single-brush path, so the large-file backends do not acquire a whole-document
parser or cache.

The caret is custom-drawn and blinked on a timer using the system caret blink
interval, because the editor does not use the Win32 caret APIs.

Mouse selection uses capture. When a captured pointer leaves the editor,
`EditorView` runs a bounded 50 ms autoscroll timer and extends the selection at
the nearest viewport edge. The same visible-row calculation clamps wheel,
scrollbar, caret, and resize paths so documents cannot scroll past their final
complete page.

Line numbers are an editor gutter, not document text, so save, copy, search,
replace, and print never see them. With word wrap on, only logical line starts
are numbered; wrapped continuation rows stay blank.

Word wrap uses a lazy visual-row prefix cache. Logical lines come from
`LineIndex` or `MappedTextDocument`; visual row counts depend on wrap width and
font metrics, so the cache is invalidated when the font, DPI, window width, or
word-wrap state changes.

## Threading

Nearly all work stays on the UI thread. There are two exceptions:

- **Printing.** The native Print dialog runs on the UI thread; once the printer
  DC is returned, pagination and spooling move to a worker thread. Completion
  posts `WM_NATIVEPAD_PRINT_COMPLETE`.
- **Updates.** Update checks and installer downloads run on worker threads
  through WinHTTP, posting `WM_NATIVEPAD_UPDATE_CHECK_COMPLETE` and
  `WM_NATIVEPAD_UPDATE_DOWNLOAD_COMPLETE`.

Worker threads must never touch HWND-owned UI state.

## External Changes and Follow Tail

`AppWindow` records a file stamp — size plus last write time — whenever a
document is opened or saved. On window activation it re-queries the stamp and
offers to reload if the file changed, warning explicitly when unsaved edits
would be lost. Declining records the new stamp so the same change does not
re-prompt on every activation.

**View > Follow Tail** (`F6`) polls the file on a one-second UI timer:

- Mapped large files refresh in place. `MappedTextDocument::Refresh` remaps the
  grown file and extends the line index from the previous end of content, so a
  multi-gigabyte log is never rescanned. Shrinks and same-size rewrites report
  as `Replaced` and trigger a full reload. Log rotation by delete-and-rename is
  caught by comparing the path stamp, because the mapped handle keeps following
  the original file.
- Editable files reload from disk when the stamp changes, and the editor is held
  read-only while following so edits cannot race the external writer.

After each refresh the caret moves to the end of the document. Follow Tail is
per-tab state. Only the active tab is polled; switching away suspends the shared
timer, and returning performs an immediate catch-up refresh before polling
resumes.

Note that while a mapping is held, external writers can append to the file but
cannot truncate it — Windows fails the truncation with
`ERROR_USER_MAPPED_FILE`.

## Crash Recovery

While an editable document is dirty, the shell journals a snapshot to
`%LOCALAPPDATA%\NativePad\Recovery` on a debounced timer, at most once every
three seconds. A journal is a pair of files named after the owning process id
and tab id (`session-<pid>-<tab-id>`): a UTF-16 LE content file, staged and
renamed so it is never half-written, and a metadata file (original path,
encoding, line ending) written last so a journal only becomes discoverable once
it is complete.

Each journal is deleted when its tab is saved, intentionally closed, or the
window exits normally. Switching tabs and opening another file leave unrelated
journals untouched. A journal that survives therefore means the process
crashed.

On startup the shell scans the recovery directory for journals whose owning
process is no longer running and offers every recoverable document. Each
accepted snapshot opens in its own tab and is re-journaled immediately so it
survives a second crash. Mapped, read-only preview, and editable-large documents
are never journaled.

## Update Flow

The update checker reads the latest GitHub release, compares the release tag
against the executable's file version, locates the
`NativePadSetup-*-win-x64.exe` asset, and downloads it to
`%LOCALAPPDATA%\NativePad\Updates`. Downloads are verified against the release
asset's SHA-256 digest when GitHub publishes one.

The UI thread owns every prompt and launches the installer elevated with
`ShellExecuteW("runas")` only after the current tab session has been stored.

Automatic checks are off by default, controlled by `CheckForUpdates` and
rate-limited by `LastUpdateCheckUtc`. The feed endpoint is `UpdateUrl`, which
defaults to
`https://api.github.com/repos/Wimukthi/NativePad/releases/latest`.
