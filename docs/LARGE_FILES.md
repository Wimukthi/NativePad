# Large Files

NativePad has separate paths for normal editable files and oversized files.

## Editable Limit

`kReadChunkLimit` in `src/main.cpp` is currently:

```text
512 MB
```

Files at or below this limit are loaded into the editable UTF-16 document model.
Files above this limit open through the read-only mapped backend.

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

Mapped large files are read-only by design.

Disabled commands include:

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
- Go To when Word Wrap is off.

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

- Editable large-file storage.
- Async/cancellable line indexing for extremely slow storage.
- Better Unicode-aware byte-backed navigation.
- Large-file performance instrumentation.
