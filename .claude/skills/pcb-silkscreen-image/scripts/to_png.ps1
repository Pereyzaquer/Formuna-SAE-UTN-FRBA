# Convert any image format Windows can decode (WEBP, JPG, GIF, BMP, TIFF, HEIC...) to PNG.
# Uses WPF/WIC (System.Windows.Media.Imaging), which on modern Windows 10/11 has broader
# codec coverage than GDI+ (System.Drawing) -- notably it can read WEBP where GDI+ cannot.
# Usage: powershell -File to_png.ps1 <input> <output.png>
param(
    [Parameter(Mandatory=$true)][string]$InputPath,
    [Parameter(Mandatory=$true)][string]$OutputPath
)
Add-Type -AssemblyName PresentationCore
$InputPath = (Resolve-Path $InputPath).Path
$uri = New-Object System.Uri($InputPath)
$decoder = [System.Windows.Media.Imaging.BitmapDecoder]::Create(
    $uri,
    [System.Windows.Media.Imaging.BitmapCreateOptions]::None,
    [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad
)
$frame = $decoder.Frames[0]
# Force a consistent, alpha-aware pixel format so the downstream PNG decoder
# always sees straightforward RGBA8.
$converted = New-Object System.Windows.Media.Imaging.FormatConvertedBitmap(
    $frame, [System.Windows.Media.PixelFormats]::Bgra32, $null, 0
)
$encoder = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
$encoder.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($converted))
$outDir = Split-Path -Parent $OutputPath
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
$stream = [System.IO.File]::Open($OutputPath, [System.IO.FileMode]::Create)
try { $encoder.Save($stream) } finally { $stream.Close() }
Write-Output "OK $($frame.PixelWidth)x$($frame.PixelHeight) -> $OutputPath"
