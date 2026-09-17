# Isolated desktop adapter: no real desktops are switched.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
if (Get-Process DesktopsHelper -ErrorAction SilentlyContinue) { throw 'Exit the helper before sign-in tests' }
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SignInState {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string name, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr hwnd, string name);
}
'@
function Wait-Desktop([int]$Expected) {
    $deadline = [DateTime]::UtcNow.AddSeconds(8)
    do {
        $hwnd = [SignInState]::FindWindow('DesktopsHelper.Widget', 'Desktops Helper')
        if ([SignInState]::GetProp($hwnd, 'DesktopsHelper.Ready').ToInt64() -eq 1 -and
            [SignInState]::GetProp($hwnd, 'DesktopsHelper.Current').ToInt64() -eq $Expected) { return }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Expected desktop $Expected"
}
foreach ($case in @(
    @{ Mode=5; Argument=''; Expected=2 },
    @{ Mode=5; Argument='--sign-in'; Expected=1 },
    @{ Mode=6; Argument='--sign-in'; Expected=1 },
    @{ Mode=5; Argument='--sign-in-other'; Expected=2 }
)) {
    $directory = 'build/sign-in-test'
    $null = New-Item -ItemType Directory -Force $directory
    gcc -shared -O2 -Wall -Wextra -Werror "-DFAULT_MODE=$($case.Mode)" tests/fake-adapter.c -o "$directory/VirtualDesktopAccessor.dll"
    if ($LASTEXITCODE) { throw 'Fake adapter build failed' }
    Copy-Item build/release/DesktopsHelper.exe $directory
    $launch = @{ FilePath=(Join-Path (Resolve-Path $directory).Path 'DesktopsHelper.exe'); WindowStyle='Hidden'; PassThru=$true }
    if ($case.Argument) { $launch.ArgumentList=$case.Argument }
    $process = Start-Process @launch
    try {
        Wait-Desktop $case.Expected
        ./build/release/health.exe reconnect
        if ($LASTEXITCODE) { throw 'Reconnect failed' }
        # The fake adapter simulates being on Desktop 2 at reconnect.
        Start-Sleep -Milliseconds 500
        Wait-Desktop 2
        ./scripts/stop.ps1
        if (!$process.WaitForExit(5000) -or $process.ExitCode -ne 0) { throw 'Unclean exit' }
        Write-Output "Passed mode=$($case.Mode) argument='$($case.Argument)' and reconnect"
    } finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}
