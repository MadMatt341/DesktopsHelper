param([switch]$NativeTaskbar, [string]$NativeTestDirectory='build/taskbar-left')
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
if(Get-Process DesktopsHelper -ErrorAction SilentlyContinue) {throw 'Exit the helper before running tests'}
function Run-Check([string]$Path, [string[]]$Arguments=@()) {
    $output = & $Path @Arguments
    if($LASTEXITCODE) {throw "$Path failed: $output"}
    return $output
}
$inputResult = Run-Check './build/release/input-policy.exe'
$nativeLifecycle=$null
if($NativeTaskbar){$nativeLifecycle=Run-Check (Join-Path $NativeTestDirectory 'NativeTaskbarLifecycle.exe')}
$adapterResult = (Run-Check './build/release/integration.exe' @('--exercise')) | ConvertFrom-Json
$launch=@{FilePath=(Resolve-Path build/release/DesktopsHelper.exe).Path; WindowStyle='Hidden'; PassThru=$true}
if($NativeTaskbar){$launch.ArgumentList='--native-taskbar'}
$process = Start-Process @launch
$report = [ordered]@{
    timestampUtc=[DateTime]::UtcNow.ToString('o')
    exeSha256=(Get-FileHash build/release/DesktopsHelper.exe).Hash
    adapterSha256=(Get-FileHash build/release/VirtualDesktopAccessor.dll).Hash
    inputPolicy=$inputResult; adapter=$adapterResult
    passed=$false
}
try {
    $deadline=[DateTime]::UtcNow.AddSeconds(10)
    do {
        & ./build/release/health.exe ready | Out-Null
        if($LASTEXITCODE -eq 0){break}
        Start-Sleep -Milliseconds 100
    } while([DateTime]::UtcNow -lt $deadline -and !$process.HasExited)
    if($LASTEXITCODE){throw 'Widget did not become ready'}
    $report['topology']=(Run-Check './build/release/topology.exe' | ConvertFrom-Json)
    Start-Sleep -Seconds 1
    $runs=@()
    for($i=0;$i -lt 10;$i++) { $runs += (Run-Check './build/release/shortcuts.exe' | ConvertFrom-Json) }
    $report['shortcutRuns']=$runs
    Start-Sleep -Seconds 1
    $report['reconnect']=Run-Check './build/release/health.exe' @('reconnect')
    if($NativeTaskbar) {
        $nativeDeadline=[DateTime]::UtcNow.AddSeconds(5)
        do {
            & ./build/release/TaskbarPrototype.exe status | Out-Null
            if($LASTEXITCODE -eq 0){break}
            Start-Sleep -Milliseconds 25
        } while([DateTime]::UtcNow -lt $nativeDeadline)
        if($LASTEXITCODE){throw 'Native layout not ready after reconnect'}
        $report['nativeDllSha256']=(Get-FileHash build/release/DesktopsHelper.TaskbarV4.dll).Hash
        $report['nativeLifecycle']=$nativeLifecycle
        $report['nativeButtons']=Run-Check (Join-Path $NativeTestDirectory 'NativeTaskbarTest.exe')
        $report['nativeRemove']=Run-Check './build/release/TaskbarPrototype.exe' @('remove')
        $report['nativeFallbackVisibility']=Run-Check './build/release/visibility.exe' @('--exercise')
        $report['nativeFallbackFullscreen']=Run-Check './build/release/fullscreen.exe'
        $report['nativeReattach']=Run-Check './build/release/TaskbarPrototype.exe' @('attach')
    } else {
        $report['visibility']=Run-Check './build/release/visibility.exe' @('--exercise')
        $report['fullscreen']=Run-Check './build/release/fullscreen.exe'
    }
    $process.Refresh()
    $priorities=@($process.Threads | ForEach-Object {$_.BasePriority})
    $report['threadBasePriorities']=$priorities
    if($priorities | Where-Object {$_ -gt 8}) {throw 'Unexpected elevated thread priority'}
    ./scripts/stop.ps1
    if(!$process.WaitForExit(5000) -or $process.ExitCode -ne 0) {throw 'Unclean widget shutdown'}
    $report['exitCode']=$process.ExitCode
    $report.passed=$true
} finally {
    if(!$process.HasExited) {./scripts/stop.ps1}
    $output=if($NativeTaskbar){'benchmarks/reliability-native-validation.json'}else{'benchmarks/reliability-validation.json'}
    $report | ConvertTo-Json -Depth 5 | Set-Content $output
    $process.Dispose()
}
Write-Output 'PASS: input policy, adapter, 100 shortcuts, topology, reconnect, selected UI mode, priorities, clean exit'
