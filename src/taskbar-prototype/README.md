# Native taskbar internals

Read [architecture](../../docs/architecture.md) for process boundaries, [development](../../docs/development.md) for building and the Explorer restart requirement, and [testing](../../docs/testing.md) for commands. This page owns component-level lifecycle, protocol and compatibility details.

The historical `taskbar-prototype` directory and `TaskbarPrototype.exe` name refer to the current native implementation. Runtime names and window classes are kept stable because the helper, component, scripts and tests share them.

## Source map

| File | Owns |
|---|---|
| `component.cpp` | Bootstrap hook, XAML mount/rollback, endpoint, click/state handlers, owner lifetime |
| `controller.cpp` | Bounded attachment process, status/remove diagnostics, optional Start/Search capture |
| `native-root.h` | Qualified image checks and private Taskbar.dll pointers |
| `shared.h` | Component/controller constants and separate test identities |
| `../taskbar_protocol.h` | Pointer-free message and property contract shared with the C helper |
| `../native_taskbar.c` | Helper-side coordinator, launcher job and deadlines, endpoint validation |

## Mount and lifetime

The controller installs a thread-specific `WH_CALLWNDPROC` hook on the primary taskbar thread and removes the hook after bootstrap delivery. Attachment carries a helper HWND and controller PID, never memory pointers. Deferred mount validates captured helper/controller process handles and its deadline before touching XAML.

A version-checked private entry point exposes the taskbar XAML root. The component finds `RootGrid` under `Taskbar.TaskbarFrame`, adds an auto-width left column, and places the taskbar content in the remaining column. The background spans both; the system-tray outer container is unchanged. Buttons use native toggle semantics, accessible names/tooltips, system brushes, 12-pixel Cascadia Mono digits (Consolas fallback), bold active state and a hover/press surface.

One endpoint reference owns the mounted component; click handlers use weak references. A one-shot wait on the helper process posts cleanup to the taskbar thread when the helper exits. Cleanup drains that callback before destroying the endpoint. It independently revokes handlers, restores original columns/spans, removes added columns, and releases UI objects. Failed mounts destroy their endpoint and permit retry. No file logging, desktop COM operations, or recurring ownership polling runs on Explorer's UI thread.

The DLL remains pinned in Explorer after UI removal. A changed DLL requires an Explorer restart before it can be loaded and tested; a helper restart alone is insufficient.

## Protocol and coordination

The helper retains the hidden top-level class `DesktopsHelper.Widget`; it is never displayed or pinned. The production component uses a message-only endpoint `DesktopsHelper.Taskbar.Control.v3`. Do not rename either independently of all consumers.

Requests are subscribe (0), switch (1–5), and detach (6). The coordinator checks the endpoint class, current Explorer PID, mounted/cancelled state, and owner helper PID before accepting a request. Accepted clicks enter the helper's service queue. State values are unavailable (0), desktop 1–5, or another desktop (6). Publication sends only changed values.

Initial attachment waits for service readiness. Controllers have a five-second attempt limit; the coordinator has a fifteen-second overall deadline and does not start attempts too late to fit. It owns only its controllers in a kill-on-close job, never Explorer. Retiring endpoints are marked cancelled before queued removal so reconnect cannot adopt them again. Reconnect resets desktop subscriptions and then reattaches; a genuine Explorer PID replacement starts a fresh helper to discard stale COM proxies.

Missing/unsupported native components leave the host hidden. The tray reports status and offers reconnect; service readiness separately determines whether keyboard shortcuts work. There is no overlay fallback.

## Compatibility

`native-root.h` accepts the qualified image with PE timestamp `2378140881`, image size `3162112`, and the checked entry-point prefix. The recorded Taskbar.dll SHA-256 is `5DCEEA036939ACB4E43B801A7A63530F328EDAD164741E6529D3D32E9B1C5866` on Windows 11 25H2 `26200.9445`. Runtime guards check the timestamp, size and prefix, not the full hash.

Do not bypass image guards to support a new Windows build. Resolve and qualify a new profile, then run layout, lifecycle and input regressions on that image. `TaskbarSymbols.exe` is an explicit research utility using Microsoft's symbol server with a cache under `build/symbols`; it does not attach to Explorer.

Multi-monitor taskbars, auto-hide, alternate alignment, narrow screens, DPI/theme changes and exclusive fullscreen need dedicated manual qualification. There is no installer or automatic profile update mechanism. Historical [architecture review](../../benchmarks/native-architecture-review.md) and [hardening evidence](../../benchmarks/native-hardening-validation.md) retain the observations and limits of their original revisions.

## Diagnostic details

`TaskbarPrototype.exe status` checks actual native layout geometry, including left-edge placement; exit code 0 indicates success. `remove` detaches the controls, and `attach` attempts a bounded mount. The last attachment HRESULT is stored on the helper as `DesktopsHelper.NativeError`.

`watch-menu` observes foreground process identity for up to 60 seconds, captures only the taskbar strip to `menu-taskbar.bmp` beside the controller, and exits. It is an explicit diagnostic, not background monitoring. Test builds use separate window classes and `DesktopsHelper.TaskbarV4.Tests.dll`; do not ship them.

## Research references

- [Microsoft hook API](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowshookexw).
- [Microsoft XAML diagnostics](https://learn.microsoft.com/en-us/windows/win32/api/xamlom/nf-xamlom-initializexamldiagnosticsex): investigated but unused because it exposed only a hidden Explorer tree on this machine.
- [Taskbar Virtual Desktop Switcher source](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-vd-switcher.wh.cpp): consulted for layout names and private ABI entry points; no Windhawk/mod integration is included.
