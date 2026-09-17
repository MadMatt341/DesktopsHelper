# Isolated provider fixtures: never reads or writes the real startup registry.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$global:dhStartupTest_fixtureSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$global:dhStartupTest_registration = $null
$global:dhStartupTest_approvalBytes = $null
$global:dhStartupTest_serviceMismatch = $false
$global:dhStartupTest_ownerMismatch = $false
$global:dhStartupTest_writes = 0
$global:dhStartupTest_valueType = 1
$global:dhStartupTest_mismatchOnce = $false
function Get-CimInstance {
    param($ClassName, $Filter)
    if ($ClassName -eq 'Win32_Process') { return [pscustomobject]@{ Fixture = 'Explorer' } }
    if ($ClassName -eq 'Win32_StartupCommand') {
        if ($global:dhStartupTest_mismatchOnce) { $global:dhStartupTest_mismatchOnce = $false; return }
        if ($null -ne $global:dhStartupTest_registration -and !$global:dhStartupTest_serviceMismatch) {
            return [pscustomobject]@{ UserSID = $global:dhStartupTest_fixtureSid; Location = 'HKU\Software\Microsoft\Windows\CurrentVersion\Run'; Command = $global:dhStartupTest_registration }
        }
        return
    }
    throw "Unexpected class: $ClassName"
}
function Invoke-CimMethod {
    param($InputObject, $Namespace, $ClassName, $MethodName, $Arguments)
    if ($MethodName -eq 'GetOwnerSid') {
        $ownerSid = $global:dhStartupTest_fixtureSid
        if ($global:dhStartupTest_ownerMismatch) { $ownerSid = 'S-1-5-18' }
        return [pscustomobject]@{ ReturnValue = 0; Sid = $ownerSid }
    }
    if ($Namespace -ne 'root/default' -or $ClassName -ne 'StdRegProv' -or $Arguments.hDefKey -ne [uint32]2147483651 -or !$Arguments.sSubKeyName.StartsWith($global:dhStartupTest_fixtureSid + '\')) { throw 'Wrong registry scope.' }
    switch ($MethodName) {
        EnumValues {
            $present = $null -ne $global:dhStartupTest_registration
            if ($Arguments.sSubKeyName -match 'StartupApproved') { $present = $null -ne $global:dhStartupTest_approvalBytes }
            $valueType = $global:dhStartupTest_valueType
            if ($Arguments.sSubKeyName -match 'StartupApproved') { $valueType = 3 }
            $names = @(); if ($present) { $names = @('DesktopsHelper') }
            return [pscustomobject]@{ ReturnValue = 0; sNames = $names; Types = @($valueType) }
        }
        GetStringValue {
            $code = 0; if ($null -eq $global:dhStartupTest_registration) { $code = 2 }
            return [pscustomobject]@{ ReturnValue = $code; sValue = $global:dhStartupTest_registration }
        }
        GetBinaryValue {
            $code = 0; if ($null -eq $global:dhStartupTest_approvalBytes) { $code = 2 }
            return [pscustomobject]@{ ReturnValue = $code; uValue = $global:dhStartupTest_approvalBytes }
        }
        CreateKey { }
        SetStringValue { $global:dhStartupTest_registration = $Arguments.sValue; $global:dhStartupTest_writes++ }
        DeleteValue { $global:dhStartupTest_registration = $null; $global:dhStartupTest_writes++ }
        default { throw "Unexpected method: $MethodName" }
    }
    return [pscustomobject]@{ ReturnValue = 0 }
}
function Assert($Condition, $Message) { if (!$Condition) { throw $Message } }
function Expect-Failure([scriptblock]$Action, [string]$Pattern) {
    try { & $Action | Out-Null } catch {
        if ($_.ToString() -notmatch $Pattern) { throw "Unexpected error: $_ (expected $Pattern)" }
        return
    }
    throw "Expected failure: $Pattern"
}
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('desktops-startup-' + [guid]::NewGuid())
$null = New-Item -ItemType Directory -Path $fixture
try {
    $exe = Join-Path $fixture 'DesktopsHelper.exe'
    foreach ($name in @('DesktopsHelper.exe', 'VirtualDesktopAccessor.dll', 'TaskbarPrototype.exe', 'DesktopsHelper.TaskbarV4.dll')) { $null = New-Item -ItemType File -Path (Join-Path $fixture $name) }
    $scriptPath = Join-Path $root 'startup.ps1'
    $state = & $scriptPath -Status
    Assert (!$state.Registered -and $global:dhStartupTest_writes -eq 0) 'Status mutated registration.'
    $state = & $scriptPath -ExecutablePath $exe
    Assert ($state.ServiceVerified -and $global:dhStartupTest_registration -ceq ('"' + $exe + '" --native-taskbar --sign-in')) 'Enable command incorrect.'
    $original = $global:dhStartupTest_registration
    $global:dhStartupTest_mismatchOnce = $true
    Expect-Failure { & $scriptPath -ExecutablePath $exe } 'prior Run value restored'
    Assert ($global:dhStartupTest_registration -ceq $original) 'Failed verification did not restore original.'
    $global:dhStartupTest_serviceMismatch = $true
    Expect-Failure { & $scriptPath -ExecutablePath $exe } 'Restoring the prior registration also failed'
    Expect-Failure { & $scriptPath -Status } 'independently confirm'
    $global:dhStartupTest_serviceMismatch = $false
    $global:dhStartupTest_approvalBytes = @(3) + @(0) * 11
    $writesBefore = $global:dhStartupTest_writes
    Expect-Failure { & $scriptPath -ExecutablePath $exe } 'StartupApproved state is Disabled'
    Assert ($global:dhStartupTest_writes -eq $writesBefore) 'Disabled approval was mutated.'
    $state = & $scriptPath -Status
    Assert ($state.StartupApproved -eq 'Disabled') 'Disabled state not reported.'
    $global:dhStartupTest_valueType = 2
    $writesBefore = $global:dhStartupTest_writes
    Expect-Failure { & $scriptPath -Disable } 'must be REG_SZ'
    Assert ($global:dhStartupTest_writes -eq $writesBefore) 'Unexpected registry type mutated.'
    $global:dhStartupTest_valueType = 1
    $global:dhStartupTest_approvalBytes = @(2, 1) + @(0) * 10
    Expect-Failure { & $scriptPath -ExecutablePath $exe } 'StartupApproved state is Unknown'
    $global:dhStartupTest_approvalBytes = @(99)
    Expect-Failure { & $scriptPath -ExecutablePath $exe } 'StartupApproved state is Unknown'
    $global:dhStartupTest_approvalBytes = $null
    $global:dhStartupTest_ownerMismatch = $true
    Expect-Failure { & $scriptPath -Disable } 'users differ'
    $global:dhStartupTest_ownerMismatch = $false
    Expect-Failure { & $scriptPath -Disable -Status } 'parameter set'
    if (Test-Path -LiteralPath (Join-Path $root '.git') -PathType Leaf) {
        Expect-Failure { & $scriptPath } 'worktree build'
    }
    $longDirectory = Join-Path $fixture ('x' * (245 - $fixture.Length - 1 - '\DesktopsHelper.exe'.Length))
    $null = New-Item -ItemType Directory -Path $longDirectory
    foreach ($name in @('DesktopsHelper.exe', 'VirtualDesktopAccessor.dll', 'TaskbarPrototype.exe', 'DesktopsHelper.TaskbarV4.dll')) { $null = New-Item -ItemType File -Path (Join-Path $longDirectory $name) }
    Expect-Failure { & $scriptPath -ExecutablePath (Join-Path $longDirectory 'DesktopsHelper.exe') } '260-character limit'
    $worktreeDirectory = Join-Path $fixture '.codex\worktrees\test'
    $null = New-Item -ItemType Directory -Path $worktreeDirectory -Force
    $worktreeExe = Join-Path $worktreeDirectory 'DesktopsHelper.exe'
    $null = New-Item -ItemType File -Path $worktreeExe
    Expect-Failure { & $scriptPath -ExecutablePath $worktreeExe } 'worktree build'
    Remove-Item -LiteralPath (Join-Path $fixture 'TaskbarPrototype.exe')
    Expect-Failure { & $scriptPath -ExecutablePath $exe } 'companion missing'
    $state = & $scriptPath -Disable
    Assert (!$state.Registered -and $null -eq $global:dhStartupTest_registration) 'Disable failed.'
    $state = & $scriptPath -Disable
    Assert (!$state.Registered) 'Repeated disable failed.'
    'PASS: isolated startup status, enable, disable, exact command, independent verification, rollback, approval, identity, parameter and dependency checks.'
} finally {
    Get-Variable -Name 'dhStartupTest_*' -Scope Global | Remove-Variable -Scope Global
    # Only the unique test directory created above is removed.
    if ($fixture -like (Join-Path ([IO.Path]::GetTempPath()) 'desktops-startup-*')) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
