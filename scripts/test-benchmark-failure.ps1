$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
if(Get-Process DesktopsHelper -ErrorAction SilentlyContinue) {throw 'Exit the helper first'}
New-Item -ItemType Directory -Force build/fault-3 | Out-Null
Copy-Item build/release/DesktopsHelper.exe build/fault-3/
gcc -shared -O2 -DFAULT_MODE=3 tests/fake-adapter.c -o build/fault-3/VirtualDesktopAccessor.dll
if($LASTEXITCODE){throw 'Fake adapter build failed'}
$rejected=$false
try {
    ./scripts/benchmark.ps1 -Seconds 10 -WarmupSeconds 1 -Executable build/fault-3/DesktopsHelper.exe -Output benchmarks/benchmark-rejects-hung-shutdown.json
} catch { $rejected=$true }
$result=Get-Content benchmarks/benchmark-rejects-hung-shutdown.json -Raw | ConvertFrom-Json
if(!$rejected -or $result.passed -or $result.exitCode -ne 2) {throw 'Benchmark accepted an unclean shutdown'}
Write-Output 'PASS: benchmark rejected hung shutdown and retained the failure result'
