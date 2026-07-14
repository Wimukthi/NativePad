# Large Files

NativePad has separate paths for normal editable files and oversized files.

## Editable Limit

`kReadChunkLimit` in `src/FileCodec.h` is currently:

```text
512 MB
```

Files at or below this limit are loaded into the editable UTF-16 document model.
Files above this limit open through the read-only mapped backend and can be
switched into the editable large-file backend on demand (see below).

## Normal Editable Path

The normal open path:

1. Opens the file with Win32 file APIs.
2. Reads or maps the bytes.
3. Detects UTF-8 BOM, UTF-16 LE, UTF-16 BE, UTF-8, or ANSI fallback.
4. Decodes the file into UTF-16.
5. Stores text in `DocumentBuffer`.
6. Builds `LineIndex` for navigation and scrolling.

This path supports editing, undo/redo, replace, save, and print.

## Mapped Large-File Path

The large-file path:

1. Checks file size before normal decoding.
2. Opens the file through `MappedTextDocument`.
3. Uses `CreateFileMappingW` and `MapViewOfFile`.
4. Detects BOM-based encoding.
5. Builds a line-start table by scanning mapped data.
6. Serves visible text ranges to `EditorView` on demand.

The OS pages file data in as needed. NativePad does not allocate a full decoded
copy of the file.

## Read-Only Behavior

Mapped large files open read-only so that the fast viewing path and Follow Tail
stay efficient. To make changes, choose **Edit > Enable Large-File Editing**,
which reopens the same file through the editable large-file backend.

While a large file is in the read-only mapped view, disabled commands include:

- Save.
- Save As.
- Replace.
- Replace All.
- Print.
- Typing and paste.
- Cut and delete.

Allowed commands include:

- Scroll.
- Select.
- Copy.
- Find.
- Find Next/Previous.
- Go To, including while Word Wrap is on.

## Editable Large-File Path

`Edit > Enable Large-File Editing` reopens the current large file through
`LargeTextDocument`, a piece table over the memory-mapped original:

1. The original file stays memory-mapped and read-only; it is never decoded in
   full.
2. Content is a sequence of pieces referencing either the mapped original or an
   append-only in-memory add buffer that holds inserted text.
3. Each source keeps a sorted newline index, and pieces carry newline counts, so
   line/offset queries stay fast without rescanning large spans.
4. Editing manipulates piece descriptors only, so memory use scales with the
   number of edits rather than the file size.

Saving streams the pieces in order to a staging file in the file's encoding,
unmaps the original, atomically replaces it, and reopens from disk. This
transiently uses extra disk space equal to the file size.

Current constraints of the editable large-file path:

- Printing and background crash-recovery journaling are disabled.
- Save As writes in the document's existing encoding; the encoding picker does
  not re-encode a large file.
- UTF-8 edits snap to code-point boundaries so multibyte characters are never
  split, but caret navigation remains byte-based (not grapheme-aware).
- While editing is enabled, Follow Tail reloads the whole file on change rather
  than refreshing incrementally, so the read-only view is preferred for tailing.

## Encoding and Coordinates

UTF-16 mapped files use UTF-16 code-unit offsets.

UTF-8 and ANSI mapped files use byte offsets. This is intentional for current
performance and memory use. It is exact for ASCII-heavy logs, which are the
primary large-file target today.

Consequences:

- Column positions in byte-backed mapped files are byte columns.
- Non-ASCII text still renders by decoding visible ranges, but caret movement is
  not grapheme-aware.
- Future editable large-file work should revisit coordinate handling.

## Search

Mapped find scans the mapped file directly.

- UTF-16 search compares wide characters.
- Byte-backed search converts the needle to bytes and scans bytes.
- Case-insensitive byte search is ASCII-oriented for speed and predictability.

## Future Work

- Incremental Follow Tail refresh for the editable large-file backend.
- Async/cancellable line indexing for extremely slow storage.
- Better Unicode-aware byte-backed navigation.
- Encoding conversion on Save As for large files.
- Large-file performance instrumentation.
