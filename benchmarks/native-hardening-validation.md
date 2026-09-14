# Native hardening — 2026-09-14

The native component now rolls back partial setup, removes its controls when the helper dies, and performs no file logging on Explorer's UI thread. A separate C coordinator owns attachment, process deadlines, endpoint validation and state publication. The concurrent visual refinement is preserved in the V4 component.

## Changes

- Each mount mutation can fail without preventing subsequent cleanup steps. Failed setup destroys the message endpoint, releases its owning reference, clears attachment state and allows retry. Click handlers use weak ownership; an endpoint holds one strong reference until destruction.
- The component observes the helper's process handle with a one-shot wait. Its callback only posts to the taskbar thread; cleanup drains that callback before destroying the endpoint. No recurring owner polling is needed.
- Controllers have five-second attempt deadlines. Deferred setup checks controller and helper lifetimes plus expiry. The helper owns its controllers in a kill-on-close job, enforces a fifteen-second overall deadline and starts no attempt too late to fit. Explorer is never assigned to this job.
- State publication ignores unchanged values. The overlay's global visibility subscriptions are suspended while native controls are mounted, and restored when fallback is needed.
- Reconnect exposed another issue: Windows cannot resolve the hidden helper's desktop view for pinning. Reconnect now retires the native endpoint, shows fallback while the desktop service reconnects, then reattaches after readiness. Initial native attachment also waits for readiness. A retiring endpoint is marked so it cannot be adopted again before queued removal completes.
- Release builds discover Visual Studio through vswhere, package native binaries through `build.ps1 -NativeTaskbar`, and exclude test/fault/symbol utilities by default. Documentation distinguishes native and overlay behavior.

## Verification

Both native C++ and helper C builds compile with warnings treated as errors. `git diff --check` passes.

`reliability-native-validation.json` records the final helper/component hashes and a passing suite:

- Eleven injected mount failures and one expired deferred request; every failure leaves no endpoint and permits a successful remount.
- 100 attach/remove cycles and abrupt disposable-owner exit cleanup.
- Nine input-policy cases, adapter switching/movement notifications, topology changes with desktop count restored, and 100 real shortcut combinations.
- Reconnect, all five real native buttons, desktop highlighting, invalid request rejection, original desktop restoration, hidden fallback, and suspended overlay subscriptions.
- Native removal followed by fallback visibility and eight fullscreen transitions, reattachment, normal thread priorities, and clean exit.

`native-launcher-validation.json` separately verifies a deliberately hung controller is terminated at the overall deadline (14.97 seconds observed), and that an abrupt helper exit terminates its controller (4.96 ms observed). Only disposable test processes are forcibly ended. The tests retain the helper as responsive during the launcher timeout.

The earlier failing reconnect result is retained in `native-reconnect-before-fix.json`; its 100 shortcut checks passed, but the reconnect did not. It is not counted as a passing suite.

## Performance

Final paired measurements are recorded in `native-final-1.json` through `native-final-3.json`; the compatible-run summary is `native-final-summary.json`. Each run samples Explorer without the helper for sixty seconds, then measures helper and Explorer for sixty seconds after a five-second warmup. Helper readiness and usable native geometry/state are timed separately. Raw CPU, memory, handle/thread and GUI-resource samples and artifact hashes are retained.

The initial `local-native-hardened-*` observations were taken before suspending overlay subscriptions and while the visual task was also building/deploying/testing. They included CPU-budget failures and are not idle qualification evidence. Their source build and runtime conditions differ from the final runs.

All three final runs passed every recorded resource/startup budget and exited cleanly. The summarizer verified matching helper, component, controller, Taskbar.dll and OS identities. Budgets have not been raised.

| Metric | Median | Observed range | Budget |
|---|---:|---:|---:|
| Helper CPU, percent of one core | 0.000% | 0.000–0.000% | ≤0.1% |
| Helper peak sampled working set | 19.14 MiB | 19.13–19.19 MiB | ≤30 MiB |
| Helper peak sampled private memory | 2.60 MiB | 2.59–2.66 MiB | ≤20 MiB |
| Helper readiness | 77.22 ms | 77.21–77.68 ms | ≤1,000 ms |
| Usable native strip | 113.38 ms | 113.17–113.40 ms | ≤2,000 ms |
| Explorer CPU difference, percent of one core | −0.052% | −0.129–0.077% | ≤0.1% |
| Explorer mean private-memory difference | −0.13 MiB | −2.02–0.08 MiB | ≤10 MiB |

Zero measured CPU means below accounting resolution. The negative Explorer differences reflect sampling/background variability, not a claimed optimization of unrelated shell work. See the raw samples and caveats below.

## Remaining qualification limits

The private Taskbar.dll profile remains specific to the verified Windows image. Other builds, multi-monitor taskbars, auto-hide, DPI/theme changes and narrow layouts still need dedicated qualification. The component and its window class remain loaded until Explorer exits.

Paired Explorer measurements include unrelated shell work; a negative difference does not demonstrate a saving. The DLL was already resident during the baseline, so these runs measure mounted-UI overhead rather than cold DLL-loading cost. This suite does not establish a long-duration leak bound or a statistically qualified click-handler p95. The proposed ten-minute retention and two-millisecond handler targets remain separate qualification work, not asserted passes.
