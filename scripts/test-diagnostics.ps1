$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$directory=Join-Path $PWD ('build/diagnostics-test-'+[Guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory $directory
gcc -std=c11 -O2 -Wall -Wextra -Werror tests/diagnostics.c src/diagnostics.c -o "$directory/diagnostics.exe"
if($LASTEXITCODE){throw 'Diagnostics test build failed'}
function Run-Case([string]$Mode) {
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName="$directory/diagnostics.exe"
    $info.Arguments=$Mode
    $info.UseShellExecute=$false
    $info.CreateNoWindow=$true
    $info.EnvironmentVariables['LOCALAPPDATA']=$directory
    $process=[Diagnostics.Process]::Start($info)
    if(!$process.WaitForExit(10000)){$process.Kill();throw "Timed out: $Mode"}
    $code=$process.ExitCode
    $process.Dispose()
    return $code
}
if((Run-Case normal) -ne 0){throw 'Normal exit failed'}
$logs=Join-Path $directory 'DesktopsHelper'
if(Get-ChildItem "$logs/active-*.txt"){throw 'Normal exit left marker'}
if((Run-Case kill) -ne 42){throw 'Kill test failed'}
$marker=Get-ChildItem "$logs/active-*.txt"
if(!$marker){throw 'Forced exit lost marker'}
$before=Get-Content $marker.FullName -Raw
if((Run-Case duplicate) -ne 0){throw 'Duplicate test failed'}
if((Get-Content $marker.FullName -Raw) -ne $before){throw 'Duplicate changed owner marker'}
if((Run-Case normal) -ne 0){throw 'Recovery failed'}
if((Run-Case crash) -eq 0){throw 'Crash test did not crash'}
if(!(Test-Path "$logs/crash.log")){throw 'Crash details missing'}
if((Run-Case normal) -ne 0){throw 'Crash recovery failed'}
$locked=[IO.File]::Open("$logs/lifecycle.log",[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try {
    if((Run-Case normal) -ne 0){throw 'Unavailable log prevented exit'}
    if(!(Get-ChildItem "$logs/active-*.txt")){throw 'Failed exit log incorrectly cleared marker'}
} finally { $locked.Dispose() }
if((Run-Case normal) -ne 0){throw 'Logging failure recovery failed'}
$log=Get-Content "$logs/lifecycle.log" -Raw
foreach($expected in 'event=start','reason=test-normal','reason=already-running','event=crash','session-','job-') {
    if(!$log.Contains($expected)){throw "Missing $expected"}
}
if(([regex]::Matches($log,'event=previous-exit-unconfirmed')).Count -ne 3){throw 'Incorrect unconfirmed exit count'}
if(Get-ChildItem "$logs/active-*.txt"){throw 'Recovery left marker'}
Write-Output 'PASS: clean exit, forced termination, duplicate isolation, crash details, unavailable log, and next-launch detection'
