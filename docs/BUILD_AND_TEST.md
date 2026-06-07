# Build and Test

## Requirements

- Windows.
- Visual Studio 2026 or compatible MSVC toolchain.
- Windows 10 SDK or newer.
- x64 target platform.

The solution uses:

- C++20.
- MSVC toolset `v145`.
- Warnings as errors.
- Unicode character set.
- Per-monitor v2 DPI awareness through `src/app.manifest`.

## Build From Developer PowerShell

Debug:

```powershell
MSBuild.exe .\NativePad.sln /p:Configuration=Debug /p:Platform=x64 /m
```

Release:

```powershell
MSBuild.exe .\NativePad.sln /p:Configuration=Release /p:Platform=x64 /m
```

If `MSBuild.exe` is not on PATH, use the installed Visual Studio path, for
example:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" .\NativePad.sln /p:Configuration=Release /p:Platform=x64 /m
```

## Outputs

Debug app:

```text
bin\x64\Debug\NativePad.exe
```

Release app:

```text
bin\x64\Release\NativePad.exe
```

Debug tests:

```text
bin\x64\Debug\NativePad.Tests.exe
```

Release tests:

```text
bin\x64\Release\NativePad.Tests.exe
```

## Run Tests

Debug:

```powershell
.\bin\x64\Debug\NativePad.Tests.exe
```

Release:

```powershell
.\bin\x64\Release\NativePad.Tests.exe
```

Expected output:

```text
DocumentBuffer tests passed
LineIndex tests passed
MappedTextDocument tests passed
TextFormat tests passed
```

## Test Coverage

Current tests cover:

- Piece-table insert, erase, replace, range, and line count behavior.
- Incremental line-index updates.
- Mapped UTF-8/byte-backed line starts, range decoding, and find.
- Mapped UTF-16 line starts, range decoding, and find.
- Encoding labels, line-ending detection/normalization, and save encoding bytes.

Manual testing is still needed for:

- Window/dialog painting.
- DPI changes across monitors.
- Dark/light mode.
- Print output.
- File dialogs and drag/drop open.
- Very large real-world files.

## Clean Build Notes

Generated output lives under:

- `bin\`
- `obj\`
- `.vs\`

Those folders are build artifacts and should not be treated as source.
