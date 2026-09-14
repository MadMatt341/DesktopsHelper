# Testing

[Build instructions](development.md) · [Architecture](architecture.md) · [Historical evidence](../benchmarks/README.md)

Run desktop integration checks in an interactive Windows session with Explorer running. The helper must be stopped for tests that create their own instance. Real-input tests temporarily switch desktops, move disposable test windows, and control focus/cursor: leave keyboard and mouse idle. They restore their starting state where documented; adapter exercise may create missing desktops up to five and leave them available.

## Pick checks by change

| Change | Checks |
|---|---|
| Documentation only | Verify links and commands against scripts; `git diff --check` |
| Helper startup, tray, attachment coordinator | No-flag launch; health and native status; reconnect; native launcher fault tests |
| Keyboard or desktop service | Input policy, adapter exercise, shortcuts, topology, service fault tests |
| Native layout, protocol, ownership | Native lifecycle suite, native button test, detach/reattach; load the changed DLL after Explorer restart |
| Adapter build | ABI/hash review, adapter exercise, lifecycle, service faults, complete regression suite |
| Performance-sensitive work / release | Three quiet paired native benchmarks on identical binaries and OS; inspect trends and spread |

## Smoke checks

```powershell
Start-Process ./build/release/DesktopsHelper.exe -WindowStyle Hidden
./build/release/health.exe ready
./build/release/TaskbarPrototype.exe status
./build/release/health.exe reconnect
# Attachment is asynchronous; wait for native status to succeed again.
./build/release/TaskbarPrototype.exe status
```

Health verifies the host is responsive, hidden, and service-ready. Native status separately checks actual taskbar layout. Also inspect the tray menu's reconnect/exit actions. Missing native files must leave a hidden host with usable service/shortcuts rather than a floating window.

## Combined suite

Build the helper normally, then build native test utilities into a separate directory:

```powershell
cmd /c scripts\build-taskbar-prototype.cmd taskbar-tests tests
./scripts/stop.ps1
./scripts/test.ps1
Start-Process ./build/release/DesktopsHelper.exe -WindowStyle Hidden
```

The suite uses the default no-flag native launch. It exercises twelve rollback/expiry stages, 100 attach/remove cycles, abrupt disposable-owner exit, input policy, adapter notifications/movement, topology, 100 shortcuts, reconnect, five real native buttons, invalid IPC, hidden-host operation after native removal, reattachment, thread priorities, and clean exit. The default report is `benchmarks/local-reliability.json`; use `-Output` to retain a deliberately named result. `-NativeTestDirectory` selects a different native test directory.

Test component classes and DLLs are separate from runtime identities. They also remain resident in Explorer: after changing their C++ sources, restart Explorer before rerunning them. Native tests validate the DLL Explorer actually loaded, not merely the newest file on disk.

## Focused and fault checks

With the helper stopped:

```powershell
./build/release/input-policy.exe
./build/release/integration.exe             # Read-only compatibility probe
./build/release/integration.exe --exercise  # Switch and move a disposable window
./build/release/lifecycle.exe               # Adapter subscription cycles
./scripts/test-failures.ps1
./scripts/test-native-launcher.ps1
./scripts/test-benchmark-failure.ps1
```

Service fault tests use isolated fake adapters for failed/transient registration, hung operations, and hung shutdown. Native launcher tests check the controller deadline and owner-crash cleanup without a real component. The benchmark-failure test uses `-ServiceOnly` to isolate shutdown failure; that switch is diagnostic measurement scope, not an application UI mode.

While the helper is running, `shortcuts.exe`, `topology.exe`, and `build/taskbar-tests/NativeTaskbarTest.exe` are available individually. `health.exe reconnect` simulates a shell broadcast; it does not replace Explorer. Actual Explorer restart recovery, tray interactions, full-screen apps, auto-hide, alternate taskbar alignment, multiple monitors, DPI/theme changes, and pinned/elevated windows still require manual qualification.

## Performance

```powershell
./scripts/stop.ps1
./scripts/benchmark.ps1 -Seconds 60 -Output benchmarks/local-native-1.json
```

The default benchmark measures an Explorer baseline for the requested duration, then the native helper and Explorer together for that duration after warmup. Repeat three quiet runs with identical binaries, Windows build, display configuration and power mode. Summarize with explicit paths:

```powershell
./scripts/summarize-native-benchmarks.ps1 -Paths benchmarks/local-native-1.json,benchmarks/local-native-2.json,benchmarks/local-native-3.json -Output benchmarks/local-native-summary.json
```

| Budget | Limit |
|---|---:|
| Helper idle CPU, percent of one logical core | 0.1% |
| Helper sampled working set / private memory | 30 / 20 MiB |
| Helper service readiness / native layout readiness | 1,000 / 2,000 ms |
| Incremental Explorer CPU / mean private memory | 0.1% / 10 MiB |

CPU is process CPU-time delta divided by actual wall time, without dividing by core count. Zero is below accounting resolution, not proof of zero work. Startup is a warm-filesystem observation with sampling granularity. Explorer includes unrelated shell activity, and its component remains resident between samples. Do not silently relax budgets or present a busy sample as controlled idle.

Reports retain hashes, OS identity, raw resource samples, exit code and forced-termination/failure status. A crash or unclean shutdown fails the run even if resource budgets pass. Three-run summaries reject mixed identities and preserve failed runs. Existing retained results qualify only their recorded hashes; a new executable requires new evidence.
