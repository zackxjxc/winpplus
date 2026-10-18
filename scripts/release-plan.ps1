# Authored by Codex. Pure release decision, shared by CI and regression checks.
param([string]$PreviousVersion, [switch]$Manual)
$ErrorActionPreference = 'Stop'
$version = & (Join-Path $PSScriptRoot 'get-version.ps1')
if ($PreviousVersion) {
    if ($PreviousVersion -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw 'Previous VERSION is invalid.'
    }
    if ([version]$version -lt [version]$PreviousVersion) {
        throw 'VERSION must not decrease.'
    }
}
[pscustomobject]@{
    Version = $version
    Tag = "v$version"
    Publish = [bool]($Manual -or -not $PreviousVersion -or [version]$version -gt [version]$PreviousVersion)
}
