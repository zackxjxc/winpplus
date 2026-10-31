# Authored by Codex. Extracts one version section for the GitHub release page.
param(
    [Parameter(Mandatory = $true)][string]$Version
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$changelogPath = Join-Path $projectRoot 'CHANGELOG.md'
$text = [IO.File]::ReadAllText($changelogPath)
$escapedVersion = [regex]::Escape($Version)
$pattern = "(?ms)^##\s+\[$escapedVersion\](?:\s+-[^\r\n]*)?\s*\r?\n(?<notes>.*?)(?=^##\s+\[|\z)"
$match = [regex]::Match($text, $pattern)
if (-not $match.Success) {
    throw "No changelog section found for version $Version."
}
$notes = $match.Groups['notes'].Value.Trim()
if ([string]::IsNullOrWhiteSpace($notes)) {
    throw "Changelog section for version $Version is empty."
}
Write-Output $notes
