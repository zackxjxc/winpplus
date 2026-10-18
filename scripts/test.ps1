# Authored by Codex. Compatible with PowerShell 5.1 and 7.
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [switch]$NoBuild,
    [string]$ExecutablePath
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration
}
$testExe = if ($ExecutablePath) { $ExecutablePath } else {
    Join-Path $projectRoot "out\build\bin\$Configuration\WinPPlus.exe"
}
if (-not (Test-Path -LiteralPath $testExe)) { throw "Test executable not found: $testExe" }
# Do not set WindowStyle Hidden: STARTF_USESHOWWINDOW would override the first
# ShowWindow call and invalidate the native-window visibility tests.
$testStartInfo = New-Object Diagnostics.ProcessStartInfo
$testStartInfo.FileName = $testExe
$testStartInfo.Arguments = '--self-test'
$testStartInfo.UseShellExecute = $false
$testStartInfo.CreateNoWindow = $true
$testProcess = [Diagnostics.Process]::Start($testStartInfo)
if (-not $testProcess.WaitForExit(30000)) {
    $testProcess.Kill()
    $testProcess.WaitForExit()
    throw 'Self-tests timed out after 30 seconds.'
}
$reportPath = Join-Path $env:APPDATA 'WinPPlus\self-test.txt'
$report = Get-Content -LiteralPath $reportPath
$passCount = @($report | Where-Object { $_ -match '^PASS ' }).Count
$logDirectory = Join-Path $projectRoot 'logs'
[void][IO.Directory]::CreateDirectory($logDirectory)
[IO.File]::WriteAllLines((Join-Path $logDirectory "self-test-$Configuration.txt"), [string[]]$report)
if ($testProcess.ExitCode -ne 0 -or $passCount -eq 0 -or $report -notcontains 'FAILURES 0') {
    $report | Write-Output
    throw "Self-tests failed with exit code $($testProcess.ExitCode)."
}
Write-Output "$Configuration self-tests: $passCount passed, 0 failed."
