# Native-only cleanup validation — 2026-09-14

This records the native-default cleanup, not a new release/performance qualification. Current commands belong in [Testing](../docs/testing.md).

## Binary identity

- Helper SHA-256: `F3F6A5C4A9DEEFBEC9F660A53ED0008B5D02592A5611FC6B3205FEC5D212D8C0`.
- Runtime native DLL SHA-256: `F6A4DBCB06C5BE56BF8514D1EE3E8A3A1422DB0E62BA9807A640491DFEF24BFA`.
- Native C++ implementation was unchanged; the existing loaded component was reused for helper regression checks.

## Results and limits

The complete default package built in `build/native-default-validation` with both toolchains and warnings as errors. The final helper was rebuilt in `build/release` with unchanged native binaries. PowerShell syntax, documentation links and `git diff --check` passed.

No-flag startup, service readiness, hidden host, native layout, reconnect, all five real native buttons/highlights, invalid IPC rejection, restored starting desktop, explicit native removal, hidden-host readiness while detached and reattachment passed.

Both combined-suite attempts completed native lifecycle rollback/expiry checks, 100 attach/remove cycles, owner-death cleanup, input-policy checks, adapter exercise and topology. They stopped at the unchanged shortcut focus assertion, so the combined suite is **not passing**. The final report is local; native checks after that failure were run separately rather than marking the combined run successful.

Service fault modes 1–4 passed, including hidden-host checks when native runtime files were absent. The fake-controller deadline and owner-crash tests passed. The benchmark failure test correctly rejected hung shutdown with exit code 2 and retained the failed result.

A ten-second paired benchmark smoke run using the new default also passed and exited cleanly (`local-native-default-smoke.json`). This verifies the default measurement path; it is not a three-run performance baseline.

## Focus comparison

An unchanged helper was built from repository commit `7755bc8` and launched with its original `--native-taskbar` option, then compared with the cleanup helper on the same desktop session using the same shortcut test executable. The baseline hash was `C57F9E8FFB1199981AE18398DB83B6B32628FA8B1B4EA18BD90FA7A45F77A430`.

Both failed destination-window focus assertions while reporting the requested desktop. The baseline reported five failures; the cleanup reported four. Observed foreground classes included `Chrome_WidgetWin_1` and `File Pilot`; one cleanup mismatch was `Qt6111QWindowIcon`. The test also sometimes could not establish its own foreground window.

This reproduces the failure without the cleanup, but does not establish its root cause or prove all focus behavior correct. The test assumes its disposable destination window receives focus, while the service can preserve an already focused destination or pinned application. Investigate test setup/foreground ownership before changing production focus policy. No assertions or product focus policy were relaxed in this cleanup.

Ad-hoc raw reports are `local-reliability*.json`, `local-focus-comparison.json`, `local-failure-tests.json`, `local-native-launcher.json`, and `local-benchmark-rejects-hung-shutdown.json` under this directory. These ignored local files may not exist in other checkouts. Historical committed reports remain unchanged. Three quiet performance samples and manual Explorer/tray/platform qualification were not performed for this revision.
