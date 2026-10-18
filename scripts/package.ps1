# Authored by Codex. Compatible with PowerShell 5.1 and 7.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release
& (Join-Path $PSScriptRoot 'test.ps1') -Configuration Release -NoBuild
$releaseExe = Join-Path $projectRoot 'out\build\bin\Release\WinPPlus.exe'
$distribution = Join-Path $projectRoot 'dist'
[void][IO.Directory]::CreateDirectory($distribution)
$destination = Join-Path $distribution 'WinPPlus.exe'
try {
    Copy-Item -LiteralPath $releaseExe -Destination $destination -Force
} catch {
    throw "Could not replace $destination. Exit the running tray application and retry. $($_.Exception.Message)"
}
Get-Item -LiteralPath $destination | Select-Object FullName, Length, LastWriteTime
$version = & (Join-Path $PSScriptRoot 'get-version.ps1')
$archive = Join-Path $distribution "WinPPlus-v$version-windows-x64.zip"
$stagingRoot = Join-Path $projectRoot ('out\package-' + [Guid]::NewGuid().ToString('N'))
$bundleRoot = Join-Path $stagingRoot 'WinPPlus'
try {
    [void][IO.Directory]::CreateDirectory($bundleRoot)
    $cmakePath = (Get-Command cmake -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    & $cmakePath --install (Join-Path $projectRoot 'out\build') --config Release --prefix $bundleRoot
    if ($LASTEXITCODE -ne 0) { throw "CMake install failed with exit code $LASTEXITCODE" }
    # CMake writes portable ZIP paths consistently in PowerShell 5.1 and 7.
    Push-Location -LiteralPath $stagingRoot
    try {
        & $cmakePath -E tar cf $archive --format=zip WinPPlus
        if ($LASTEXITCODE -ne 0) { throw "CMake archive failed with exit code $LASTEXITCODE" }
    } finally {
        Pop-Location
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        if ($zip.Entries.Count -eq 0 -or @($zip.Entries | Where-Object {
            -not $_.FullName.Replace('\', '/').StartsWith('WinPPlus/', [StringComparison]::Ordinal)
        }).Count -ne 0) {
            throw 'Portable ZIP must contain exactly one top-level WinPPlus directory.'
        }
        if (-not $zip.GetEntry('WinPPlus/WinPPlus.exe')) {
            throw 'Portable ZIP is missing WinPPlus/WinPPlus.exe.'
        }
    } finally {
        $zip.Dispose()
    }
} finally {
    $allowedParent = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out')) + [IO.Path]::DirectorySeparatorChar
    $resolvedStaging = [IO.Path]::GetFullPath($stagingRoot)
    if (-not $resolvedStaging.StartsWith($allowedParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove staging directory outside $allowedParent"
    }
    if (Test-Path -LiteralPath $resolvedStaging) {
        Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
    }
}
$checksums = @($destination, $archive) | ForEach-Object {
    $hash = Get-FileHash -LiteralPath $_ -Algorithm SHA256
    '{0}  {1}' -f $hash.Hash.ToLowerInvariant(), (Split-Path -Leaf $hash.Path)
}
[IO.File]::WriteAllLines((Join-Path $distribution 'SHA256SUMS.txt'), [string[]]$checksums,
    (New-Object Text.UTF8Encoding $false))
