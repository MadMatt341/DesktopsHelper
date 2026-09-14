$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
if(Get-Process DesktopsHelper -ErrorAction SilentlyContinue){throw 'Exit helper before native launcher tests'}
$testDirectory=Join-Path $PWD 'build/native-launcher-fault'
New-Item -ItemType Directory -Force $testDirectory | Out-Null
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode -mwindows tests/fake-native-controller.c -o "$testDirectory/TaskbarPrototype.exe"
if($LASTEXITCODE){throw 'Fake controller build failed'}
Copy-Item build/release/DesktopsHelper.exe,build/release/VirtualDesktopAccessor.dll $testDirectory -Force
$results=@()
foreach($scenario in @('deadline','owner-crash')) {
    $helper=Start-Process -FilePath "$testDirectory/DesktopsHelper.exe" -ArgumentList '--native-taskbar' -WindowStyle Hidden -PassThru
    try {
        $limit=[DateTime]::UtcNow.AddSeconds(4)
        $controller=$null
        do {
            $controller=Get-Process TaskbarPrototype -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq "$testDirectory\TaskbarPrototype.exe"} | Select-Object -First 1
            if(!$controller){Start-Sleep -Milliseconds 25}
        } while(!$controller -and [DateTime]::UtcNow -lt $limit)
        if(!$controller){throw 'Fake controller did not start'}
        $clock=[Diagnostics.Stopwatch]::StartNew()
        if($scenario -eq 'owner-crash'){$helper.Kill();$helper.WaitForExit()}
        if(!$controller.WaitForExit(17000)){throw "Controller survived $scenario"}
        $controllerElapsedMs=$clock.Elapsed.TotalMilliseconds
        if($scenario -eq 'deadline') {
            & ./build/release/health.exe ready | Out-Null
            if($LASTEXITCODE){throw 'Helper stopped responding during native deadline'}
            & ./scripts/stop.ps1
            if(!$helper.WaitForExit(5000) -or $helper.ExitCode -ne 0){throw 'Helper failed clean shutdown'}
        }
        $results += [pscustomobject]@{scenario=$scenario; elapsedMs=$controllerElapsedMs; controllerExited=$controller.HasExited; passed=$true}
        Write-Output "native launcher ${scenario}: PASS"
    } finally {
        if(!$helper.HasExited){$helper.Kill();$helper.WaitForExit()}
        $helper.Dispose()
        if($controller){$controller.Dispose()}
    }
}
$results | ConvertTo-Json | Set-Content benchmarks/native-launcher-validation.json
