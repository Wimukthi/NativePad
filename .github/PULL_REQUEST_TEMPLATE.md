## What this changes

<!-- What the change does, and why. Link the issue it closes, if any. -->

## How it was tested

<!-- Both configurations, or say which you skipped and why. -->

- [ ] `MSBuild.exe .\NativePad.sln /p:Configuration=Debug /p:Platform=x64 /m` and Debug tests pass
- [ ] `MSBuild.exe .\NativePad.sln /p:Configuration=Release /p:Platform=x64 /m` and Release tests pass
- [ ] New non-UI logic has tests

### Manual UI verification

<!-- Delete this section if the change cannot affect the UI. -->

- [ ] Dark mode
- [ ] Light mode
- [ ] Windows High Contrast
- [ ] Non-100% DPI, or a mixed-DPI monitor move

What I checked manually, and what I did not:

## Documentation

- [ ] Relevant docs updated (see [CONTRIBUTING.md](../CONTRIBUTING.md))
- [ ] `CHANGELOG.md` updated under `Unreleased`, if user-visible
- [ ] Screenshots in `docs/images/` refreshed, if a UI change made them misleading

## Version

- [ ] The `src/NativePad.rc` version bump from building is either included or reverted — not left half-applied
