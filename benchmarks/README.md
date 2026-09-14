# Validation evidence

Current commands, test selection and budgets live in [Testing](../docs/testing.md). Read evidence files only when investigating a regression or comparing specific binary hashes. These are recorded observations, not a description of the current implementation.

| Evidence | What it records |
|---|---|
| [Native-only cleanup validation](native-only-cleanup-validation.md) | Default native launch and cleanup checks; pre-existing shortcut focus failure reproduced on the unchanged version |
| [Native architecture review](native-architecture-review.md) | Historical design assessment and tradeoffs |
| [Native hardening validation](native-hardening-validation.md) | 2026-09-14 lifecycle, input, reconnect and performance qualification |
| `reliability-native-validation.json`, `native-launcher-validation.json` | Original native regression reports with binary identity where provided |
| `native-final-1.json` through `native-final-3.json`, `native-final-summary.json` | Three retained paired Explorer/helper samples and summary |
| [Overlay history](overlay-history.md) | Retired overlay baselines, investigations and the preserved local visual-idle measurement |

Native-only cleanup removes overlay fallback and host pinning. The older reports include those historical behaviors and do not qualify the changed helper. Keep new ad-hoc reports in ignored `local-*.json`; retain a named report only when intentionally recording new evidence. Never overwrite a historical baseline just to run a check.
