# Versioning

NativePad uses a four-part version number:

```text
major.minor.patch.build
```

The current release baseline is `1.0.0.0`.

## Increment Rules

- Increment `major` for compatibility-breaking behavior, major architecture
  changes, or a release that intentionally changes the product contract.
- Increment `minor` for user-visible feature additions that preserve existing
  behavior.
- Increment `patch` for bug fixes, polish, reliability work, and performance
  improvements that do not add a new user-facing feature.
- Increment `build` for every packaged release build, even if the source change
  is only packaging, metadata, or installer work.

When a parent component changes, reset the components to its right. For example:

- `1.0.0.14` -> `1.0.1.0` for a patch release.
- `1.0.1.8` -> `1.1.0.0` for a minor feature release.
- `1.9.4.3` -> `2.0.0.0` for a major release.

## Source Of Truth

The executable resource in `src/NativePad.rc` is the version source of truth.
The About dialog reads the file version from that resource at runtime so the UI
and Windows file properties remain aligned.

Before producing a packaged release build:

1. Update `FILEVERSION` and `PRODUCTVERSION`.
2. Update the `FileVersion` and `ProductVersion` string values.
3. Build Release x64.
4. Run the test suite.
5. Confirm the About dialog displays the expected version and build timestamp.

Local Debug builds do not need a version bump unless they are being shared as a
packaged build.
