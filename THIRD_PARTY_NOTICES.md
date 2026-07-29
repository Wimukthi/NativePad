# Third-party notices

## Wimukthi.Win32Theme

NativePad uses the `Wimukthi.Win32Theme` C++ framework from a separate source
checkout. The framework facade is licensed under the MIT License.

The default source layout places `Wimukthi.Win32Theme` beside this repository.
Its complete source and license are included in that project.

## Darkmodelib

`Wimukthi.Win32Theme` vendors Darkmodelib 0.75.0 from:

- Project: https://github.com/ozone10/win32-darkmodelib
- Pinned commit: `fa99647299c4edb3cf662bc14f19b5451090723e`
- Primary license: Mozilla Public License 2.0
- Additional MIT-covered portions: see the vendored license files

The complete corresponding Darkmodelib source is under
`Wimukthi.Win32Theme/third_party/darkmodelib`. NativePad binary packages also
carry it under `licenses/source/darkmodelib`. Changes to MPL-covered files must
remain available under the Mozilla Public License 2.0.
