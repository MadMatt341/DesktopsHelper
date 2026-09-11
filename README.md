# Desktops Helper

A small native Windows widget for working across five virtual desktops on one monitor.

## Run

Open `build/release/DesktopsHelper.exe`. Keep the bundled `VirtualDesktopAccessor.dll` beside it. The adapter statically links its C runtime; no separate Visual C++ redistributable, .NET runtime, installation, administrator rights, or network connection is needed for this build. Clean-machine qualification beyond the tested Windows build remains outstanding.

- **Win+1–5:** switch to that desktop and hand keyboard focus to its frontmost available window (or the desktop shell if none is available).
- **Shift+Win+1–5:** move the foreground window there and stay on your current desktop.
- **Click a number:** switch desktops without activating the widget.
- **Right-click the widget or its tray icon:** change placement, reconnect, or exit.

The compact strip uses 13-pixel Consolas digits (scaled for DPI), rounded corners, subtle hover feedback, and a filled active button with a bold number and blue underline. Its neutral colors follow the Windows system light/dark setting and use system colors in high-contrast mode. Theme changes update through Windows messages; there are no animations or repaint timers. Missing destination desktops are created on demand; existing desktops and their names are preserved. Desktops beyond five remain accessible through Windows Task View; none of 1–5 is highlighted when you're on one of them. Use the number row, not the numeric keypad.

The widget hides while the foreground app has fullscreen content covering its monitor and returns without taking focus when that app leaves fullscreen, is minimized/closed, or loses foreground status. Ordinary maximized windows keep the indicator visible. Foreground, geometry, and window lifecycle events drive this behavior without polling; desktop shortcuts remain active while hidden.

The widget overlays the bottom-left taskbar area. It listens for foreground and window-stacking changes and restores its position above the taskbar if Explorer covers it; this uses no polling timer. If it covers Weather, Start, or another control, select **Place above taskbar**. `DesktopsHelper.exe --above-taskbar` starts in that position. Auto-hide taskbars use the above-taskbar placement. Placement changes from the menu last for the current session.

Exiting restores normal Windows shortcuts. There is no automatic startup registration.

Only one helper runs per Windows session. It owns a mutex while running, so a retained mutex handle from an exited process does not prevent restarting it. A running older build is also detected by its widget window.

## Build

Requires an x64 MinGW-w64 GCC toolchain on PATH and PowerShell:

```powershell
./scripts/build.ps1
```

The helper is C11/Win32, built with optimization and `-Wall -Wextra -Werror`. The checked-in adapter DLL is verified against `vendor/adapter.sha256` before every build. Rebuilding the adapter is optional and requires Python 3, Rust's MSVC toolchain, and Visual Studio C++ build tools. Use `-Python <path-to-python.exe>` if Python is not on PATH:

```powershell
./scripts/rebuild-adapter.ps1
./scripts/build.ps1
```

## Test and benchmark

Run these from a normal interactive Windows session, with Explorer running. Restricted automation sandboxes cannot reliably access the desktop service.

With the helper stopped, `./scripts/test.ps1` runs the normal regression suite and retains a report with the exact executable/DLL hashes. Individual checks are available below.

```powershell
# Read-only adapter compatibility probe:
./build/release/integration.exe

# Switches desktops and moves only a disposable test window; restores the starting desktop.
# Creates missing desktops up to five and leaves them available.
./build/release/integration.exe --exercise

# Start the helper, then test the ten real keyboard shortcuts:
./build/release/shortcuts.exe

# Verify the indicator remains uncovered when the taskbar comes to the front:
./build/release/visibility.exe --exercise

# While the helper is running: fullscreen, restore, minimize, and close transitions.
./build/release/fullscreen.exe

# While the helper is running: temporary desktops test reorder/delete notifications.
./build/release/topology.exe

# No real keyboard injection: modifier/repeat and unavailable-service policy.
./build/release/input-policy.exe

# Stop the helper, then record startup and a one-minute idle sample:
./scripts/stop.ps1
./scripts/benchmark.ps1 -Seconds 60 -Output benchmarks/local-idle.json

# Helper must be stopped for these isolated adapter/failure tests:
./build/release/lifecycle.exe
./scripts/test-failures.ps1
./scripts/test-benchmark-failure.ps1
```

Avoid typing or clicking during shortcut tests because they temporarily control focus and switch desktops. They never intentionally move an existing user window. Benchmark details and retained measurements are in [benchmarks/README.md](benchmarks/README.md).

## Design and compatibility

The UI repaints only when needed. A dedicated keyboard thread intercepts Win+1–5 and Shift+Win+1–5 and submits commands to a bounded internal queue. A separate worker owns desktop COM calls and notification registration; the UI never waits for a desktop operation. After a requested switch, a short-lived timer lets Explorer settle before the worker activates a destination window. It preserves an already focused destination or pinned window, skips minimized windows and the widget, and stops within 1.5 seconds; there is no idle focus polling. If ordinary activation is denied, the worker temporarily attaches to the foreground and destination input queues, activates the destination, and detaches. An unresponsive app can stall that worker, which remains subject to the existing service timeout and shutdown deadline. External window messages carry wakeups, not executable commands or window pointers. Move requests capture their target and process ID and reject stale targets. Ctrl/Alt combinations and other digits pass through. No keystrokes are saved or transmitted.

The patched adapter forwards switches, creation, deletion, and reordering notifications and confirms successful subscription before the helper reports ready. Its threads use normal priority and block on events with no recurring health-check timer. Explorer's `TaskbarCreated` broadcast and reported operation failures trigger reconnection. Recovery makes up to five delayed retries and then stops. A five-second deadline runs only while work is pending. Timed-out/unavailable service releases new shortcut combinations to Windows and clears the active-number highlight. Use Reconnect if recovery stops; a stuck worker requires restarting the helper. A silently lost subscription without a restart or API error still requires manual reconnect; there is deliberately no idle polling fallback.

Shutdown unregisters notifications without holding the adapter listener lock while joining threads. Notification threads explicitly initialize and release COM, with their proxies released before COM teardown. If the worker fails to stop within two seconds, the process terminates with code 2, bypassing DLL detach to avoid waiting on locks owned by a hung thread. Benchmarks treat this as failure. Input, desktop service, UI, and crash diagnostics are separate modules under `src/`.

Windows 11 no longer supports traditional third-party taskbar deskbands, so this is a separate overlay. It reserves no taskbar space and can cover existing controls. Explorer must register it as an application window so it can be pinned across desktops; its taskbar button is removed through `ITaskbarList`. It may still appear in Alt+Tab/Task View. Borderless fullscreen transitions are covered by a disposable-window regression test. Exclusive fullscreen games, multiple monitors, and every DPI/taskbar configuration still need manual qualification.

The public Windows desktop API is insufficient for switching desktops or moving other apps' windows. The helper uses the MIT-licensed [VirtualDesktopAccessor](https://github.com/Ciantic/VirtualDesktopAccessor) adapter, based on commit `8172097993b1194e3d5e2ff38421ebb06f867b6c` with the checked-in `scripts/patch-adapter.py` changes. The immutable upstream archive and license are included. The host checks a private ABI marker before calling COM, so an older upstream DLL is rejected. Use the bundled patched DLL. Target: x64 Windows 11 24H2 and newer; tested on **25H2 26200.9445**. These undocumented COM interfaces may change with Windows updates.

Pinned windows, elevated applications, modal windows, and application-specific restrictions can prevent moving a window. The helper runs at normal user privilege. After an Explorer restart it attempts to reconnect; the tray menu also offers a manual reconnect. Restart the helper if recovery fails. Explorer restart recovery and elevated-window behavior require further manual qualification.

One COM access-violation crash occurred during the original end-to-end run. Its root cause remains unconfirmed; moving desktop work off the UI thread and fixing shutdown do not prove that specific crash is fixed. A best-effort exception handler now writes the UTC time, exception code, module/offset, and operation phase to `%LOCALAPPDATA%\DesktopsHelper\crash.log`. It does not save window titles, keystrokes, or memory dumps. Phase 10 means connect, 11 action, 12 refresh, and 20 shutdown. Native crashes still terminate this process; the worker is a thread, not a crash-isolation process. Retest under a debugger if it recurs. Release binaries remain unsigned.

Official references: [keyboard hook constraints](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc), [Windows 11 feature changes](https://www.microsoft.com/en-us/windows/windows-11-specifications).
