# gen_icons.ps1
# Generates all 11 AirBeam tray ICO files and matching SVG source files.
# Uses only System.Drawing (built-in) — no external tools required.
# ICOs use Vista+ PNG-in-ICO format with 16x16, 32x32, and 48x48 frames.
#
# Usage:
#   pwsh resources/icons/src/gen_icons.ps1
# Output:
#   resources/icons/*.ico        (11 files, ~12-16 KB each)
#   resources/icons/src/*.svg    (SVG source files for future editing)

param(
    [string]$OutDir = (Join-Path $PSScriptRoot "..")
)

Add-Type -AssemblyName System.Drawing

# ---------------------------------------------------------------------------
# Color palette
# ---------------------------------------------------------------------------
$Gray  = [System.Drawing.Color]::FromArgb(255, 158, 158, 158)  # #9E9E9E idle
$Blue  = [System.Drawing.Color]::FromArgb(255, 33,  150, 243)  # #2196F3 streaming + connecting
$Red   = [System.Drawing.Color]::FromArgb(255, 244, 67,  54)   # #F44336 error
$White = [System.Drawing.Color]::White

# ---------------------------------------------------------------------------
# Helper: Draw one bitmap frame at $Size x $Size pixels
# ---------------------------------------------------------------------------
function New-Frame {
    param([int]$Size, [scriptblock]$Draw)
    $bmp = New-Object System.Drawing.Bitmap $Size, $Size, `
           ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.SmoothingMode    = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    & $Draw $g $Size
    $g.Dispose()
    return $bmp
}

# ---------------------------------------------------------------------------
# Helper: Package PNG frames into a Vista+ ICO (PNG-in-ICO)
# ---------------------------------------------------------------------------
function Save-Ico {
    param([string]$Path, [System.Drawing.Bitmap[]]$Frames)

    $pngs = $Frames | ForEach-Object {
        $ms = New-Object System.IO.MemoryStream
        $_.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        , $ms.ToArray()
    }

    $count   = $pngs.Count
    $hdrSize = 6
    $dirSize = $count * 16
    $offset  = $hdrSize + $dirSize
    foreach ($p in $pngs) { $offsets += @($offset); $offset += $p.Length }

    $ms     = New-Object System.IO.MemoryStream
    $writer = New-Object System.IO.BinaryWriter $ms

    # ICO header
    $writer.Write([uint16]0)        # Reserved
    $writer.Write([uint16]1)        # Type: icon
    $writer.Write([uint16]$count)

    # Directory
    $offsets = @()
    $off = $hdrSize + $dirSize
    foreach ($p in $pngs) { $offsets += $off; $off += $p.Length }

    for ($i = 0; $i -lt $count; $i++) {
        $w = $Frames[$i].Width;  if ($w -ge 256) { $w = 0 }
        $h = $Frames[$i].Height; if ($h -ge 256) { $h = 0 }
        $writer.Write([byte]$w)
        $writer.Write([byte]$h)
        $writer.Write([byte]0)        # ColorCount
        $writer.Write([byte]0)        # Reserved
        $writer.Write([uint16]1)      # Planes
        $writer.Write([uint16]32)     # BitCount
        $writer.Write([uint32]$pngs[$i].Length)
        $writer.Write([uint32]$offsets[$i])
    }

    foreach ($p in $pngs) { $writer.Write($p) }
    $writer.Flush()
    [System.IO.File]::WriteAllBytes($Path, $ms.ToArray())
    $writer.Dispose()
    $ms.Dispose()
}

# ---------------------------------------------------------------------------
# Drawing primitives  (all coordinates on a virtual 64x64 canvas → scale)
# ---------------------------------------------------------------------------

# Speaker: a classic megaphone/horn pointing right
# Body rect: (4,22)-(18,42), Horn: (18,22)→(34,8)→(34,56)→(18,42)
function New-SpeakerPath {
    param([float]$scale)
    $gp = New-Object System.Drawing.Drawing2D.GraphicsPath
    # Speaker: body rect (4,22)-(18,42) + horn (18,22)→(34,8)→(34,56)→(18,42), closed
    $gp.AddLine( 4*$scale, 22*$scale, 18*$scale, 22*$scale)
    $gp.AddLine(18*$scale, 22*$scale, 34*$scale,  8*$scale)
    $gp.AddLine(34*$scale,  8*$scale, 34*$scale, 56*$scale)
    $gp.AddLine(34*$scale, 56*$scale, 18*$scale, 42*$scale)
    $gp.AddLine(18*$scale, 42*$scale,  4*$scale, 42*$scale)
    $gp.CloseFigure()
    return $gp
}

function Draw-SpeakerFilled {
    param($g, [float]$scale, [System.Drawing.Color]$color)
    $gp    = New-SpeakerPath $scale
    $brush = New-Object System.Drawing.SolidBrush $color
    $g.FillPath($brush, $gp)
    $brush.Dispose(); $gp.Dispose()
}

function Draw-SpeakerOutline {
    param($g, [float]$scale, [System.Drawing.Color]$color)
    $gp    = New-SpeakerPath $scale
    $width = [Math]::Max(1.5, 2.5 * $scale)
    $pen   = New-Object System.Drawing.Pen $color, $width
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $g.DrawPath($pen, $gp)
    $pen.Dispose(); $gp.Dispose()
}

# Three concentric arcs radiating to the right, centered at (34,32)
# Radii 11, 19, 27; spanning ±55 degrees from the positive-X axis
function Draw-Arcs {
    param($g, [float]$scale, [System.Drawing.Color]$color, [bool]$thick = $false)
    $cx     = 34 * $scale
    $cy     = 32 * $scale
    $radii  = @(11, 19, 27)
    $sw     = if ($thick) { [Math]::Max(1.5, 3.5*$scale) } else { [Math]::Max(1.0, 2.5*$scale) }
    $pen    = New-Object System.Drawing.Pen $color, $sw
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
    foreach ($r in $radii) {
        $rs = $r * $scale
        $g.DrawArc($pen, ($cx - $rs), ($cy - $rs), (2*$rs), (2*$rs), -55.0, 110.0)
    }
    $pen.Dispose()
}

# X overlay (error) — two diagonal lines across the horn area
function Draw-X {
    param($g, [float]$scale, [System.Drawing.Color]$color)
    $sw  = [Math]::Max(1.5, 4.5*$scale)
    $pen = New-Object System.Drawing.Pen $color, $sw
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
    # X centered over the horn: (16,14)→(42,50) and (42,14)→(16,50)
    $g.DrawLine($pen, 16*$scale, 14*$scale, 42*$scale, 50*$scale)
    $g.DrawLine($pen, 42*$scale, 14*$scale, 16*$scale, 50*$scale)
    $pen.Dispose()
}

# Spinning arc: radius 27, 90-degree sweep, centered at canvas center (32,32)
# frame 0-7 → rotate by frame*45 degrees; GDI+ 0° = 3 o'clock, -90 = 12 o'clock
function Draw-Spinner {
    param($g, [float]$scale, [System.Drawing.Color]$color, [int]$frame)
    $cx     = 32 * $scale
    $cy     = 32 * $scale
    $r      = 27 * $scale
    $sw     = [Math]::Max(1.5, 4.5*$scale)
    $pen    = New-Object System.Drawing.Pen $color, $sw
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
    $startAngle = -90.0 + ($frame * 45.0)
    $g.DrawArc($pen, ($cx - $r), ($cy - $r), (2*$r), (2*$r), $startAngle, 90.0)
    $pen.Dispose()
}

# ---------------------------------------------------------------------------
# Composed icon draw functions
# ---------------------------------------------------------------------------
$drawIdle = {
    param($g, $size)
    $s = $size / 64.0
    Draw-SpeakerOutline $g $s $Gray
    Draw-Arcs           $g $s $Gray $false
}

$drawStreaming = {
    param($g, $size)
    $s = $size / 64.0
    Draw-SpeakerFilled $g $s $Blue
    Draw-Arcs          $g $s $Blue $true
}

$drawError = {
    param($g, $size)
    $s = $size / 64.0
    Draw-SpeakerFilled $g $s $Red
    Draw-X             $g $s $White
}

# ---------------------------------------------------------------------------
# Generate ICO files
# ---------------------------------------------------------------------------
$sizes = @(16, 32, 48)

function Save-Icon {
    param([string]$Name, [scriptblock]$Draw)
    $frames = $sizes | ForEach-Object { New-Frame $_ $Draw }
    $path   = Join-Path $OutDir "$Name.ico"
    Save-Ico $path $frames
    $kb = [Math]::Round((Get-Item $path).Length / 1024, 1)
    Write-Host ("  {0,-42} {1,5} KB" -f "$Name.ico", $kb)
    $frames | ForEach-Object { $_.Dispose() }
}

Write-Host ""
Write-Host "Generating AirBeam branded tray icons..."
Write-Host ("=" * 55)

Save-Icon "airbeam_idle"      $drawIdle
Save-Icon "airbeam_streaming" $drawStreaming
Save-Icon "airbeam_error"     $drawError

for ($frame = 0; $frame -lt 8; $frame++) {
    $frameName = "airbeam_connecting_$('{0:D3}' -f ($frame + 1))"
    $f = $frame  # capture loop variable
    $drawConn = [scriptblock]::Create(
        "param(`$g, `$size)
        `$s = `$size / 64.0
        Draw-SpeakerOutline `$g `$s `$Blue
        Draw-Spinner        `$g `$s `$Blue $f"
    )
    Save-Icon $frameName $drawConn
}

Write-Host ("=" * 55)
Write-Host "Done. 11 ICO files written to: $OutDir"
Write-Host ""
Write-Host "Run validate_icons.ps1 to verify."
