# Architecture

[Product overview](../README.md) · [Build and restart](development.md) · [Testing](testing.md)

The application separates desktop operations from the shell UI. Explorer owns the five native buttons; a helper process owns their behavior. A short-lived controller connects them at startup. Native taskbar buttons are the only visual interface, with a tray icon for status, reconnect, and exit.

## Responsibilities

| Part | Responsibility | Read next for changes |
|---|---|---|
| Helper host | Hidden top-level window, tray menu, service deadlines, reconnect, single instance | `src/main.c` |
| Keyboard thread | Win+1–5 / Shift+Win+1–5 interception; nonblocking command submission | `src/input.c` |
| Service worker | Desktop COM calls, command queue, notifications, destination focus | `src/service.c`, `src/service.h` |
| Native coordinator | Validate endpoints, publish changed state, own controller processes, handle Explorer replacement | `src/native_taskbar.c` |
| Controller and Explorer component | Attach controls, reserve layout space, handle native appearance and UI lifetime | [Component internals](../src/taskbar-prototype/README.md) |
| Adapter | Patched VirtualDesktopAccessor, loaded only in the helper/service tests | [Provenance and rebuild](../vendor/README.md) |
| Diagnostics | Best-effort crash code/module/phase logging | `src/diagnostics.c` |

## Startup and state flow

The main thread acquires the session mutex and creates a hidden top-level host. It must receive Explorer's `TaskbarCreated` broadcast, so it is not a message-only window. The historical `DesktopsHelper.Widget` class is a stable discovery identity used by the component and tests; the host is never shown or pinned to virtual desktops.

The keyboard thread installs its hook. The service worker initializes COM, validates the adapter ABI, queries desktop state, and subscribes to notifications. Once the service reports ready, the coordinator launches native attachment. The controller bootstraps a component on Explorer's taskbar thread and exits. Subsequent clicks travel through validated window messages to the service queue; desktop notifications publish the selected button back to Explorer.

Readiness has two meanings: `health.exe ready` checks the responsive hidden host and desktop service; `TaskbarPrototype.exe status` checks native layout. Successful service readiness alone does not prove the buttons attached.

## Failure and recovery

Native attachment has a 15-second overall budget and five-second controller attempts. Missing binaries or an unsupported Taskbar.dll leave the tray available and the host hidden. Keyboard shortcuts continue while the desktop service is ready. The tray tooltip reports unavailable buttons; Reconnect retries both the service and native attachment. There is no overlay fallback.

A service timeout releases new shortcut combinations to Windows and disables native buttons. Connection failures get up to five delayed retries. A pending operation has a five-second deadline; a hung worker requires restarting the helper. Reconnect detaches the old endpoint before resetting the service. A genuine Explorer PID change starts a replacement helper that waits for the old process to exit, discarding stale COM subscriptions.

Shutdown stops input, unsubscribes service notifications, and removes native controls. If a worker cannot stop within two seconds, exit code 2 bypasses DLL detach to avoid loader-lock deadlock. The Explorer component remains loaded even after removing its controls; see the [restart boundary](development.md#changing-the-explorer-component).

## Constraints to preserve

Input callbacks never wait for desktop COM. Commands use a bounded queue; move requests retain the original HWND/PID and reject stale targets. Explorer performs UI work only. Private desktop APIs stay in the helper process.

Idle highlighting and ownership are event-driven. Timers exist only for bounded attachment/retry/deadline work and up to 1.5 seconds of focus settling after switching. A silently lost subscription without a shell restart or API error still needs manual reconnect.

The worker may activate the destination's frontmost eligible window after Explorer settles, or the desktop shell if there is none. It preserves an already focused destination/pinned window and does not restore minimized applications. Pinned/elevated windows and application restrictions can prevent movement.

Undocumented desktop and taskbar interfaces are version-sensitive. One historical COM access violation remains unconfirmed; thread separation does not provide process crash isolation. Supported image checks and remaining platform qualification belong in the [component guide](../src/taskbar-prototype/README.md), and observations belong in [benchmark evidence](../benchmarks/README.md).

Crash diagnostics append UTC time, exception code, module/offset and operation phase to `%LOCALAPPDATA%\DesktopsHelper\crash.log`. Phases are connect (10), action (11), refresh (12), and shutdown (20). The log contains no window titles, keystrokes or memory dumps. Native crashes still terminate the helper; reproduce under a debugger if one recurs.
