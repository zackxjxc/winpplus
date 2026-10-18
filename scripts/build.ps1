# Authored by Codex. Compatible with PowerShell 5.1 and 7.
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [switch]$Rebuild
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmakePath = (Get-Command cmake -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
& $cmakePath --preset windows -S $projectRoot
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
$buildArguments = @('--build', (Join-Path $projectRoot 'out\build'), '--config', $Configuration, '--parallel')
if ($Rebuild) { $buildArguments += '--clean-first' }
& $cmakePath @buildArguments
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
