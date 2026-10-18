# Authored by Codex. Compatible with PowerShell 5.1 and 7.
param([string]$Path = (Join-Path (Split-Path -Parent $PSScriptRoot) 'VERSION'))
$ErrorActionPreference = 'Stop'
$version = [IO.File]::ReadAllText($Path).Trim()
if ($version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
    throw 'VERSION must contain a stable version in MAJOR.MINOR.PATCH format.'
}
foreach ($component in $version.Split('.')) {
    if ([long]$component -gt 65535) { throw 'Version components must fit Windows version resources (0-65535).' }
}
$version
