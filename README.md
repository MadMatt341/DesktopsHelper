# Desktops Helper

Native **1–5** buttons at the far-left edge of the primary Windows taskbar, plus keyboard shortcuts for switching desktops and moving windows. The buttons reserve their own taskbar space and follow the active desktop.

## Use

Open `build/release/DesktopsHelper.exe`; native taskbar integration is the default and only UI. Keep its bundled DLLs and controller beside it. Right-click the tray icon to reconnect or exit.

- **Win+1–5:** switch desktops and focus the destination's available frontmost window.
- **Shift+Win+1–5:** move the foreground window there while staying on your current desktop.
- **Click 1–5:** switch desktops through the native taskbar buttons.

Missing desktops up to five are created on demand; existing desktops and names are preserved. Other desktops remain accessible through Task View. Only one helper runs per Windows session. Exiting restores normal Windows shortcuts. Startup registration is managed separately.

The native taskbar integration currently targets a qualified Windows 11 Taskbar.dll image. Unsupported images leave the tray and working keyboard shortcuts available, without a floating overlay. See [compatibility details](src/taskbar-prototype/README.md#compatibility) if the buttons do not appear.

## Develop

Start with the guide for your task; implementation details live one level deeper.

| Need | Read |
|---|---|
| Build, run, or replace binaries | [Development](docs/development.md) |
| Understand the main parts and their boundaries | [Architecture](docs/architecture.md) |
| Select tests or measure performance | [Testing](docs/testing.md) |

[MIT license](LICENSE). The bundled desktop adapter retains its [upstream license](vendor/LICENSE.VirtualDesktopAccessor.txt).
