# Native left taskbar integration

Our own Explorer component adds **1 2 3 4 5** at the far-left edge of the primary taskbar. These are native XAML controls in a reserved layout column, not a floating overlay. Clicking switches desktops through the existing helper; active-desktop notifications update the selected button. Keyboard shortcuts and window movement stay in the helper process.

No Windhawk, ExplorerPatcher, third-party mod, or additional runtime is installed or linked. Microsoft C++/WinRT and Windows SDK headers are build dependencies; the C runtime is statically linked. The existing desktop adapter remains part of the helper, outside Explorer.

## Build and run

Build with an x64 MinGW-w64 compiler on PATH and installed Visual Studio C++ tools plus the Windows SDK. The script discovers the C++ installation using Microsoft's vswhere:

```powershell
./scripts/build.ps1 -NativeTaskbar
./build/release/DesktopsHelper.exe --native-taskbar
```

The controller and DLL must be beside `DesktopsHelper.exe`. The helper starts the controller without showing a console, waits for the native control, and then hides the old overlay. Attachment attempts are bounded; unsupported taskbar versions retain the normal overlay. Registration for Windows startup is separate from the program itself. This workstation's sign-in entry has been configured with `--native-taskbar`.

The component DLL stays resident in Explorer until Explorer exits. Remove the native UI before replacing it; replacing the same loaded DLL requires an Explorer restart. The build script accepts an optional alternate output directory under `build` for development.

## Lifecycle and protocol

- A thread-specific `WH_CALLWNDPROC` hook starts our component on the primary taskbar thread. The controller removes the hook after delivery. The registered attachment message carries a helper HWND and controller PID, never memory pointers, and validates the target class/process. A deferred mount checks both captured process handles and its deadline.
- A version-checked private entry point exposes the taskbar's XAML root. We find the main `RootGrid` under `Taskbar.TaskbarFrame` and add an auto-width left column plus the remaining content column. The background spans both; the taskbar's item repeater occupies the remaining content column. The outer system-tray container is unchanged.
- The strip is 120 logical pixels wide, with eight-pixel side margins and five 24 × 32 click targets. The 12-pixel Cascadia Mono digits (Consolas fallback) sit directly on the taskbar; the active desktop is bold and bright, and inactive digits are muted. A small rounded surface appears only on hover or press. Native toggle semantics, tooltips, accessible names, and system theme brushes are retained.
- `taskbar_protocol.h` defines pointer-free, same-user window messages. Requests are subscribe (0), switch to desktop 1–5, or detach (6). The helper checks the endpoint's class, Explorer process, mounted property, and owning helper PID, then enqueues bounded desktop actions. State reports are unavailable (0), current desktop 1–5, or another desktop (6). No desktop COM calls run in Explorer.
- Initial attachment waits for desktop-service readiness. Reconnect retires the native endpoint and shows the fallback while Windows registers/pins its desktop view again, then reattaches. Overlay visibility event subscriptions are suspended while native controls are mounted and restored on fallback.
- Desktop service updates publish changed state asynchronously. No idle polling is used for highlighting. Buttons disable while the helper reports an unavailable service. A one-shot process wait removes the strip after an abrupt helper exit; its callback only posts to the UI thread and is drained before endpoint destruction.
- Each controller attempt has a five-second deadline. The helper allows at most fifteen seconds overall, starts no late attempt, and owns controller processes in a kill-on-close job. Deferred callbacks reject expired requests and exited controllers/helpers. The pinned DLL and its registered window class remain resident until Explorer exits.
- Removal is idempotent and attempts each cleanup step independently: revoke handlers, restore existing column/span values, remove our columns, and release UI objects. Failed mounts also destroy the endpoint and clear attachment state. The helper reveals its fallback overlay.
- Exiting the helper removes the native control. When a genuine Explorer PID change is broadcast, the helper starts a replacement that waits for the old helper to exit. This discards stale COM subscriptions and reattaches the native controls automatically. This behavior is limited to native mode.

## Diagnostics

```powershell
./build/release/TaskbarPrototype.exe status
./build/release/TaskbarPrototype.exe remove
./build/release/TaskbarPrototype.exe attach
./build/release/TaskbarPrototype.exe watch-menu
./build/taskbar-left/NativeTaskbarTest.exe
```

`status` checks actual native layout geometry, including left-edge placement. `watch-menu` explicitly observes foreground process identity for up to 60 seconds, captures only the taskbar strip into `menu-taskbar.bmp`, and exits. The component performs no file logging. It stores the latest attachment HRESULT in the helper's `DesktopsHelper.NativeError` property; the controller prints it on failure. This bounded diagnostic avoids disk I/O on Explorer's UI thread.

`NativeTaskbarTest.exe` finds the five buttons by accessibility name, verifies that their click points belong to the taskbar, clicks each one, and checks both the helper's desktop and native highlight. It also checks invalid requests, restores the initial desktop/cursor, and checks that the fallback overlay is hidden. Avoid other input during this short test.

Build diagnostic and test tools explicitly with `cmd /c scripts\build-taskbar-prototype.cmd taskbar-left tests`. They are excluded from the normal release package. With the helper stopped, `scripts/test.ps1 -NativeTaskbar` exercises rollback at eleven mutation stages, expiration of a deferred request, 100 attach/remove cycles, abrupt disposable-owner exit, the existing service/input suite, and actual native buttons. The injected failures exist only in `DesktopsHelper.TaskbarV4.Tests.dll`, using separate test window/message identities. Never package this DLL.

`TaskbarSymbols.exe` uses the Windows SDK's DbgHelp and Microsoft's symbol server to inspect matching symbols. The cache is under `build/symbols`; the utility does not attach to Explorer.

## Qualified image and evidence

Validated on this workstation on 2026-09-14:

- Taskbar.dll SHA-256: `5DCEEA036939ACB4E43B801A7A63530F328EDAD164741E6529D3D32E9B1C5866`.
- PE timestamp: `2378140881`; image size: `3162112`.
- PDB-resolved RVAs: `CTaskBand::GetTaskbarHost` `0x1097a0`; `ITaskListWndSite` vtable `0x1cbf50`; `TaskbarHost::FrameHeight` `0x8aefc`; shared-reference release `0x27ca0`.
- Host-element offset `0x10` was checked against the installed machine code. The component rejects other timestamps, image sizes, and accessor instruction prefixes before following private taskbar pointers.
- Both builds pass warnings-as-errors. The original strip measured x=8, width=150, height=32 in its own left column. The subsequent visual refinement narrows it to 120 logical pixels.
- All five native click/desktop/highlight tests pass, invalid requests are ignored, the initial desktop is restored, and the overlay remains hidden.
- Removal restores the original implicit taskbar grid; reattachment works.
- Explorer restart automatically replaces the helper process, reattaches the strip, and reconnects the desktop service. All five click tests passed again afterward.
- The earlier one-button native proof remained visible in a captured Search-open taskbar at display band 6, while the old floating overlay was covered.
- The user visually confirmed the final five-button left-side strip remains visible with Start/Search open. The final timed capture did not record a menu opening; this confirmation is manual.

This remains version-specific integration using undocumented taskbar internals. Other Windows builds must be qualified before adding profiles. Multi-monitor taskbars, auto-hide, alternate taskbar layouts, narrow screens, DPI changes, and theme transitions still need dedicated qualification. No production installer or automatic profile updates are included.

## Maintenance and performance

`src/native_taskbar.c` coordinates native attachment, endpoint validation, state publication, launcher ownership and Explorer replacement. `component.cpp` owns shell UI lifetime. `native-root.h` is the version-specific ABI boundary. `taskbar_protocol.h` is shared by the C helper, C++ component and tests. Desktop operations remain in the existing service module.

Use `scripts/benchmark.ps1 -NativeTaskbar` with the helper stopped. It records a same-length Explorer baseline before launch, then helper and Explorer samples with the native strip attached. It separately checks helper readiness (1 second) and usable native layout/state (2 seconds), retains all binary/Taskbar.dll hashes, and adds provisional incremental Explorer targets of 0.1% of one core and 10 MiB private memory. Repeat three quiet runs and compare medians and spread; shell totals include unrelated activity and the pinned DLL remains resident between runs. Existing helper budgets remain 0.1% CPU, 30 MiB working set and 20 MiB private memory.

The final three observed runs passed these budgets; median helper CPU was below measurement resolution, working set was 19.14 MiB, and native readiness was 113.38 ms. [Full hardening evidence and measurement limits](../../benchmarks/native-hardening-validation.md) include the passing rollback, launcher ownership, reconnect, native and fallback regression tests.

## Research references

- [Microsoft SetWindowsHookEx documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowshookexw).
- [Microsoft XAML diagnostics](https://learn.microsoft.com/en-us/windows/win32/api/xamlom/nf-xamlom-initializexamldiagnosticsex): investigated but unused in the final integration because the connection exposed only a hidden Explorer tree on this machine.
- [Taskbar Virtual Desktop Switcher source](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-vd-switcher.wh.cpp): consulted to understand layout names and private ABI entry points. No mod code or Windhawk integration is included.
