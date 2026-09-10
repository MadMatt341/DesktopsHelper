param(
    [ValidateRange(10,3600)][int]$Seconds = 60,
    [ValidateRange(1,120)][int]$WarmupSeconds = 5,
    [string]$Output = 'benchmarks/local-idle.json',
    [string]$Executable = 'build/release/DesktopsHelper.exe'
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
if (Get-Process DesktopsHelper -ErrorAction SilentlyContinue) { throw 'Exit the helper before benchmarking so startup is measured correctly.' }
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BenchWindow {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h, string name);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
}
'@
$exe = (Resolve-Path $Executable).Path
$clock = [Diagnostics.Stopwatch]::StartNew()
$process = Start-Process -FilePath $exe -WindowStyle Hidden -PassThru
$result = [ordered]@{schemaVersion=2; timestampUtc=[DateTime]::UtcNow.ToString('o'); passed=$false}
$failure = $null
$forcedTermination = $false
try {
    $window = [IntPtr]::Zero
    while ($clock.Elapsed.TotalSeconds -lt 15) {
        if ($process.HasExited) { throw "Helper exited with $($process.ExitCode)" }
        $window = [BenchWindow]::FindWindow('DesktopsHelper.Widget', 'Desktops Helper')
        [uint32]$ownerPid = 0
        [void][BenchWindow]::GetWindowThreadProcessId($window, [ref]$ownerPid)
        if ($ownerPid -eq $process.Id -and [BenchWindow]::GetProp($window, 'DesktopsHelper.Ready') -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 10
    }
    if ($window -eq [IntPtr]::Zero -or [BenchWindow]::GetProp($window, 'DesktopsHelper.Ready') -eq [IntPtr]::Zero) { throw 'Widget did not become ready in 15 seconds' }
    $startupMs = $clock.Elapsed.TotalMilliseconds
    Start-Sleep -Seconds $WarmupSeconds
    $process.Refresh()
    $cpuStart = $process.TotalProcessorTime.TotalMilliseconds
    $samples = [Collections.Generic.List[object]]::new()
    $measurement = [Diagnostics.Stopwatch]::StartNew()
    for ($i = 0; $i -lt $Seconds; $i++) {
        Start-Sleep -Seconds 1
        $process.Refresh()
        if ($process.HasExited) { throw 'Helper exited during measurement' }
        $samples.Add([pscustomobject]@{
            elapsedSeconds = $measurement.Elapsed.TotalSeconds
            cpuTotalMs = $process.TotalProcessorTime.TotalMilliseconds
            workingSetBytes = $process.WorkingSet64
            privateBytes = $process.PrivateMemorySize64
            handles = $process.HandleCount
            threads = $process.Threads.Count
            gdiObjects = [BenchWindow]::GetGuiResources($process.Handle, 0)
            userObjects = [BenchWindow]::GetGuiResources($process.Handle, 1)
        })
    }
    $elapsedMs = $measurement.Elapsed.TotalMilliseconds
    $cpuMs = $samples[-1].cpuTotalMs - $cpuStart
    $os = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    $workingPeak = ($samples.workingSetBytes | Measure-Object -Maximum).Maximum
    $privatePeak = ($samples.privateBytes | Measure-Object -Maximum).Maximum
    $cpuPercent = 100 * $cpuMs / $elapsedMs
    $result = [ordered]@{
        schemaVersion = 2; timestampUtc = [DateTime]::UtcNow.ToString('o')
        scenario = 'idle-visible-widget'; osBuild = "$($os.CurrentBuild).$($os.UBR)"
        logicalProcessors = [Environment]::ProcessorCount
        exeSha256 = (Get-FileHash $exe).Hash
        adapterSha256 = (Get-FileHash (Join-Path (Split-Path $exe) 'VirtualDesktopAccessor.dll')).Hash
        startupToReadyMs = $startupMs; warmupSeconds = $WarmupSeconds
        elapsedSeconds = $elapsedMs / 1000; cpuMs = $cpuMs
        cpuPercentOfOneCore = $cpuPercent
        workingSetPeakMiB = $workingPeak / 1MB; privatePeakMiB = $privatePeak / 1MB
        budgets = @{ cpuPercentOfOneCore = 0.1; workingSetPeakMiB = 30; privatePeakMiB = 20; startupToReadyMs = 1000 }
        passed = ($cpuPercent -le 0.1 -and $workingPeak -le 30MB -and $privatePeak -le 20MB -and $startupMs -le 1000)
        samples = $samples
    }
    if (!$result.passed) { throw "Performance budget exceeded. See $Output" }
} catch {
    $failure = $_.Exception.Message
    $result.passed = $false
} finally {
    if (!$process.HasExited) {
        $window = [BenchWindow]::FindWindow('DesktopsHelper.Widget', 'Desktops Helper')
        [uint32]$ownerPid = 0
        [void][BenchWindow]::GetWindowThreadProcessId($window, [ref]$ownerPid)
        if ($ownerPid -eq $process.Id) { [void][BenchWindow]::PostMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero) }
        if (!$process.WaitForExit(5000)) {
            $forcedTermination = $true
            $process.Kill(); $process.WaitForExit()
        }
    }
    $result['forcedTermination'] = $forcedTermination
    $result['exitCode'] = $process.ExitCode
    if ($forcedTermination -or $process.ExitCode -ne 0) {
        $result.passed = $false
        if (!$failure) { $failure = "Unclean shutdown: exit=$($process.ExitCode), forced=$forcedTermination" }
    }
    $result['failure'] = $failure
    $result | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 $Output
    $process.Dispose()
}
[pscustomobject]$result | Select-Object scenario,startupToReadyMs,cpuPercentOfOneCore,workingSetPeakMiB,privatePeakMiB,exitCode,forcedTermination,passed | Format-List
if (!$result.passed) { throw "Benchmark failed: $failure. See $Output" }
