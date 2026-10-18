# Authored by Codex. Windows PowerShell 5.1 / 7; development tool only.
# Generates selected design A1 (balanced displays) in both theme colors using GDI+.
param([switch]$Light)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$projectRoot = Split-Path -Parent $PSScriptRoot
$assetDirectory = Join-Path $projectRoot 'assets'
[void][IO.Directory]::CreateDirectory($assetDirectory)

function New-RoundedPath([single]$X, [single]$Y, [single]$Width, [single]$Height, [single]$Radius) {
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $diameter = $Radius * 2
    $path.AddArc($X, $Y, $diameter, $diameter, 180, 90)
    $path.AddArc($X + $Width - $diameter, $Y, $diameter, $diameter, 270, 90)
    $path.AddArc($X + $Width - $diameter, $Y + $Height - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($X, $Y + $Height - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return ,$path
}

$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$images = @()
foreach ($size in $sizes) {
    $large = New-Object Drawing.Bitmap ($size * 4), ($size * 4)
    $graphics = [Drawing.Graphics]::FromImage($large)
    $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.ScaleTransform($size / 8.0, $size / 8.0)
    # Match assets/app-icon.svg in the 32-unit viewbox.
    $rear = New-Object Drawing.Drawing2D.GraphicsPath
    $rear.AddLine([single]7, [single]17, [single]4.5, [single]17)
    $rear.AddArc(2, 12, 5, 5, 90, 90)
    $rear.AddLine([single]2, [single]14.5, [single]2, [single]5.5)
    $rear.AddArc(2, 3, 5, 5, 180, 90)
    $rear.AddLine([single]4.5, [single]3, [single]19.5, [single]3)
    $rear.AddArc(17, 3, 5, 5, 270, 90)
    $rear.AddLine([single]22, [single]5.5, [single]22, [single]8)
    $screen = New-RoundedPath 10 11 20 15 2.5
    $color = if ($Light) { [Drawing.Color]::FromArgb(245, 245, 245) } else { [Drawing.Color]::FromArgb(35, 39, 45) }
    $ink = New-Object Drawing.Pen $color, 2.1
    $ink.StartCap = [Drawing.Drawing2D.LineCap]::Round
    $ink.EndCap = [Drawing.Drawing2D.LineCap]::Round
    $ink.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
    $graphics.DrawPath($ink, $rear)
    $graphics.DrawPath($ink, $screen)
    $graphics.DrawLine($ink, 20, 26, 20, 30)
    $graphics.DrawLine($ink, 15, 30, 25, 30)
    $graphics.DrawLine($ink, [single]2, [single]24, [single]7.5, [single]24)
    $graphics.DrawLine($ink, [single]4.5, [single]21, [single]7.5, [single]24)
    $graphics.DrawLine($ink, [single]7.5, [single]24, [single]4.5, [single]27)
    $bitmap = New-Object Drawing.Bitmap $size, $size
    $target = [Drawing.Graphics]::FromImage($bitmap)
    $target.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $target.DrawImage($large, 0, 0, $size, $size)
    $stream = New-Object IO.MemoryStream
    $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
    $images += ,$stream.ToArray()
    $stream.Dispose()
    $target.Dispose()
    $bitmap.Dispose()
    $ink.Dispose()
    $screen.Dispose()
    $rear.Dispose()
    $graphics.Dispose()
    $large.Dispose()
}

$fileName = if ($Light) { 'app-light.ico' } else { 'app.ico' }
$output = [IO.File]::Create((Join-Path $assetDirectory $fileName))
$writer = New-Object IO.BinaryWriter $output
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $dimension = if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
        $writer.Write([byte]$dimension)
        $writer.Write([byte]$dimension)
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$images[$i].Length)
        $writer.Write([uint32]$offset)
        $offset += $images[$i].Length
    }
    foreach ($bytes in $images) { $writer.Write([byte[]]$bytes) }
} finally {
    $writer.Dispose()
}
Get-Item -LiteralPath (Join-Path $assetDirectory $fileName) | Select-Object FullName, Length
