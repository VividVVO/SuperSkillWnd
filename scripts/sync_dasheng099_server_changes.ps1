param(
    [string]$SourceRoot = 'G:\code\dasheng099',
    [string]$TargetRoot = 'C:\Users\Administrator\Desktop\dasheng099'
)

$files = @(
    'src\constants\ServerConfig.java',
    'src\tools\packet\MaplePacketCreator.java',
    'src\client\MapleBeans.java',
    'src\client\MapleCharacter.java',
    'src\handling\channel\handler\PlayerHandler.java',
    'src\server\MapleStatEffect.java',
    'src\handling\world\PlayerBuffStorage.java',
    'src\handling\channel\handler\InterServerHandler.java'
)

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

foreach ($rel in $files) {
    $src = Join-Path $SourceRoot $rel
    $dst = Join-Path $TargetRoot $rel

    if (-not (Test-Path $src)) {
        Write-Host "[MISS] source not found: $src" -ForegroundColor Red
        continue
    }

    $dstDir = Split-Path -Parent $dst
    if (-not (Test-Path $dstDir)) {
        New-Item -ItemType Directory -Path $dstDir -Force | Out-Null
    }

    $text = Get-Content -Raw -LiteralPath $src
    [System.IO.File]::WriteAllText($dst, $text, $utf8NoBom)
    Write-Host "[SYNC] $rel" -ForegroundColor Green
}

Write-Host "Done. Now rebuild the NetBeans project from: $TargetRoot" -ForegroundColor Cyan
