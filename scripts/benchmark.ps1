param(
    [ValidateRange(10,3600)][int]$Seconds = 60,
    [ValidateRange(1,120)][int]$WarmupSeconds = 5,
    [string]$Output = 'benchmarks/local-idle.json',
    [string]$Executable = 'build/release/DesktopsHelper.exe',
    [switch]$ServiceOnly
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
if (Get-Process DesktopsHelper -ErrorAction SilentlyContinue) { throw 'Exit the helper before benchmarking so startup is measured correctly.' }
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BenchWindow {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, IntPtr w, IntPtr l, uint flags, uint timeout, out UIntPtr result);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h, string name);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
}
'@
$exe = (Resolve-Path $Executable).Path
$shell = $null
$shellBaseline = [Collections.Generic.List[object]]::new()
function Shell-Sample($Process, $Clock) {
    $Process.Refresh()
    if($Process.HasExited){throw 'Explorer exited during native benchmark'}
    [pscustomobject]@{elapsedSeconds=$Clock.Elapsed.TotalSeconds; cpuTotalMs=$Process.TotalProcessorTime.TotalMilliseconds;
        workingSetBytes=$Process.WorkingSet64; privateBytes=$Process.PrivateMemorySize64;
        handles=$Process.HandleCount; threads=$Process.Threads.Count;
        gdiObjects=[BenchWindow]::GetGuiResources($Process.Handle,0); userObjects=[BenchWindow]::GetGuiResources($Process.Handle,1)}
}
if(!$ServiceOnly) {
    [uint32]$shellPid=0
    [void][BenchWindow]::GetWindowThreadProcessId([BenchWindow]::FindWindow('Shell_TrayWnd',$null),[ref]$shellPid)
    $shell=Get-Process -Id $shellPid
    $baselineClock=[Diagnostics.Stopwatch]::StartNew()
    for($i=0;$i -le $Seconds;$i++) {
        $shellBaseline.Add((Shell-Sample $shell $baselineClock))
        if($i -lt $Seconds){Start-Sleep -Seconds 1}
    }
}
$clock = [Diagnostics.Stopwatch]::StartNew()
$launch=@{FilePath=$exe; WindowStyle='Hidden'; PassThru=$true}
$process = Start-Process @launch
$result = [ordered]@{schemaVersion=2; timestampUtc=[DateTime]::UtcNow.ToString('o'); passed=$false}
$failure = $null
$forcedTermination = $false
try {
    $window = [IntPtr]::Zero
    $helperReadyMs=$null
    $nativeReadyMs=$null
    while ($clock.Elapsed.TotalSeconds -lt 15) {
        if ($process.HasExited) { throw "Helper exited with $($process.ExitCode)" }
        $window = [BenchWindow]::FindWindow('DesktopsHelper.Widget', 'Desktops Helper')
        [uint32]$ownerPid = 0
        [void][BenchWindow]::GetWindowThreadProcessId($window, [ref]$ownerPid)
        if ($ownerPid -eq $process.Id -and [BenchWindow]::GetProp($window, 'DesktopsHelper.Ready') -ne [IntPtr]::Zero) {
            if($null -eq $helperReadyMs){$helperReadyMs=$clock.Elapsed.TotalMilliseconds}
            if($ServiceOnly){break}
            $native=[BenchWindow]::FindWindowEx([IntPtr](-3),[IntPtr]::Zero,'DesktopsHelper.Taskbar.Control.v3',$null)
            [UIntPtr]$geometry=[UIntPtr]::Zero
            if($native -ne [IntPtr]::Zero -and [BenchWindow]::GetProp($native,'DesktopsHelper.NativeOwner').ToInt64() -eq $process.Id -and
               [BenchWindow]::GetProp($native,'DesktopsHelper.NativeCurrent') -ne [IntPtr]::Zero -and
               [BenchWindow]::SendMessageTimeout($native,0x8048,[IntPtr]::Zero,[IntPtr]::Zero,2,100,[ref]$geometry) -ne [IntPtr]::Zero -and $geometry.ToUInt64() -ne 0) {
                $nativeReadyMs=$clock.Elapsed.TotalMilliseconds;break
            }
        }
        Start-Sleep -Milliseconds 10
    }
    if ($window -eq [IntPtr]::Zero -or [BenchWindow]::GetProp($window, 'DesktopsHelper.Ready') -eq [IntPtr]::Zero) { throw 'Helper did not become ready in 15 seconds' }
    if(!$ServiceOnly -and $null -eq $nativeReadyMs){throw 'Native strip did not become usable in 15 seconds'}
    $startupMs = $helperReadyMs
    Start-Sleep -Seconds $WarmupSeconds
    $process.Refresh()
    $cpuStart = $process.TotalProcessorTime.TotalMilliseconds
    $samples = [Collections.Generic.List[object]]::new()
    $measurement = [Diagnostics.Stopwatch]::StartNew()
    $shellSamples=[Collections.Generic.List[object]]::new()
    if(!$ServiceOnly){$shellSamples.Add((Shell-Sample $shell $measurement))}
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
        if(!$ServiceOnly){$shellSamples.Add((Shell-Sample $shell $measurement))}
    }
    $elapsedMs = $measurement.Elapsed.TotalMilliseconds
    $cpuMs = $samples[-1].cpuTotalMs - $cpuStart
    $os = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    $workingPeak = ($samples.workingSetBytes | Measure-Object -Maximum).Maximum
    $privatePeak = ($samples.privateBytes | Measure-Object -Maximum).Maximum
    $cpuPercent = 100 * $cpuMs / $elapsedMs
    $result = [ordered]@{
        schemaVersion = 2; timestampUtc = [DateTime]::UtcNow.ToString('o')
        scenario = 'service-only-diagnostic'; osBuild = "$($os.CurrentBuild).$($os.UBR)"
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
    if(!$ServiceOnly) {
        $baselineCpu=100*($shellBaseline[-1].cpuTotalMs-$shellBaseline[0].cpuTotalMs)/(1000*($shellBaseline[-1].elapsedSeconds-$shellBaseline[0].elapsedSeconds))
        $nativeCpu=100*($shellSamples[-1].cpuTotalMs-$shellSamples[0].cpuTotalMs)/(1000*($shellSamples[-1].elapsedSeconds-$shellSamples[0].elapsedSeconds))
        $privateDelta=(($shellSamples.privateBytes | Measure-Object -Average).Average-($shellBaseline.privateBytes | Measure-Object -Average).Average)/1MB
        $result.scenario='paired-native-taskbar'
        $result['nativeDllSha256']=(Get-FileHash (Join-Path (Split-Path $exe) 'DesktopsHelper.TaskbarV4.dll')).Hash
        $result['controllerSha256']=(Get-FileHash (Join-Path (Split-Path $exe) 'TaskbarPrototype.exe')).Hash
        $result['taskbarDllSha256']=(Get-FileHash "$env:WINDIR\System32\Taskbar.dll").Hash
        $result['nativeStartupToReadyMs']=$nativeReadyMs
        $result['explorer']=[ordered]@{pid=$shell.Id; baselineCpuPercent=$baselineCpu; nativeCpuPercent=$nativeCpu;
            incrementalCpuPercent=$nativeCpu-$baselineCpu; incrementalMeanPrivateMiB=$privateDelta;
            baseline=$shellBaseline; native=$shellSamples}
        $result.budgets['nativeStartupToReadyMs']=2000
        $result.budgets['explorerIncrementalCpuPercent']=0.1
        $result.budgets['explorerIncrementalMeanPrivateMiB']=10
        $result['qualificationNote']='Single paired observation; repeat three times quietly and compare medians/spread. Explorer includes unrelated shell activity. Resident DLLs remain loaded between samples.'
        $result.passed=$result.passed -and $nativeReadyMs -le 2000 -and ($nativeCpu-$baselineCpu) -le 0.1 -and $privateDelta -le 10
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
