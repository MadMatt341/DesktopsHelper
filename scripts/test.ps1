param([string]$NativeTestDirectory='build/taskbar-tests', [string]$Output='benchmarks/local-reliability.json')
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
if(Get-Process DesktopsHelper -ErrorAction SilentlyContinue) {throw 'Exit the helper before running tests'}
function Run-Check([string]$Path, [string[]]$Arguments=@()) {
    $output = & $Path @Arguments
    if($LASTEXITCODE) {throw "$Path failed: $output"}
    return $output
}
$inputResult = Run-Check './build/release/input-policy.exe'

$nativeLifecycle=Run-Check (Join-Path $NativeTestDirectory 'NativeTaskbarLifecycle.exe')
$adapterResult = (Run-Check './build/release/integration.exe' @('--exercise')) | ConvertFrom-Json
$launch=@{FilePath=(Resolve-Path build/release/DesktopsHelper.exe).Path; WindowStyle='Hidden'; PassThru=$true}
$process = Start-Process @launch
$report = [ordered]@{
    timestampUtc=[DateTime]::UtcNow.ToString('o')
    exeSha256=(Get-FileHash build/release/DesktopsHelper.exe).Hash
    adapterSha256=(Get-FileHash build/release/VirtualDesktopAccessor.dll).Hash
    inputPolicy=$inputResult; adapter=$adapterResult; nativeLifecycle=$nativeLifecycle
    passed=$false
}
try {
    $deadline=[DateTime]::UtcNow.AddSeconds(10)
    do {
        & ./build/release/health.exe ready | Out-Null
        if($LASTEXITCODE -eq 0){break}
        Start-Sleep -Milliseconds 100
    } while([DateTime]::UtcNow -lt $deadline -and !$process.HasExited)
    if($LASTEXITCODE){throw 'Helper did not become ready'}
    $report['topology']=(Run-Check './build/release/topology.exe' | ConvertFrom-Json)
    Start-Sleep -Seconds 1
    $runs=@()
    for($i=0;$i -lt 10;$i++) { $runs += (Run-Check './build/release/shortcuts.exe' | ConvertFrom-Json) }
    $report['shortcutRuns']=$runs
    Start-Sleep -Seconds 1
    $report['reconnect']=Run-Check './build/release/health.exe' @('reconnect')
    $nativeDeadline=[DateTime]::UtcNow.AddSeconds(5)
    do {
        & ./build/release/TaskbarPrototype.exe status | Out-Null
        if($LASTEXITCODE -eq 0){break}
        Start-Sleep -Milliseconds 25
    } while([DateTime]::UtcNow -lt $nativeDeadline)
    if($LASTEXITCODE){throw 'Native layout not ready after reconnect'}
    $report['nativeDllSha256']=(Get-FileHash build/release/DesktopsHelper.TaskbarV4.dll).Hash
    $report['nativeButtons']=Run-Check (Join-Path $NativeTestDirectory 'NativeTaskbarTest.exe')
    $report['nativeRemove']=Run-Check './build/release/TaskbarPrototype.exe' @('remove')
    $report['detachedHost']=Run-Check './build/release/health.exe' @('ready')
    $report['nativeReattach']=Run-Check './build/release/TaskbarPrototype.exe' @('attach')
    $process.Refresh()
    $priorities=@($process.Threads | ForEach-Object {$_.BasePriority})
    $report['threadBasePriorities']=$priorities
    if($priorities | Where-Object {$_ -gt 8}) {throw 'Unexpected elevated thread priority'}
    ./scripts/stop.ps1
    if(!$process.WaitForExit(5000) -or $process.ExitCode -ne 0) {throw 'Unclean helper shutdown'}
    $report['exitCode']=$process.ExitCode
    $report.passed=$true
} catch {
    $report['failure']=$_.Exception.Message
    throw
} finally {
    if(!$process.HasExited) {./scripts/stop.ps1}
    $report | ConvertTo-Json -Depth 5 | Set-Content $output
    $process.Dispose()
}
Write-Output 'PASS: input policy, adapter, 100 shortcuts, topology, reconnect, native taskbar and hidden host, priorities, clean exit'
