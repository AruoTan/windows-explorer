#Requires -Version 5.1
# Automated subset only. Does not switch/rename desktops, restart Explorer or inspect application content.
[CmdletBinding()]
param([switch]$ExerciseExplorer)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output = Join-Path $root 'out/windows-validation'
[void][IO.Directory]::CreateDirectory($output)
$exe = Join-Path $root 'out/explorer-extensions/ExplorerExtensions.exe'
function Run-Entry([string[]]$Arguments) {
    $process = Start-Process -FilePath $exe -ArgumentList $Arguments -PassThru
    if (-not $process.WaitForExit(180000)) { $process.Kill(); throw 'Entry point timed out' }
    $process.Refresh()
    if ($process.ExitCode -ne 0) { throw "Entry failed ($($process.ExitCode)): $Arguments" }
}
function Read-Value([string]$Path, [string]$Name) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($Path)
    if ($null -eq $key) { return $null }
    try { return $key.GetValue($Name, $null) } finally { $key.Close() }
}
$version = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
if ($version.CurrentBuildNumber -ne '26300' -or $version.UBR -ne 9711) { throw 'This acceptance script requires OS Build 26300.9711' }
$control = 'Software\ExplorerExtensions'
$run = 'Software\Microsoft\Windows\CurrentVersion\Run'
$baseline = [ordered]@{
    Build = "$($version.CurrentBuildNumber).$($version.UBR)"
    Drive = Read-Value $control 'DriveNavigationFilterEnabled'
    Popup = Read-Value $control 'DisableVirtualDesktopPopups'
    Startup = Read-Value $run 'ExplorerExtensions'
}
$baseline | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $output 'baseline.json')
$test = Join-Path $root 'out/tests/hook-smoke.exe'
if (Test-Path -LiteralPath $test) { & $test; if ($LASTEXITCODE -ne 0) { throw 'Hook smoke test failed' } }
Run-Entry -Arguments @('--diagnose')
if (-not $ExerciseExplorer) { Write-Output 'PASS: read-only target diagnostics'; return }
Run-Entry -Arguments @('--disable')
# Use a separate immutable runtime copy: Windows pins the loaded DLL until Explorer exits.
$runtimeHash = (Get-FileHash -LiteralPath (Join-Path $root 'out/explorer-extensions/ExplorerExtensions.dll') -Algorithm SHA256).Hash.Substring(0,12)
$runtime = Join-Path $output ('runtime-' + $runtimeHash)
[void][IO.Directory]::CreateDirectory($runtime)
foreach ($name in @('ExplorerExtensions.exe','ExplorerExtensions.dll')) {
    Copy-Item -LiteralPath (Join-Path $root "out/explorer-extensions/$name") -Destination (Join-Path $runtime $name)
}
$exe = Join-Path $runtime 'ExplorerExtensions.exe'
$session = (Get-Process -Id $PID).SessionId
$explorer = @(Get-Process explorer | Where-Object SessionId -eq $session | Select-Object -ExpandProperty Id)
if ($explorer.Count -ne 1) { throw 'Automated test requires one Explorer in this session' }
$targetPid = $explorer[0]
$status = "$control\Processes\$targetPid"
function Wait-State([string]$Feature, [string]$Expected) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $state = Read-Value $status ($Feature + 'State')
        if ($state -eq $Expected) { return }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out waiting for $Feature=$Expected; got $state"
}
$pdb = Join-Path $env:LOCALAPPDATA 'ExplorerExtensions/symbols/twinui.pdb/FDDD5C12E412A64AD5FB8AA23B5029871/twinui.pdb'
# This fixture is tied to the recorded module identity and only used for this acceptance run.
$backup = $pdb + '.validation-backup-' + $PID
$results = [ordered]@{Build=$baseline.Build;TargetPid=$targetPid;VisualTests='Not executed'}
try {
    Run-Entry -Arguments @('--enable','drive-navigation')
    Wait-State 'Drive' 'Enabled'
    $results.DriveInstall = 'PASS'
    Move-Item -LiteralPath $pdb -Destination $backup
    try {
        [IO.File]::WriteAllBytes($pdb, [byte[]](0,1,2,3))
        Run-Entry -Arguments @('--enable','virtual-desktop-popups')
        Wait-State 'Popup' 'Unavailable'
        Wait-State 'Drive' 'Enabled'
        $results.BadSymbolsIsolated = 'PASS'
        Run-Entry -Arguments @('--disable','virtual-desktop-popups')
    } finally {
        Remove-Item -LiteralPath $pdb -Force -ErrorAction SilentlyContinue
        Move-Item -LiteralPath $backup -Destination $pdb
    }
    Run-Entry -Arguments @('--enable','virtual-desktop-popups')
    Wait-State 'Popup' 'Enabled'
    $results.PopupInstall = 'PASS'
    for ($cycle = 0; $cycle -lt 5; $cycle++) {
        Run-Entry -Arguments @('--disable','virtual-desktop-popups'); Wait-State 'Popup' 'Disabled'
        Run-Entry -Arguments @('--enable','virtual-desktop-popups'); Wait-State 'Popup' 'Enabled'
    }
    $results.RepeatedToggles = 5
    if (-not (Get-Process -Id $targetPid -ErrorAction SilentlyContinue)) { throw 'Explorer exited unexpectedly' }
    $results.ExplorerAlive = 'PASS'
} finally {
    # Restore this project feature selection.
    $restoreDrive = $baseline.Drive -eq 1
    Run-Entry -Arguments @('--disable')
    if ($restoreDrive) { Run-Entry -Arguments @('--enable','drive-navigation') }
    if ($baseline.Popup -eq 1) { Run-Entry -Arguments @('--enable','virtual-desktop-popups') }
    # Restore whether login startup existed before this test. Do not add startup merely by testing.
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($run)
    try {
        if ($null -eq $baseline.Startup) { $key.DeleteValue('ExplorerExtensions', $false) }
        else { $key.SetValue('ExplorerExtensions', $baseline.Startup, [Microsoft.Win32.RegistryValueKind]::String) }
    } finally { $key.Close() }
    $results | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $output 'results.json')
    foreach ($processId in $explorer) {
        Get-ChildItem (Join-Path $env:LOCALAPPDATA "ExplorerExtensions/logs/$processId-*.log") -ErrorAction SilentlyContinue |
            Copy-Item -Destination $output
    }
}
$results | ConvertTo-Json
