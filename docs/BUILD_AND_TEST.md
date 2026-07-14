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

Building the NativePad app project automatically increments the fourth version
component in `src\NativePad.rc`. Use `/p:AutoIncrementVersion=false` only for a
diagnostic build where the source version must not change.

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

## Continuous Integration

GitHub Actions builds and tests both Debug x64 and Release x64 on Windows. The
workflow lives at `.github/workflows/ci.yml`.

The CI job currently targets `windows-2025-vs2026` because the project uses the
Visual Studio 2026/MSVC `v145` toolset. If the hosted runner labels change, keep
this file and the workflow aligned with the toolset declared in the solution.

## Release Packaging

The manual `Release Package` workflow lives at
`.github/workflows/release-package.yml`. It builds Release x64, runs the Release
test binary, creates `NativePad-<version>-win-x64.zip`, builds the Inno Setup
installer `NativePadSetup-<version>-win-x64.exe`, and uploads both files as
workflow artifacts.

The packaging build runs with `AutoIncrementVersion=false`, so the released
version equals the value already committed in `src/NativePad.rc`. Run it from
GitHub Actions with that committed version as the input. Before publishing the
artifact, complete [Release Checklist](RELEASE_CHECKLIST.md).

The installer can also be built locally with:

```powershell
.\installer\build-installer.ps1
```

See [Installer](INSTALLER.md) for installer scope and file-association behavior.

## Test Coverage

Current tests cover:

- Piece-table insert, erase, replace, range, and line count behavior.
- Incremental line-index updates.
- Mapped UTF-8/byte-backed line starts, range decoding, and find.
- Mapped UTF-16 line starts, range decoding, and find.
- Mapped-document refresh: appended content extends the line index across the
  old mapping boundary, and in-place rewrites report as replaced.
- Recovery journal round trips: journals from dead processes are claimed with
  exact text/metadata, journals from live processes are left alone, and
  clearing removes all journal files.
- Encoding labels, line-ending detection/normalization, and save encoding bytes.

Manual testing is still needed for:

- Window/dialog painting.
- DPI changes across monitors.
- Dark/light mode.
- Print output.
- File dialogs and drag/drop open.
- Very large real-world files.
- Follow Tail against a live log writer, and the reload-on-activation prompt.

## Clean Build Notes

Generated output lives under:

- `bin\`
- `obj\`
- `.vs\`

Those folders are build artifacts and should not be treated as source.
