param(
    [switch]$HelperOnly,
    [string]$OutputDirectory = 'build/release'
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$expected = (Get-Content vendor/adapter.sha256).Trim()
if ((Get-FileHash vendor/VirtualDesktopAccessor.dll).Hash -ne $expected) { throw 'Adapter SHA256 mismatch' }
$outputPath = [IO.Path]::GetFullPath((Join-Path $PWD $OutputDirectory))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $PWD 'build')) + [IO.Path]::DirectorySeparatorChar
if (!$outputPath.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'OutputDirectory must be under build/' }
New-Item -ItemType Directory -Force $outputPath | Out-Null
if ($HelperOnly) {
    foreach ($file in 'TaskbarPrototype.exe','DesktopsHelper.TaskbarV4.dll') {
        if (!(Test-Path (Join-Path $outputPath $file))) { throw "Missing $file. Run the full build first." }
    }
} else {
    & cmd /c scripts\build-taskbar-prototype.cmd
    if ($LASTEXITCODE) { throw 'Native taskbar build failed' }
    foreach ($file in 'TaskbarPrototype.exe','DesktopsHelper.TaskbarV4.dll') {
        $source = Join-Path 'build/taskbar-left' $file
        $destination = Join-Path $outputPath $file
        if ((Test-Path $destination) -and (Get-FileHash $source).Hash -eq (Get-FileHash $destination).Hash) { continue }
        try { Copy-Item -LiteralPath $source -Destination $destination -Force }
        catch { throw "Cannot replace $destination. See docs/development.md for the Explorer DLL restart workflow. $($_.Exception.Message)" }
    }
}
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode -mwindows -s src/main.c src/native_taskbar.c src/service.c src/input.c src/diagnostics.c -o "$outputPath/DesktopsHelper.exe" -luser32 -lshell32 -lole32
if ($LASTEXITCODE) { throw 'Build failed' }
Copy-Item vendor/VirtualDesktopAccessor.dll $outputPath
Copy-Item vendor/LICENSE.VirtualDesktopAccessor.txt $outputPath
gcc -std=c11 -O2 -Wall -Wextra -Werror tests/integration.c -o "$outputPath/integration.exe" -luser32 -lole32
if ($LASTEXITCODE) { throw 'Test build failed' }
gcc -std=c11 -O2 -Wall -Wextra -Werror tests/shortcuts.c -o "$outputPath/shortcuts.exe" -luser32
if ($LASTEXITCODE) { throw 'Shortcut test build failed' }
gcc -std=c11 -O2 -Wall -Wextra -Werror tests/health.c -o "$outputPath/health.exe" -luser32
if ($LASTEXITCODE) { throw 'Health test build failed' }
gcc -std=c11 -O2 -Wall -Wextra -Werror tests/topology.c -o "$outputPath/topology.exe" -luser32
if ($LASTEXITCODE) { throw 'Topology test build failed' }
gcc -std=c11 -O2 -Wall -Wextra -Werror tests/input-policy.c -o "$outputPath/input-policy.exe" -luser32
if ($LASTEXITCODE) { throw 'Input test build failed' }
gcc -std=c11 -O2 -Wall -Wextra -Werror tests/lifecycle.c -o "$outputPath/lifecycle.exe" -luser32 -lole32
if ($LASTEXITCODE) { throw 'Lifecycle test build failed' }
Write-Output "Built $outputPath/DesktopsHelper.exe (native taskbar)"
