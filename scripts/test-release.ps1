# Authored by Codex. Isolated release/version checks; never edits the real VERSION.
$ErrorActionPreference = 'Stop'
$scriptDirectory = $PSScriptRoot
$count = 0
function Assert-Release([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "FAIL $Name" }
    $script:count++
}
$current = & (Join-Path $scriptDirectory 'get-version.ps1')
$initial = & (Join-Path $scriptDirectory 'release-plan.ps1')
Assert-Release ($initial.Publish -and $initial.Tag -eq "v$current") 'first version creates a release'
$unchanged = & (Join-Path $scriptDirectory 'release-plan.ps1') -PreviousVersion $current
Assert-Release (-not $unchanged.Publish) 'unchanged version does not publish again'
$raised = & (Join-Path $scriptDirectory 'release-plan.ps1') -PreviousVersion '0.0.0'
Assert-Release $raised.Publish 'increased version publishes'
$manual = & (Join-Path $scriptDirectory 'release-plan.ps1') -PreviousVersion $current -Manual
Assert-Release $manual.Publish 'manual dispatch can retry an unpublished version'
$rejected = $false
try { & (Join-Path $scriptDirectory 'release-plan.ps1') -PreviousVersion '65535.65535.65535' | Out-Null }
catch { $rejected = $true }
Assert-Release $rejected 'version downgrade is rejected'
$fixture = [IO.Path]::GetTempFileName()
try {
    foreach ($invalid in @('1.2', '01.2.3', '1.2.3-beta', '65536.0.0', '1.2.3 extra')) {
        [IO.File]::WriteAllText($fixture, $invalid)
        $rejected = $false
        try { & (Join-Path $scriptDirectory 'get-version.ps1') -Path $fixture | Out-Null }
        catch { $rejected = $true }
        Assert-Release $rejected "invalid version rejected: $invalid"
    }
} finally {
    [IO.File]::Delete($fixture)
}
Write-Output "Release rules: $count passed, 0 failed."
