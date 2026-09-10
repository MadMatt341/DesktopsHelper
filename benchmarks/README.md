# Performance contract

Current event-driven build (`baseline-event-driven-final.json`, schema 2): **128.2 ms startup**, **19.30 MiB peak working set**, **2.71 MiB peak private memory**, and **0 ms measured CPU-time increase over the two-minute sample**. All resource budgets passed, exit code was 0, and no forced termination was required. This remains a single warm-start sample; the earlier files below retain the development history and their respective hashes.

First retained baseline (`baseline-idle.json`, Windows 11 26200.9445): **151.6 ms startup**, **21.34 MiB peak working set**, **2.87 MiB peak private memory**, **0 ms CPU-time increase over 60.7 seconds**. All four budgets passed. This is one warm-start sample, not a multi-run median.

After the taskbar visibility fix (`baseline-visibility-fix.json`): **149.4 ms startup**, **19.30 MiB peak working set**, **2.69 MiB peak private memory**, **0.077% of one core** over 60.7 seconds. All budgets passed. The older shortcut stress results and `validation-summary.json` describe the earlier executable hash; the visibility-fix baseline identifies the updated executable separately.

Measure the entire helper process, including its native adapter and keyboard thread. No working-set trimming or forced garbage collection is used. The PowerShell sampler runs in a separate process and is excluded from helper CPU and memory totals.

| Metric | Initial budget |
|---|---:|
| Idle CPU, percent of one logical core | ≤ 0.1% |
| Peak sampled working set | ≤ 30 MiB |
| Peak sampled private committed bytes | ≤ 20 MiB |
| Process launch to widget and shortcuts ready | ≤ 1,000 ms |

`scripts/benchmark.ps1` waits for the widget's explicit ready property, warms up for five seconds, and samples once a second for 60 seconds by default. CPU is process CPU-time delta / actual wall time; it is **not** divided by the number of CPUs. Zero means below Windows' CPU accounting resolution, not proof of zero work. Startup includes launching from PowerShell and has approximately 10 ms sampling granularity; it is a warm-filesystem observation, not a cold-boot promise.

Each JSON result keeps timestamps, OS build, executable/DLL hashes, duration, budgets, pass/fail, and all raw samples: CPU, working set, private bytes, handles, threads, GDI objects, and USER objects. The script exits with an error if a budget is exceeded and closes only its own test instance. Retain baselines by name; `local-*.json` is excluded from Git for ad-hoc measurements.

Schema 2 saves results after shutdown, recording `exitCode`, `forcedTermination`, and `failure`. A crash, nonzero exit, or forced termination fails the run even if every resource budget passed. `test-benchmark-failure.ps1` proves this using an isolated fake adapter; its intentionally failing output is `benchmark-rejects-hung-shutdown.json`, not a performance baseline.

`test-failures.ps1` verifies UI responsiveness and readiness for failed registration, a hung desktop operation, a hung unsubscribe, and transient registration failures followed by recovery. It also simulates Explorer's restart broadcast to the helper; it does not kill the user's Explorer process. `lifecycle.exe` performs 100 subscription/unsubscription cycles during 40 switches, using two temporary desktops and restoring the initial desktop/count. `topology.exe` verifies reordering and deleting a temporary desktop before the active one, and removes its two test desktops. `input-policy.exe` checks nine modifier/repeat cases, including both Win keys and pass-through when the service cannot accept work, without injecting real keyboard input.

`reliability-validation.json` records the passing combined suite for the executable identified in the current baseline. Topology runs before rapid synthetic shortcuts, with focus-settling between scenarios. Earlier combined attempts saw Windows return to another desktop during topology setup; the indicator matched the actual desktop, but the expected test desktop was not active. The assertions still require the expected desktop and matching indicator. Keep the keyboard/mouse idle during these tests; isolated Windows transition failures should be investigated with the emitted actual/expected state, not treated as successful runs.

For a release comparison, run three idle samples on the same machine, power mode, display setup, and Windows build, with no desktop switching or typing during the idle interval. Compare medians and inspect handle/GDI/private-memory trends. Use `-Seconds 600` for a longer leak observation. Budgets are initial engineering targets; don't silently raise them to make a run pass.

`integration.exe --exercise` reports five switch-to-notification measurements and five move-and-verification measurements. Switching includes delivery of the desktop-change notification with a 10 ms pump interval, but not the end of Windows' visual animation. A destination that is already current is a no-op and shouldn't be used as a switch-latency sample. `shortcuts.exe` verifies all ten actual input combinations, the widget's active-number state, and that moving leaves the current desktop unchanged. Retain stress runs alongside idle baselines. These small samples are regression evidence, not statistically stable p95 claims.

Before release, manually check: click behavior; bold indicator after Task View and Ctrl+Win+arrow changes; left/right Win keys; key repeat; plain numbers and unrelated Win shortcuts; Start-menu suppression; foreground focus after switching; pinned/elevated apps; Explorer restart; auto-hide and left-aligned taskbar; DPI changes; full-screen apps; clean exit and restored native shortcuts.

`visibility.exe --exercise` brings the taskbar forward without activating it, waits 500 ms for the event-driven correction, and checks that the indicator is visible, not minimized or cloaked, and is the window hit at its center. This catches taskbar occlusion that an isolated window screenshot cannot detect. Run with the default taskbar placement and an unobstructed desktop.

Fullscreen visibility: `baseline-fullscreen.json` retains a one-minute idle measurement after adding foreground geometry/lifecycle event subscriptions. `fullscreen.exe` checks windowed/maximized visibility and fullscreen entry, exit, reentry, minimize, restore, and close using a disposable window. The earlier reliability report applies to its recorded executable hash; it is not a rerun of the full suite for this change. Exclusive fullscreen games remain a manual qualification item.
The initial fullscreen sample (`baseline-fullscreen.json`) failed the CPU budget at 0.204% of one core. After restricting geometry/lifecycle subscriptions to the foreground thread, `baseline-fullscreen-filtered.json` passed at 0.0244% CPU, 19.39 MiB peak working set, 2.75 MiB private memory, and exit code 0. Both samples retain their executable hashes; these are single-run observations, not a statistical comparison.
Visual refresh measurements: `baseline-visual-refresh.json` and `baseline-visual-refresh-final.json` exceeded the CPU budget during concurrent user activity. Their scenario field comes from the idle benchmark harness, but these were not controlled idle runs and must not be used as idle baselines. A short diagnostic build recorded two paints and no measured CPU time; that does not qualify the final visual build. Repeat controlled idle qualification when the computer is available. The final visual change uses 13-pixel Consolas digits; fullscreen and taskbar visibility checks passed before this font-only adjustment.
