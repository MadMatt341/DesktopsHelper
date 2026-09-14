param(
    [string[]]$Paths=@('benchmarks/native-final-1.json','benchmarks/native-final-2.json','benchmarks/native-final-3.json'),
    [string]$Output='benchmarks/native-final-summary.json'
)
$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
if($Paths.Count -lt 3){throw 'Use at least three paired runs'}
$runs=@($Paths | ForEach-Object {Get-Content $_ -Raw | ConvertFrom-Json})
foreach($identity in @('exeSha256','nativeDllSha256','controllerSha256','taskbarDllSha256','osBuild')) {
    $values=@($runs | ForEach-Object {$_.$identity} | Sort-Object -Unique)
    if($values.Count -ne 1 -or !$values[0]){throw "Mixed or missing $identity; do not combine different builds"}
}
if($runs | Where-Object {$_.scenario -ne 'paired-native-taskbar' -or $_.elapsedSeconds -lt 60}) {
    throw 'Each paired run must contain at least sixty seconds of native sampling'
}
function Summary([double[]]$Values,[double]$Budget) {
    $sorted=@($Values | Sort-Object)
    $middle=[int][Math]::Floor($sorted.Count/2)
    $median=if($sorted.Count%2){$sorted[$middle]}else{($sorted[$middle-1]+$sorted[$middle])/2}
    [ordered]@{median=$median; minimum=$sorted[0]; maximum=$sorted[-1]; budget=$Budget;
        medianPassed=$median -le $Budget; allSamplesPassed=@($Values | Where-Object {$_ -gt $Budget}).Count -eq 0}
}
$metrics=[ordered]@{
    helperCpuPercent=Summary @($runs.cpuPercentOfOneCore) 0.1
    helperWorkingMiB=Summary @($runs.workingSetPeakMiB) 30
    helperPrivateMiB=Summary @($runs.privatePeakMiB) 20
    helperReadyMs=Summary @($runs.startupToReadyMs) 1000
    nativeReadyMs=Summary @($runs.nativeStartupToReadyMs) 2000
    explorerIncrementalCpuPercent=Summary @($runs | ForEach-Object {$_.explorer.incrementalCpuPercent}) 0.1
    explorerIncrementalMeanPrivateMiB=Summary @($runs | ForEach-Object {$_.explorer.incrementalMeanPrivateMiB}) 10
}
$result=[ordered]@{
    timestampUtc=[DateTime]::UtcNow.ToString('o')
    exeSha256=$runs[0].exeSha256; nativeDllSha256=$runs[0].nativeDllSha256
    controllerSha256=$runs[0].controllerSha256; taskbarDllSha256=$runs[0].taskbarDllSha256
    osBuild=$runs[0].osBuild; runFiles=$Paths; metrics=$metrics
    allRunsPassed=@($runs | Where-Object {!$_.passed -or $_.forcedTermination -or $_.exitCode -ne 0}).Count -eq 0
    note='Observed paired runs on this workstation. Explorer contains unrelated shell activity; negative differences do not imply a resource saving. Pinned DLLs were resident in the baseline. This is not a cold-start or universal compatibility qualification.'
}
$result | ConvertTo-Json -Depth 6 | Set-Content $Output
$result.metrics | ConvertTo-Json -Depth 4
if(!$result.allRunsPassed){throw "At least one run failed; retained in $Output"}
