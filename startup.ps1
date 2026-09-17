[CmdletBinding(DefaultParameterSetName = 'Enable')]
param(
    [Parameter(ParameterSetName = 'Disable', Mandatory)][switch]$Disable,
    [Parameter(ParameterSetName = 'Status', Mandatory)][switch]$Status,
    [Parameter(ParameterSetName = 'Enable')][string]$ExecutablePath = (Join-Path $PSScriptRoot 'build\release\DesktopsHelper.exe')
)
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Run this script in 64-bit PowerShell.' }
$name = 'DesktopsHelper'
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$sessionId = (Get-Process -Id $PID).SessionId
$shells = @(Get-CimInstance Win32_Process -Filter "Name='explorer.exe' AND SessionId=$sessionId")
if (!$shells.Count) { throw 'No interactive Explorer in this session; run from your normal Windows account.' }
foreach ($shell in $shells) {
    $owner = Invoke-CimMethod -InputObject $shell -MethodName GetOwnerSid
    if ($owner.ReturnValue -ne 0 -or $owner.Sid -ne $sid) {
        throw 'Current and interactive Windows users differ. Refusing to change another account or sandbox registry.'
    }
}
$runKey = "$sid\Software\Microsoft\Windows\CurrentVersion\Run"
$approvedKey = "$sid\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run"
function Registry([string]$Method, [string]$Key, [hashtable]$Extra = @{}) {
    $arguments = @{ hDefKey = [uint32]2147483651; sSubKeyName = $Key; sValueName = $name }
    if ($Method -in @('CreateKey', 'EnumValues')) { $arguments.Remove('sValueName') }
    foreach ($keyName in $Extra.Keys) { $arguments[$keyName] = $Extra[$keyName] }
    $result = Invoke-CimMethod -Namespace root/default -ClassName StdRegProv -MethodName $Method -Arguments $arguments
    if ($result.ReturnValue -notin @(0, 2)) { throw "StdRegProv $Method failed: $($result.ReturnValue)" }
    return $result
}
function Read-Registration {
    $values = Registry EnumValues $runKey
    if ($values.ReturnValue -eq 2 -or $name -notin $values.sNames) { return $null }
    $index = [Array]::IndexOf([string[]]$values.sNames, ($values.sNames | Where-Object { $_ -ieq $name } | Select-Object -First 1))
    if ($index -lt 0 -or $values.Types[$index] -ne 1) { throw 'Existing Run value must be REG_SZ; refusing to change an unexpected registry type.' }
    $value = Registry GetStringValue $runKey
    if ($value.ReturnValue -eq 2) { return $null }
    return [string]$value.sValue
}
function Verify-Service([AllowNull()][string]$Command, [bool]$Present) {
    $entries = @(Get-CimInstance Win32_StartupCommand -Filter "Name='$name'" | Where-Object {
        $_.UserSID -eq $sid -and $_.Location -match '\\Run$'
    })
    if ($Present) {
        if ($entries.Count -ne 1 -or $entries[0].Command -cne $Command) {
            throw 'Win32_StartupCommand does not independently confirm the exact current-user Run registration.'
        }
    } elseif ($entries.Count) { throw 'Win32_StartupCommand still reports the current-user Run registration.' }
}
$before = Read-Registration
$approvalValues = Registry EnumValues $approvedKey
$approval = [pscustomobject]@{ ReturnValue = 2 }
if ($approvalValues.ReturnValue -eq 0 -and $name -in $approvalValues.sNames) {
    $index = [Array]::IndexOf([string[]]$approvalValues.sNames, ($approvalValues.sNames | Where-Object { $_ -ieq $name } | Select-Object -First 1))
    if ($index -lt 0 -or $approvalValues.Types[$index] -ne 3) { throw 'StartupApproved value must be REG_BINARY; refusing an unexpected registry type.' }
    $approval = Registry GetBinaryValue $approvedKey
}
$approvalState = 'Not set (Windows default)'
$blocked = $false
if ($approval.ReturnValue -eq 0) {
    $bytes = @($approval.uValue)
    $stateCode = $null
    if ($bytes.Count -ge 12) { $stateCode = [BitConverter]::ToUInt32([byte[]]$bytes, 0) }
    if ($stateCode -in @(2, 6)) { $approvalState = 'Enabled' }
    elseif ($stateCode -in @(3, 7)) { $approvalState = 'Disabled'; $blocked = $true }
    else { $approvalState = 'Unknown'; $blocked = $true }
}
if ($Status) {
    Verify-Service $before ($null -ne $before)
    [pscustomobject]@{ Name = $name; UserSID = $sid; Registered = ($null -ne $before); Command = $before; StartupApproved = $approvalState; ServiceVerified = $true }
    return
}
$desired = $null
if (!$Disable) {
    if ($blocked) { throw "Windows StartupApproved state is $approvalState. Enable DesktopsHelper in Windows Settings > Apps > Startup, then retry; this script never changes that state." }
    if ($ExecutablePath.Contains('"') -or $ExecutablePath.Contains("`r") -or $ExecutablePath.Contains("`n")) { throw 'Invalid executable path.' }
    if ((Test-Path -LiteralPath (Join-Path $PSScriptRoot '.git') -PathType Leaf) -and !$PSBoundParameters.ContainsKey('ExecutablePath')) {
        throw 'A worktree build must not replace the live startup target. Pass -ExecutablePath pointing to your permanent project build.'
    }
    $resolved = Get-Item -LiteralPath $ExecutablePath
    if ($resolved.PSIsContainer -or $resolved.Extension -ine '.exe') { throw 'ExecutablePath must identify an existing .exe file.' }
    $executable = $resolved.FullName
    if ($executable -match '[\\/]\.codex[\\/]worktrees[\\/]') {
        throw 'A worktree build must not replace the live startup target. Pass -ExecutablePath pointing to your permanent project build.'
    }
    foreach ($file in @('VirtualDesktopAccessor.dll', 'TaskbarPrototype.exe', 'DesktopsHelper.TaskbarV4.dll')) {
        if (!(Test-Path -LiteralPath (Join-Path $resolved.DirectoryName $file) -PathType Leaf)) { throw "Required native-mode companion missing: $file" }
    }
    $desired = '"' + $executable + '" --native-taskbar --sign-in'
    if ($desired.Length -gt 260) { throw "Run command exceeds the 260-character limit ($($desired.Length))." }
}
try {
    if ($Disable) { $null = Registry DeleteValue $runKey }
    else {
        $created = Registry CreateKey $runKey
        if ($created.ReturnValue -ne 0) { throw 'Could not create the Run key.' }
        $result = Registry SetStringValue $runKey @{ sValue = $desired }
        if ($result.ReturnValue -ne 0) { throw 'Could not write the Run registration.' }
    }
    $actual = Read-Registration
    if ($actual -cne $desired) { throw 'Run registration read-back differs from the requested value.' }
    Verify-Service $desired (!$Disable)
} catch {
    $failure = $_
    try {
        if ($null -eq $before) { $null = Registry DeleteValue $runKey }
        else {
            $restored = Registry SetStringValue $runKey @{ sValue = $before }
            if ($restored.ReturnValue -ne 0) { throw 'Restore write failed.' }
        }
        if ((Read-Registration) -cne $before) { throw 'Restore read-back failed.' }
        Verify-Service $before ($null -ne $before)
    } catch { throw "Registration failed: $failure. Restoring the prior registration also failed: $_" }
    throw "Registration failed; prior Run value restored: $failure"
}
[pscustomobject]@{ Name = $name; UserSID = $sid; Registered = (!$Disable); Command = $desired; StartupApproved = $approvalState; ServiceVerified = $true }
