# Changelog

All notable changes to NativePad are recorded here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
NativePad uses a four-part `major.minor.patch.build` version — see
[docs/VERSIONING.md](docs/VERSIONING.md) — where the build component is
incremented automatically and does not by itself imply a release.

## [Unreleased]

Nothing yet.

## [1.1.0.4] - 2026-08-07

### Added

- Shared plain-text file-type catalog used by the Open/Save dialogs, Windows
  per-user Open With registration, and extension-based editor behavior.
- Lightweight line-local syntax highlighting for JSON, INI/configuration,
  Markdown, and XML documents. Large-file backends remain on the fast plain
  text renderer.
- Proper `.nfo` support, including catalogued file associations and OEM 437
  decoding/saving for legacy DOS artwork.
- Release packaging now verifies and uses a pinned UPX build to reduce the
  distributed executable size.

## [1.1.0.3] - 2026-07-29

### Changed

- Integrated the shared
  [`Wimukthi.Win32Theme`](https://github.com/Wimukthi/Wimukthi.Win32Theme)
  framework for consistent light, dark, and High Contrast behavior across the
  main window and the custom dialogs. Building now requires a checkout of that
  repository beside NativePad.
- Menu tracking now behaves like native Windows menus: while one top-level menu
  is open, moving across the menu bar switches to the menu under the pointer.
- Updated CI, release packaging, the installer, documentation, and third-party
  notices for the shared theme dependency.

### Fixed

- Main-window activation no longer flickers when a modal child window closes;
  the owner and the previous editor focus are restored before teardown.

## [1.1.0.2] - 2026-07-22

### Fixed

- The menu bar and the owner-drawn popup and context menus no longer flicker
  under the mouse. Painting is double-buffered — composed off-screen and
  blitted in one pass — so hover repaints atomically.

## [1.1.0.1] - 2026-07-22

First minor feature release since 1.0. Every addition preserves classic Notepad
behavior.

### Added

- **Editable large files.** Files above the editable limit still open read-only
  for fast viewing; **Edit > Enable Large-File Editing** switches them to a
  memory-mapped piece-table backend that never decodes the whole file, with an
  atomic save. Printing, crash-recovery journaling, and Save As encoding
  conversion are not supported for large files.
- **External change detection.** Prompts to reload when the open file changes
  on disk, warning before unsaved edits would be discarded.
- **Follow Tail (`F6`).** Follows appended content in growing files such as
  live logs; read-only mapped large files refresh incrementally rather than
  rescanning.
- **Crash recovery.** Unsaved edits are journaled in the background and offered
  for restore after an unclean exit.
- **Editor zoom.** `Ctrl`+wheel, `Ctrl+Plus/Minus`, and `Ctrl+0`, shown as a
  percentage in the status bar. Scaling affects rendering only, not the saved
  font size.
- **Recent files.** Up to eight recently opened files in the File menu.
- **Line operations.** Duplicate (`Ctrl+Shift+D`), Delete (`Ctrl+Shift+K`), and
  Move Up/Down (`Alt+Up` / `Alt+Down`), each a single undo step.

### Fixed

- The window is moved back onto the primary monitor when the display it was on
  is disconnected, and no longer launches off-screen from a stale saved
  position.

## [1.0.0.4] - 2026-06-14

### Added

- Custom dark-themed message dialogs for confirmations, errors, and
  informational prompts.
- Embedded 256 px PNG message icons decoded at the current DPI, replacing
  stretched low-resolution system icons.

### Fixed

- Custom popup and context menus show the arrow cursor instead of the editor
  I-beam.

## [1.0.0.3] - 2026-06-14

### Added

- Update checking against GitHub releases, including installer download.

### Changed

- Settings moved to `%LOCALAPPDATA%\NativePad\NativePad.ini`, with a one-time
  migration from the previous registry values.
- Improved dark styling in the installer, and better maintenance/update
  behavior.

### Fixed

- Menu keyboard cue handling and display repaint edge cases.
- Long unwrapped editor lines are clipped so they cannot paint under the
  line-number gutter.

## [1.0.0.2] - 2026-06-11

First published release.

### Added

- Custom dark-aware About, Font, Find/Replace, and Go To dialogs.
- Inno Setup installer packaging, plus a release workflow that uploads both the
  installer and the portable ZIP.
- Automatic build-version incrementing backed by `src/NativePad.rc`.

### Changed

- Split the Win32 UI shell into focused modules for dialogs, popup menus,
  settings, file I/O, printing, and shared theming.
- Expanded tests and documentation around the mapped read-only large-file
  backend.

[Unreleased]: https://github.com/Wimukthi/NativePad/compare/v1.1.0.4...HEAD
[1.1.0.4]: https://github.com/Wimukthi/NativePad/releases/tag/v1.1.0.4
[1.1.0.3]: https://github.com/Wimukthi/NativePad/releases/tag/v1.1.0.3
[1.1.0.2]: https://github.com/Wimukthi/NativePad/releases/tag/v1.1.0.2
[1.1.0.1]: https://github.com/Wimukthi/NativePad/releases/tag/v1.1.0.1
[1.0.0.4]: https://github.com/Wimukthi/NativePad/releases/tag/v1.0.0.4
[1.0.0.3]: https://github.com/Wimukthi/NativePad/releases/tag/v1.0.0.3
[1.0.0.2]: https://github.com/Wimukthi/NativePad/releases/tag/v1.0.0.2
