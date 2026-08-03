# Versioning

NativePad uses a four-part Windows version number:

```text
major.minor.patch.build
```

The baseline was `1.0.0.0`. The current version always lives in
`src/NativePad.rc`.

## Increment Rules

| Component | Increment for |
| --- | --- |
| `major` | Compatibility-breaking behavior, major architecture changes, or an intentional change to the product contract |
| `minor` | User-visible feature additions that preserve existing behavior |
| `patch` | Bug fixes, polish, reliability, and performance work with no new user-facing feature |
| `build` | Every build — MSBuild does this automatically |

Reset everything to the right of the component you increment:

```text
1.0.0.14  ->  1.0.1.0   patch release
1.0.1.8   ->  1.1.0.0   minor feature release
1.9.4.3   ->  2.0.0.0   major release
```

## Source of Truth

The executable resource in `src/NativePad.rc` is authoritative. Four fields
must always agree: `FILEVERSION`, `PRODUCTVERSION`, `FileVersion`, and
`ProductVersion`.

The About dialog reads the file version from the running executable at runtime,
so the UI and the Windows file properties can never drift apart.

## Automatic Build Increment

Before `NativePad.vcxproj` builds, MSBuild runs
`tools\Update-NativePadVersion.ps1`, which increments the fourth component and
rewrites all four resource fields in a single pass — so MSBuild never sees
partially updated version metadata.

This intentionally modifies `src/NativePad.rc`, so **a successful local build
leaves a version change in your working tree.** The script refuses to write if
the four fields disagree, if a component would exceed 65535, or if the
replacement produced no change.

For a diagnostic build that must not touch the version:

```powershell
MSBuild.exe .\NativePad.sln /p:Configuration=Release /p:Platform=x64 /p:AutoIncrementVersion=false /m
```

To print the current version without changing anything:

```powershell
.\tools\Update-NativePadVersion.ps1 -PrintVersion
```

## Releasing a Version

1. Manually set `major`, `minor`, or `patch` in `src/NativePad.rc` if the
   release warrants it, resetting the components to its right.
2. Build Release x64 and let MSBuild increment `build`.
3. Run the test suite.
4. Confirm the About dialog shows the expected version and build timestamp.
5. Commit the resulting `src/NativePad.rc`.
6. Run the `Release Package` workflow with that exact version as its input.

The packaging workflow builds with `AutoIncrementVersion=false`, so the released
version is exactly what was committed. It fails if the built executable's
version does not match the input you supplied.

Record the release in [CHANGELOG.md](../CHANGELOG.md) and follow
[Release Checklist](RELEASE_CHECKLIST.md) before publishing.
