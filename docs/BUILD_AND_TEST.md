# Build and Test

## Requirements

| Requirement | Notes |
| --- | --- |
| Windows | 64-bit, Windows 10 or later |
| Visual Studio 2026 | Or any MSVC toolchain providing the `v145` toolset |
| Windows 10 SDK | Or newer |
| `Wimukthi.Win32Theme` | A checkout beside NativePad — see below |

The solution builds x64 only, with C++20, the Unicode character set, warnings
at level 4 treated as errors, and per-monitor v2 DPI awareness requested by
`src/app.manifest`.

### Framework layout

`NativePad.vcxproj` imports `Wimukthi.Win32Theme.props`, which contributes the
shared theme facade, the pinned Darkmodelib sources, include paths, preprocessor
definitions, and Windows libraries. The default layout is:

```text
Software\
  NativePad\
  Wimukthi.Win32Theme\
```

If the framework is missing the build fails early with a clear message. Pass
`/p:WimukthiWin32ThemeRoot=<path>` to use a different checkout. The test project
does not depend on the theme framework.

## Build

From a Visual Studio Developer PowerShell, at the repository root:

```powershell
MSBuild.exe .\NativePad.sln /p:Configuration=Release /p:Platform=x64 /m
```

```powershell
MSBuild.exe .\NativePad.sln /p:Configuration=Debug /p:Platform=x64 /m
```

With a framework checkout elsewhere:

```powershell
MSBuild.exe .\NativePad.sln /p:Configuration=Release /p:Platform=x64 /p:WimukthiWin32ThemeRoot=D:\Libraries\Wimukthi.Win32Theme /m
```

If `MSBuild.exe` is not on `PATH`, call it by full path:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" .\NativePad.sln /p:Configuration=Release /p:Platform=x64 /m
```

> **Every app build bumps the version.** Building `NativePad.vcxproj` increments
> the fourth version component in `src\NativePad.rc`, so a successful local
> build leaves a change in your working tree. Add
> `/p:AutoIncrementVersion=false` for a diagnostic build that must not touch the
> version. See [Versioning](VERSIONING.md).

### Outputs

```text
bin\x64\Release\NativePad.exe
bin\x64\Release\NativePad.Tests.exe
bin\x64\Debug\NativePad.exe
bin\x64\Debug\NativePad.Tests.exe
```

Intermediates land in `obj\`. Both directories, along with the Visual Studio
`.vs\` cache, are build artifacts — they are ignored by Git and safe to delete.

## Test

The test runner is a single console executable with no test-framework
dependency, so it builds on a clean Windows machine with nothing extra
installed.

```powershell
.\bin\x64\Release\NativePad.Tests.exe
```

Expected output, exit code `0`:

```text
DocumentBuffer tests passed
LineIndex tests passed
MappedTextDocument tests passed
LargeTextDocument tests passed
RecoveryJournal tests passed
SessionStore tests passed
TextFormat tests passed
SyntaxHighlighter tests passed
TextFileTypes tests passed
```

Any failure prints the failing assertion to stderr and aborts with a non-zero
exit code.

### What is covered

| Suite | Coverage |
| --- | --- |
| `DocumentBuffer` | Piece-table insert, erase, replace, range reads, line counts, and find across piece boundaries |
| `LineIndex` | Incremental line-start updates after edits, without full rebuilds |
| `MappedTextDocument` | UTF-8/byte-backed and UTF-16 line starts, range decoding, find, and refresh — appended content extends the index across the old mapping boundary, in-place rewrites report as replaced |
| `LargeTextDocument` | Piece-table-over-mmap insert/erase/find across UTF-8 and UTF-16 originals, erase snapping to UTF-8 code-point boundaries, line/offset queries, and save round-trips that preserve a BOM |
| `RecoveryJournal` | Journals from dead processes are claimed with exact text and metadata, journals from live processes are left alone, and clearing removes every journal file |
| `SessionStore` | Versioned normal-exit manifests, exact unsaved-text round trips, clean file-backed metadata, large-file snapshots, and consume/clear behavior |
| `TextFormat` | Encoding labels, UTF-8 validation, OEM 437 heuristics, line-ending detection and normalization, and save-encoding byte output |
| `SyntaxHighlighter` | Line-local JSON, INI, Markdown, and XML token spans |
| `TextFileTypes` | Extension lookup, dialog patterns, and default-extension selection |

### What still needs a human

The UI layer has no automated coverage. Verify manually after UI changes:

- Window and dialog painting in dark, light, and Windows High Contrast.
- DPI changes, including dragging across monitors at different scales.
- Print output.
- Common file dialogs and drag-and-drop open.
- Very large real-world files.
- Follow Tail against a live log writer, and the reload-on-activation prompt.

[Release Checklist](RELEASE_CHECKLIST.md) is the full manual pass.

## Continuous Integration

`.github/workflows/ci.yml` builds and tests Debug x64 and Release x64 on every
push and pull request against `main`. It checks out `Wimukthi.Win32Theme`
alongside NativePad so the framework import resolves the same way it does
locally.

The job runs on `windows-2025-vs2026` because the project needs the Visual
Studio 2026 `v145` toolset. If the hosted runner labels change, keep the
workflow and the toolset declared in the projects aligned.

## Release Packaging

`.github/workflows/release-package.yml` is a manual (`workflow_dispatch`)
workflow. It builds Release x64, runs the Release tests, produces
`NativePad-<version>-win-x64.zip`, builds the Inno Setup installer
`NativePadSetup-<version>-win-x64.exe`, and uploads both as workflow artifacts.
After the tests pass, it downloads the pinned official UPX x64 build,
verifies its SHA-256, packs `NativePad.exe` with `--best --lzma`, and runs
UPX's integrity test before either package is assembled. Packing must remain
before any future code-signing step because modifying the executable would
invalidate a signature.
The workflow's `pack_with_upx` dispatch input defaults to `true`; set it to
`false` for a diagnostic or emergency uncompressed package if a security tool
flags the packed binary.

It builds with `AutoIncrementVersion=false`, so the released version is exactly
the value already committed in `src/NativePad.rc` — supply that version as the
workflow input, and the job fails if the built binary disagrees.

To build the installer locally instead:

```powershell
.\installer\build-installer.ps1
```

See [Installer](INSTALLER.md) for what the package contains, and
[Release Checklist](RELEASE_CHECKLIST.md) before publishing anything.
