# Security Policy

## Supported Versions

Only the latest release receives fixes. Older versions are not patched — please
update before reporting.

| Version | Supported |
| --- | --- |
| Latest release | Yes |
| Anything older | No |

## Reporting a Vulnerability

**Do not open a public issue for a security problem.**

Report it privately through GitHub:
[Security > Report a vulnerability](https://github.com/Wimukthi/NativePad/security/advisories/new).

Please include:

- The NativePad version from **Help > About**.
- Your Windows version and build.
- What an attacker gains, and what access they need to get it.
- Steps to reproduce. If a specific file triggers the problem, include its size
  and encoding — and the file itself if you can share it safely.

This is a single-maintainer project, so expect an acknowledgement within a few
days rather than within hours. You will get an update when the report is
triaged and again when a fix ships. Credit in the release notes is offered
unless you would rather stay anonymous.

## Scope

NativePad is a local desktop text editor. The areas most worth scrutiny:

- **File parsing.** Encoding detection and decoding of untrusted files —
  crashes, out-of-bounds reads, or hangs on malformed UTF-8/UTF-16 input.
- **Memory-mapped large files.** Line indexing and range decoding over
  attacker-controlled content.
- **Save paths.** The atomic replace used for large-file saves, and anything
  that could truncate or corrupt a file on an interrupted save.
- **The update checker.** It fetches release metadata over HTTPS from the
  GitHub API, downloads an installer to `%LOCALAPPDATA%\NativePad\Updates`,
  verifies it against the release asset's SHA-256 digest where GitHub publishes
  one, and launches it elevated. Weaknesses in that chain are in scope.
- **File association registration.** The per-user `.txt` handler written by
  **Help > Set as Default Editor**.

### Out of scope

- Missing code signing. Releases are knowingly unsigned; the SmartScreen
  warning is expected and documented.
- Anything requiring the attacker to already have administrator rights or write
  access to the installation directory.
- Behavior of `Wimukthi.Win32Theme` or Darkmodelib — report those to their own
  projects, though a heads-up here is welcome.
