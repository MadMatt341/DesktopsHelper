# VirtualDesktopAccessor provenance

- Upstream: https://github.com/Ciantic/VirtualDesktopAccessor
- Source commit: `8172097993b1194e3d5e2ff38421ebb06f867b6c`
- Source archive: `adapter-source.zip`, SHA256 `6DB729B361C07C718E198390EF0AB1BD7D51E2AA810F30D19B3F3E2BDD022058`
- Binary: `VirtualDesktopAccessor.dll`, SHA256 in `adapter.sha256`
- License: MIT, included as `LICENSE.VirtualDesktopAccessor.txt`
- Local changes: `scripts/patch-adapter.py`, applied to a fresh extraction by `scripts/rebuild-adapter.ps1`.
- Build: explicit `x86_64-pc-windows-msvc` target, `--release --locked`, `RUSTFLAGS=-C target-feature=+crt-static`. The source includes Cargo.lock. The C runtime is statically linked; the resulting import table has no `VCRUNTIME140.dll` dependency.

The patch removes the listener's three-second polling and time-critical priority, waits for actual registration success, forwards desktop-topology invalidations, and releases listener locks before shutdown joins. It adds host ABI version 2, worker-thread COM cache reset, and a desktop-reorder export used by the topology regression test. Event messages are invalidations; consumers query the current desktop rather than relying on message parameters for old/new indices.

The release DLL dated 2024-12-16 was tested and rejected for the current Windows build. The included binary is built from the above source commit plus local patches. A local rebuild may have a different binary hash due to compiler/toolchain/path differences; the source archive and dependency lockfile remain fixed. Review the updated hash and rerun tests after rebuilding.
