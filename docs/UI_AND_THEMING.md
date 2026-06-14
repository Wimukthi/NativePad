# UI and Theming

NativePad uses native Win32 widgets, but several surfaces are custom-painted to
make dark mode coherent.

## Main Window

The main window is implemented in `AppWindow` in `src/main.cpp`.

It owns:

- Top-level window frame.
- Custom menu strip.
- Popup menus (window classes and painting live in `src/PopupMenu.*`).
- Editor child control.
- Owner-draw status bar.
- Dialog creation and command routing.

Shared dark-mode plumbing — theme color palettes, DPI scaling, dark frame and
control theming, and the owner-draw control helpers — lives in `src/UiSupport.*`
and is reused by the shell and every custom dialog. Each dialog (Font, Find and
Replace, Go To, About) is implemented in its own `src/*Dialog.*` module.

## Dark Mode

NativePad starts from the system app theme. The View menu can force dark/light,
and that override is persisted under `%LOCALAPPDATA%\NativePad\NativePad.ini`.

Dark-mode implementation uses:

- DWM dark frame attributes.
- `SetWindowTheme(..., L"DarkMode_Explorer", ...)` for native controls.
- Owner-draw painting for menus, status bar, custom dialog controls, and problem
  areas where Windows still paints light fragments.

Color palettes are centralized in:

- `ColorsForTheme`
- `DialogColorsForTheme`

## DPI

The manifest requests per-monitor v2 DPI awareness. Layout code scales constants
with `ScaleForDpi`.

Dialog and editor code should not use hard-coded physical pixels unless the
value is intentionally device-pixel based.

## Custom Dialogs

NativePad currently has custom dialogs for:

- About.
- Go To.
- Find/Replace.
- Font.

They use normal child controls where possible, with custom parent-painted
backgrounds and borders.
The About dialog also exposes update checking controls; those controls reuse the
same button and checkbox theming as the rest of the custom dialogs.
Dialogs rely on the native DWM frame and shadow. Avoid adding separate shadow
helper windows around dialogs because they do not match the DWM-rounded frame
and can read as extra window chrome.

Important conventions:

- Parent windows draw edit/list borders with `DrawDialogChildBorder`.
- Edit/list controls are inset with `MoveBorderedControl`.
- Controls use `WS_CLIPSIBLINGS` where resize paint artifacts are likely.
- Owner-draw toggles and list boxes are used when native dark-mode painting is
  unreliable.

## Menu Strip and Popup Menus

The menu strip is a custom child window rather than the built-in menu bar. Popup
menus are owner-drawn so item text, accelerator text, separators, selected rows,
disabled text, and borders match dark mode.

Popup menus are no-activate top-level windows. A click-through layered shadow
window is shown behind each popup so the shadow remains visible in dark mode
while preserving the same active-window behavior as standard Win32 menus.
When a popup has mouse capture, outside right-clicks are re-resolved against the
main window and reposted as `WM_CONTEXTMENU` so the editor context menu can open
without leaving the top-level menu stuck open.
The menu strip also emulates standard keyboard cues: Alt/F10 reveals top-level
mnemonic underlines and moves focus into menu navigation until Esc or a menu
command exits that mode.

## Editor Surface

The editor is a child HWND registered by `EditorView`.

Rendering uses:

- Direct2D render target.
- DirectWrite text format.
- Theme-provided background, text, selection, caret, and line-number gutter
  colors.

Line numbers are an optional View-menu gutter. They are visual-only, right
aligned, DPI/font-aware, and use an arrow cursor over the gutter rather than the
text insertion cursor.

The editor uses native scrollbars. Native scrollbar dark-mode support depends on
Windows theme behavior, so scrollbar regressions should be tested manually.

The status bar is owner-drawn and reports current line, column, total logical
line count, encoding, read-only state where applicable, file size, and character
count. Non-editor chrome uses the arrow cursor; the I-beam is reserved for the
editor text area.

## Painting Checklist

When changing UI code, test:

- Dark and light mode.
- 100%, 125%, 150%, and mixed-DPI monitor moves.
- Resizing dialogs.
- Scrolling owner-draw list boxes.
- Popup menu hover, disabled items, and separators.
- Context menu over the editor.
- Horizontal and vertical scrollbars.
