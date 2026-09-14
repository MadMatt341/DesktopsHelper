# Native taskbar review — 2026-09-14

This is the initial review, before hardening. See [implementation and validation](native-hardening-validation.md) for the subsequent fixes and measured results; line references below describe the reviewed revision.

Verdict: the process boundary is appropriate and the observed helper footprint is small, but native mode is still a workstation-qualified prototype. Failure recovery and native performance qualification need work before treating it as a maintained release feature. This review did not modify or restart the running implementation.

## Findings, in priority order

1. **P1 — Failed mount can permanently block automatic reattachment.** `component.cpp:123` catches mount failures and calls `Remove()`, but leaves the message window, its owning reference, registered class, and `attaching` flag alive. The controller subsequently sees that window and returns “Already attached,” even though the mounted property is absent. An unexpected existing grid column is a concrete trigger. In addition, a XAML exception during `Remove()` prevents later cleanup, including destruction and resetting the attach flag. Make teardown idempotent and best-effort, with unconditional endpoint destruction/reference release and explicit detached/attaching/mounted states. Validate failures at each mutation step and a successful subsequent retry; happy-path removal does not exercise this.

2. **P2 — The attachment retry limit does not bound the controller lifetime.** `main.c:74` closes its process handle after 15 timer attempts; closing a handle does not stop the child. `controller.cpp:78` can perform fifty one-second synchronous inspections plus sleeps, beyond the helper's retry window. A queued deferred mount can also execute after the helper exits. Use one elapsed-time deadline and a cancellation/owner-lifetime protocol; require a live helper before committing the layout. Validate slow/hung attachment and helper exit during attachment. A controller deadline alone cannot cancel a callback already queued in Explorer.

3. **P2 — Filesystem work runs on Explorer's UI thread.** `component.cpp:24` opens, appends, and closes a log synchronously on each click and inspection. Slow storage or file scanning therefore adds latency to the shell itself. The log is unbounded. Remove routine click/inspection disk logging from the release path, and route exceptional diagnostics to the helper or a bounded asynchronous facility. Preserve enough failure information to diagnose unsupported profiles and rollback failures.

4. **P2 — Existing performance and regression gates do not cover native mode.** `scripts/benchmark.ps1:23` launches without `--native-taskbar`, measures helper readiness rather than usable native controls, and samples only the helper. `scripts/test.ps1` likewise runs overlay mode. The native click test verifies successful operation, but not failed mount rollback, repeated attachment, helper crash, or native overhead. A passing existing baseline cannot establish native-mode compliance.

5. **P2 — Abrupt helper loss leaves stale enabled controls.** The component disables buttons only on state messages. If the helper crashes, it cannot send an unavailable state or removal message; `Request()` silently does nothing when no helper window exists. Observe the helper process lifetime without recurring polling and disable/remove the strip on exit. Include restart/resubscription coverage. The tested Explorer restart path addresses a different process failure.

## Architecture and maintainability

Keep the current boundary: XAML rendering and input forwarding inside Explorer; virtual desktop COM work, keyboard handling, and bounded action submission outside it. Asynchronous messages and event-driven highlighting are good choices. Do not move desktop adapter work into the shell.

Separate the native helper-side coordinator from `main.c`, the UI lifecycle from the private ABI resolver, and diagnostic tools from the release controller. Replace dense one-line lifecycle code and magic request values with named operations and documented ownership invariants. These changes should support the failure fixes, rather than becoming a broad rewrite.

The private resolver deliberately supports one Taskbar.dll profile. Its runtime checks use timestamp, image size, an instruction prefix, and a vtable address; the documented SHA-256 is evidence, not a runtime hash check. This is a compatibility guard, not a guarantee against native crashes. Keep profile data explicit and qualify each new image and expected layout before enabling it. Do not relax the guard to accommodate Windows updates.

Build/release integration is incomplete: MSVC discovery is hard-coded to a Community installation, the regular build does not package the native binaries, and diagnostics/tests are built with the component. Add toolchain discovery, a native build/package option, and matching artifact identities. Update the top-level README to distinguish overlay-only appearance, placement, right-click, and visibility descriptions from native behavior.

## Performance targets and evidence

Existing helper budgets remain unchanged:

| Metric | Budget | Live 30-second observation |
|---|---:|---:|
| CPU, percent of one logical core | ≤0.1% | 0.0% measured |
| Peak sampled working set | ≤30 MiB | 19.57 MiB |
| Peak sampled private memory | ≤20 MiB | 2.84 MiB |
| Launch to helper readiness | ≤1,000 ms | Not measured |

Helper handles and threads were stable. This was an uncontrolled live sample, not a release baseline. Zero measured CPU means below accounting resolution. Full Explorer CPU was 0.103% of one core, with 230.98 MiB working set and 351.00 MiB private memory; those totals include unrelated shell work and must not be attributed to the component. Raw samples and deployed binary hashes are in `local-native-review.json` (ignored local artifact).

Proposed additional native gates, not yet measured or accepted as baselines:

| Metric | Proposed target | Measurement |
|---|---:|---|
| Incremental Explorer idle CPU | ≤0.1% of one core | Matched native-disabled/enabled runs; three runs each, report medians and spread |
| Incremental Explorer private memory | ≤10 MiB | Same warmed shell configurations, separate resident DLL and mounted UI cost |
| Launch to usable native strip | ≤2,000 ms warm | Both native geometry and service readiness; include controller |
| Explorer click handler time | p95 ≤2 ms | Handler entry/exit instrumentation, excluding Windows desktop animation |
| Attachment completion or fallback | ≤15 seconds | Elapsed-time deadline plus deferred-work cancellation checks |
| Attach/remove retention | No monotonic growth | 100 cycles after warmup; handles, UI objects, private-memory trend |

Use a ten-minute quiet run for retention, record helper/controller/component hashes and OS profile, and keep Windows animation latency separate from handler latency. Do not declare compliance from the earlier overlay baselines or this short live sample. Multi-monitor, auto-hide, DPI/theme transitions, and narrow/left-aligned layouts remain separate compatibility qualification items.

Suggested implementation order: transactional lifecycle and owner loss; bounded attachment; remove shell-thread disk I/O; native test/performance gates; then module and build cleanup.
