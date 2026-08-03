# Third-Party Notices

NativePad itself is licensed under the GNU General Public License v3.0 (see
[LICENSE](LICENSE)). It links the components below.

## Wimukthi.Win32Theme

- Project: <https://github.com/Wimukthi/Wimukthi.Win32Theme>
- License: MIT

NativePad builds the framework's shared facade from a separate source checkout
placed beside this repository. Its complete source and licence live in that
project; binary packages carry the licence as
`licenses/Wimukthi.Win32Theme-MIT.txt`.

## Darkmodelib

`Wimukthi.Win32Theme` vendors Darkmodelib, which NativePad therefore also
compiles into its binaries.

- Project: <https://github.com/ozone10/win32-darkmodelib>
- Version: 0.75.0
- Pinned commit: `fa99647299c4edb3cf662bc14f19b5451090723e`
- Primary license: Mozilla Public License 2.0
- Additional MIT-covered portions: see the vendored licence files

The vendored copy carries a small local patch to group-box background painting,
recorded in the framework's `dependencies.lock.json`.

The complete corresponding source is available at
`Wimukthi.Win32Theme/third_party/darkmodelib` in the framework checkout, and is
shipped inside NativePad binary packages under `licenses/source/darkmodelib`.
Changes to MPL-covered files remain available under the Mozilla Public License
2.0.

## Windows Components

NativePad links only libraries supplied with Windows: Direct2D, DirectWrite,
Windows Imaging Component, WinHTTP, common controls, common dialogs, DWM,
UxTheme, GDI, Shell, and the CNG (`bcrypt`) hashing API. No redistribution is
required for these.
