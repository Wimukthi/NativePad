# Installer

NativePad uses Inno Setup 6 for the Windows installer.

## Local Requirements

- Release x64 build tools from Visual Studio.
- Inno Setup 6.

Install Inno Setup locally with:

```powershell
winget install --id JRSoftware.InnoSetup --exact
```

## Build Locally

From the repository root:

```powershell
.\installer\build-installer.ps1
```

The script builds Release x64, runs the Release tests, reads the post-build
version from `src/NativePad.rc`, and writes the installer to:

```text
installer\output\NativePadSetup-<version>-win-x64.exe
```

To package an already-built Release binary:

```powershell
.\installer\build-installer.ps1 -SkipBuild -SkipTests
```

If `-Version` is supplied, it must match the current `src/NativePad.rc` version.
This keeps the installer name and uninstall metadata aligned with the executable.

## Installed Files

The installer writes:

- `NativePad.exe`.
- `README.md`.
- `LICENSE`.
- `docs\`.

It also creates a Start Menu shortcut, offers an optional desktop shortcut, and
registers `NativePad.exe` under Windows App Paths.

## File Associations

The installer does not force NativePad as the `.txt` default. Windows protects
default-app ownership, and silent default changes are not reliable or desirable.

Use **Help > Set as Default Editor** inside NativePad to register the per-user
`.txt` handler and open Windows Default Apps for confirmation.
