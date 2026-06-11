# Versioning

NativePad uses a four-part version number:

```text
major.minor.patch.build
```

The initial release baseline was `1.0.0.0`. The current source version is the
value in `src/NativePad.rc`.

## Increment Rules

- Increment `major` for compatibility-breaking behavior, major architecture
  changes, or a release that intentionally changes the product contract.
- Increment `minor` for user-visible feature additions that preserve existing
  behavior.
- Increment `patch` for bug fixes, polish, reliability work, and performance
  improvements that do not add a new user-facing feature.
- Increment `build` for every build. MSBuild does this automatically for the
  NativePad application project.

When a parent component changes, reset the components to its right. For example:

- `1.0.0.14` -> `1.0.1.0` for a patch release.
- `1.0.1.8` -> `1.1.0.0` for a minor feature release.
- `1.9.4.3` -> `2.0.0.0` for a major release.

## Source Of Truth

The executable resource in `src/NativePad.rc` is the version source of truth.
The About dialog reads the file version from that resource at runtime so the UI
and Windows file properties remain aligned.

## Automatic Build Increment

`NativePad.vcxproj` runs `tools\Update-NativePadVersion.ps1` before the app
project builds. The script increments only the fourth component and updates all
resource fields together:

- `FILEVERSION`.
- `PRODUCTVERSION`.
- `FileVersion`.
- `ProductVersion`.

This intentionally modifies `src/NativePad.rc`, so a successful local build will
leave a version change in the working tree.

To run a diagnostic build without changing the version:

```powershell
MSBuild.exe .\NativePad.sln /p:Configuration=Release /p:Platform=x64 /p:AutoIncrementVersion=false /m
```

Before producing a packaged release build:

1. Manually update `major`, `minor`, or `patch` in `src/NativePad.rc` if the
   release requires it, resetting the components to the right.
2. Build Release x64 and let MSBuild increment the `build` component.
3. Run the test suite.
4. Confirm the About dialog displays the expected version and build timestamp.

The `Release Package` workflow asks for the expected version after the automatic
increment and fails if the built executable version does not match.
