$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
if(Get-Process DesktopsHelper -ErrorAction SilentlyContinue) { throw 'Exit the helper before fault tests' }
$results = @()
foreach($mode in 1,2,3,4) {
    $directory = "build/fault-$mode"
    New-Item -ItemType Directory -Force $directory | Out-Null
    gcc -shared -O2 -Wall -Wextra -Werror "-DFAULT_MODE=$mode" tests/fake-adapter.c -o "$directory/VirtualDesktopAccessor.dll"
    if($LASTEXITCODE) {throw 'Fake adapter build failed'}
    Copy-Item build/release/DesktopsHelper.exe $directory
    $process = Start-Process (Join-Path (Resolve-Path $directory).Path 'DesktopsHelper.exe') -WindowStyle Hidden -PassThru
    try {
        Start-Sleep -Seconds 6
        $expected = if($mode -ge 3) {'ready'} else {'unavailable'}
        $health = & ./build/release/health.exe $expected
        if($LASTEXITCODE) {throw "Fault $mode health failed: $health"}
        if($mode -eq 4) {
            ./build/release/health.exe reconnect
            if($LASTEXITCODE) {throw 'Reconnect event failed'}
        }
        ./scripts/stop.ps1
        if(!$process.WaitForExit(5000)) {throw "Fault $mode process did not exit"}
        $expectedExit = if($mode -eq 1 -or $mode -eq 4) {0} else {2}
        if($process.ExitCode -ne $expectedExit) {throw "Fault $mode exit code $($process.ExitCode), expected $expectedExit"}
        $results += [pscustomobject]@{mode=$mode;health=($health|ConvertFrom-Json);exitCode=$process.ExitCode;passed=$true}
    } finally {
        if(!$process.HasExited) {$process.Kill();$process.WaitForExit()}
        $process.Dispose()
    }
}
$results | ConvertTo-Json -Depth 4 | Set-Content benchmarks/failure-tests.json
$results | Format-Table
