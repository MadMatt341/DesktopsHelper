# Build and development

[Overview](../README.md) · [Architecture](architecture.md) · [Testing](testing.md)

Run commands from the repository root in PowerShell. Use a normal interactive Windows session for launching and testing; a restricted automation sandbox may not access the desktop service. Administrator rights are not an application requirement.

## Toolchains and outputs

The helper uses x64 MinGW-w64 GCC on PATH, C11, `-O2 -Wall -Wextra -Werror`. The native component/controller use Visual Studio C++ tools, the Windows SDK/C++/WinRT headers, C++20, `/O2 /W4 /WX /MT`. The build discovers Visual Studio through its installed `vswhere.exe`. Both toolchains are required for the normal build.

```powershell
./scripts/build.ps1
Start-Process ./build/release/DesktopsHelper.exe -WindowStyle Hidden
./build/release/health.exe ready
./build/release/TaskbarPrototype.exe status
```

The default build always includes native taskbar support. No launch flag is needed. Existing shortcuts with `--native-taskbar` still work; `--above-taskbar` no longer selects an alternate UI. Sign-in registration is external to the application.

Keep these runtime files together: `DesktopsHelper.exe`, `VirtualDesktopAccessor.dll`, `TaskbarPrototype.exe`, `DesktopsHelper.TaskbarV4.dll`, and the bundled adapter license. The build also produces helper regression executables. Native fault/test/symbol utilities require a separate explicit build and must not be packaged as runtime files.

The checked-in adapter is hash-verified before every build. Rust/Python are needed only when [rebuilding that adapter](../vendor/README.md), not for ordinary application work. Runtime binaries statically link their C runtimes; no .NET runtime or separate VC++ redistributable is required by this build. Binaries remain unsigned, and clean-machine qualification remains outstanding.

## Changing only helper C code

When the existing native binaries match the unchanged native sources, reuse them:

```powershell
./scripts/stop.ps1
./scripts/build.ps1 -HelperOnly
Start-Process ./build/release/DesktopsHelper.exe -WindowStyle Hidden
./build/release/health.exe ready
./build/release/TaskbarPrototype.exe status
```

`-HelperOnly` requires both native runtime files to exist and deliberately does not rebuild them. It is a development shortcut, not an overlay build. Do not use it after changing the component, controller, shared headers, or native compiler settings. Rebuild and test the complete package for release.

## Changing the Explorer component

Explorer pins the component DLL until Explorer exits. Stopping the helper removes controls but does not unload that DLL. Replacing a loaded DLL may fail, and launching a second binary path does not prove Explorer loaded the new component. Check the loaded module identity when validating native C++ changes.

Compile a candidate without overwriting the active package:

```powershell
./scripts/build.ps1 -OutputDirectory build/candidate
```

Output directories must be inside `build/`. The native compiler stages its output under `build/taskbar-left/`; the full build copies it into the selected package directory. Keep that staging directory out of live launches so subsequent builds do not lock compiler outputs.

To load a changed component: stop the helper, restart **Windows Explorer** from Task Manager (this briefly resets the taskbar and can affect Explorer windows), then launch the candidate helper. Stop the helper before the Explorer restart so its automatic recovery does not immediately reload the previous DLL. Once validated, stop the candidate and restart Explorer again before replacing the release DLL and launching the release helper. Do not automatically kill Explorer as part of a normal build.

For helper-only changes, this Explorer restart is unnecessary. If a full build reports a locked destination, use the candidate workflow or the helper-only workflow as appropriate; do not assume a partially copied directory is a tested release.

## Where to work

Use [architecture](architecture.md) to locate responsibility, then read the relevant source. Native XAML/protocol/version details live [beside that component](../src/taskbar-prototype/README.md). The historical `taskbar-prototype` / `TaskbarPrototype.exe` / `TaskbarV4` names remain file and protocol identities; they are the production native path, not a second implementation.

Choose checks from [testing](testing.md). Keep ad-hoc reports under `benchmarks/local-*.json`. Document current behavior at the owning level, preserve old measurements as historical evidence, and link instead of duplicating long implementation explanations in README or AGENTS.md.
