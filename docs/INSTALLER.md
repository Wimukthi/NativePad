# Installer

NativePad uses Inno Setup 6 for the Windows installer.

The installer uses Inno Setup's dynamic modern wizard style, so Setup and
Uninstall follow the user's Windows light/dark app mode.

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

## Update, Reinstall, and Remove

The installer keeps a stable Inno Setup `AppId`, so later packages update the
same installation in place and reuse the previous installation directory.

When Setup detects an existing NativePad install, it shows a maintenance choice:

- Update from an older installed version to the package version.
- Repair/reinstall when the installed version matches the package version.
- Refuse to install over a newer installed version.
- Remove NativePad by launching the existing uninstaller.

Setup uses Windows Restart Manager through `CloseApplications=yes` so in-use
NativePad binaries can be closed before files are replaced.

## File Associations

The installer does not force NativePad as the `.txt` default. Windows protects
default-app ownership, and silent default changes are not reliable or desirable.

Use **Help > Set as Default Editor** inside NativePad to register the per-user
`.txt` handler and open Windows Default Apps for confirmation.
