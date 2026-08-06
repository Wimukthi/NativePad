# Large Files

NativePad routes a file down one of two paths at open time, based only on its
size. The threshold is `kReadChunkLimit` in `src/FileCodec.h`:

```text
512 MB
```

At or below the limit the file is decoded into memory and fully editable. Above
it the file is memory-mapped and read-only, with editing available on request.

![A 588 MB log open through the read-only mapped backend](images/large-file-view.png)

## Normal Editable Path

1. Open the file with Win32 file APIs.
2. Read or map the bytes.
3. Detect UTF-8 BOM, UTF-16 LE, UTF-16 BE, UTF-8, ANSI fallback, or OEM 437
   for legacy `.nfo` artwork.
4. Decode to UTF-16.
5. Store the text in `DocumentBuffer`.
6. Build a `LineIndex` for navigation and scrolling.

This path supports everything: editing, undo/redo, replace, save, and print.

## Mapped Large-File Path

1. Check the file size before any decoding.
2. Open through `MappedTextDocument`.
3. Map with `CreateFileMappingW` and `MapViewOfFile`.
4. Detect BOM-based encoding, with the same OEM 437 `.nfo` heuristic used by
   the editable path.
5. Build a line-start table by scanning the mapped bytes.
6. Serve visible ranges to `EditorView` on demand.

The OS pages file data in as it is touched. NativePad never allocates a decoded
copy of the file, which is what keeps a multi-gigabyte log responsive.

The status bar shows `READ-ONLY MAPPED` with the file size. If the mapping
itself fails, NativePad falls back to decoding a leading chunk and shows
`READ-ONLY PREVIEW` with the decoded and total sizes, so a partial view is never
mistaken for the whole file.

### What the read-only view allows

| Available | Disabled |
| --- | --- |
| Scroll, select, copy | Save, Save As |
| Find, Find Next/Previous | Replace, Replace All |
| Go To, including under Word Wrap | Typing, paste, cut, delete |
| Select All | Print |

Read-only is the default because it is what makes viewing and Follow Tail fast.
Nothing in this mode can modify or overwrite the source file.

## Editable Large-File Path

**Edit > Enable Large-File Editing** reopens the current large file through
`LargeTextDocument`, a piece table layered over the memory-mapped original:

1. The original file stays mapped and read-only; it is never decoded in full.
2. Content is a sequence of pieces referencing either the mapped original or an
   append-only in-memory add buffer holding inserted text.
3. Each source keeps a sorted newline index, and every piece carries a newline
   count, so line and offset queries stay fast without rescanning large spans.
4. Editing manipulates piece descriptors only, so memory use scales with the
   number of edits rather than with the file size.

Saving streams the pieces in order into a staging file in the document's
encoding, unmaps the original, replaces it atomically, and reopens from disk.
An interrupted save therefore never leaves a truncated file — but it does
transiently need free disk space equal to the file size.

The status bar shows `LARGE FILE` in this mode.

### Current constraints

- Printing and background crash-recovery journaling are disabled.
- Save As writes in the document's existing encoding; the encoding picker does
  not re-encode a large file.
- UTF-8 edits snap to code-point boundaries so multibyte characters are never
  split, but caret navigation remains byte-based rather than grapheme-aware.
- Follow Tail reloads the whole file on change instead of refreshing
  incrementally, so the read-only view is the better choice for tailing.

## Coordinates

| File | Offsets |
| --- | --- |
| Editable documents | UTF-16 code units |
| Mapped UTF-16 files | UTF-16 code units |
| Mapped UTF-8 / ANSI / OEM 437 files | Bytes |
| Editable large files | Bytes |

Byte offsets are a deliberate trade. They are exact for the ASCII-heavy logs
that are the primary large-file target, and they avoid building a decoded index
over a file that may not fit in memory. The consequences:

- Reported column positions in byte-backed files are byte columns.
- Non-ASCII text still renders correctly — visible ranges are decoded when
  painted — but caret movement is not grapheme-aware.

## Search

Mapped find scans the mapped bytes directly rather than materializing text.

- UTF-16 search compares wide characters.
- Byte-backed search converts the needle to bytes and scans bytes.
- Case-insensitive byte search is ASCII-oriented, for speed and predictability.

## Future Work

- Incremental Follow Tail refresh for the editable large-file backend.
- Async, cancellable line indexing for very slow storage.
- Grapheme-aware navigation over byte-backed content.
- Encoding conversion on Save As for large files.
- Instrumentation for open, index, and find latency.
