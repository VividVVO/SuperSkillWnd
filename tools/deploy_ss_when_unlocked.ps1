param(
    [string]$Source = "G:\code\c++\SuperSkillWnd\build\Debug\SS.dll",
    [string]$Destination = "G:\code\mxd\Data\Plugins\SS\SS.dll",
    [string]$Marker = "C:\SuperSkillWnd_deploy.txt",
    [int]$MaxAttempts = 7200,
    [int]$SleepMs = 500
)

$copied = $false
for ($i = 0; $i -lt $MaxAttempts -and -not $copied; $i++)
{
    try
    {
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
        $item = Get-Item -LiteralPath $Destination -ErrorAction Stop
        $hash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256 -ErrorAction Stop).Hash
        "DEPLOYED $($item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')) len=$($item.Length) sha256=$hash" |
            Out-File -LiteralPath $Marker -Encoding utf8 -Force
        $copied = $true
    }
    catch
    {
        Start-Sleep -Milliseconds $SleepMs
    }
}

if (-not $copied)
{
    "DEPLOY FAILED $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" |
        Out-File -LiteralPath $Marker -Encoding utf8 -Force
}
