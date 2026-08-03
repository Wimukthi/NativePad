# Installer

NativePad ships an Inno Setup 6 installer built from `installer/NativePad.iss`.
It uses Inno's `modern dynamic` wizard style, so Setup and Uninstall follow the
user's Windows light/dark app mode.

## Requirements

- Release x64 build tools from Visual Studio.
- Inno Setup 6.
- A `Wimukthi.Win32Theme` checkout beside NativePad, or `-ThemeRoot` pointing at
  one — the package includes the framework and Darkmodelib licences and the
  complete corresponding Darkmodelib source.

```powershell
winget install --id JRSoftware.InnoSetup --exact
```

## Build

From the repository root:

```powershell
.\installer\build-installer.ps1
```

The script builds Release x64, runs the Release tests, reads the post-build
version from `src/NativePad.rc`, and writes:

```text
installer\output\NativePadSetup-<version>-win-x64.exe
```

Useful switches:

```powershell
.\installer\build-installer.ps1 -SkipBuild -SkipTests
```

```powershell
.\installer\build-installer.ps1 -ThemeRoot D:\Libraries\Wimukthi.Win32Theme
```

If you pass `-Version`, it must match the current `src/NativePad.rc` version.
That keeps the installer filename and the Windows uninstall metadata aligned
with the executable.

The script locates MSBuild and `ISCC.exe` on `PATH`, then in the usual Visual
Studio and Inno Setup install locations, then via `vswhere`. It fails with an
actionable message rather than guessing.

## What Gets Installed

| Path | Contents |
| --- | --- |
| `NativePad.exe` | The application |
| `README.md`, `LICENSE`, `THIRD_PARTY_NOTICES.md`, `CHANGELOG.md` | Project documents |
| `docs\` | The full documentation set |
| `licenses\` | `Wimukthi.Win32Theme` MIT licence, Darkmodelib MPL-2.0 and MIT licences |
| `licenses\source\darkmodelib\` | Complete corresponding Darkmodelib source, as MPL-2.0 requires |

Setup also creates a Start Menu shortcut, offers an optional desktop shortcut,
and registers `NativePad.exe` under Windows App Paths.

Installation requires administrator privileges and targets 64-bit Windows 10 or
later.

## Update, Repair, and Remove

The installer keeps a stable Inno Setup `AppId`, so later packages update the
same installation in place and reuse the previous install directory.

When Setup finds an existing installation it shows a maintenance page:

- **Update** an older installed version to the package version.
- **Repair/reinstall** when the installed version matches the package version.
- **Refuse** to install over a newer installed version.
- **Remove** NativePad by launching the existing uninstaller.

`CloseApplications=yes` puts Setup on the Windows Restart Manager, so a running
NativePad can be closed before its files are replaced.

## File Associations

The installer does **not** force NativePad as the `.txt` default. Windows
protects default-app ownership, and silently reassigning it is neither reliable
nor desirable.

Use **Help > Set as Default Editor** inside NativePad instead: it registers the
per-user `.txt` handler and opens Windows Default Apps so you can confirm.

## Signing

Releases are currently unsigned. SmartScreen will warn on first run for both the
installer and the portable ZIP. Keep that clearly labelled on the release page
until code signing is in place.
