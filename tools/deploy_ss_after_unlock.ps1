$ErrorActionPreference = 'Stop'

$sourceDll = 'G:\code\c++\SuperSkillWnd\build\Debug\SS.dll'
$sourcePdb = 'G:\code\c++\SuperSkillWnd\build\Debug\SS.pdb'
$targetDll = 'G:\code\mxd\Data\Plugins\SS\SS.dll'
$targetPdb = 'G:\code\mxd\Data\Plugins\SS\SS.pdb'
$logPath = 'G:\code\c++\SuperSkillWnd\build\deploy_ss_after_unlock.log'

function Write-DeployLog {
    param([string]$Message)
    $timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
    Add-Content -LiteralPath $logPath -Value "[$timestamp] $Message"
}

function Wait-ForProcessExit {
    param([string]$ProcessName)
    while (Get-Process -Name $ProcessName -ErrorAction SilentlyContinue) {
        Start-Sleep -Milliseconds 500
    }
}

function Wait-ForFileUnlock {
    param([string]$Path)
    while ($true) {
        try {
            $stream = [System.IO.File]::Open($Path,
                [System.IO.FileMode]::OpenOrCreate,
                [System.IO.FileAccess]::ReadWrite,
                [System.IO.FileShare]::None)
            $stream.Close()
            return
        } catch {
            Start-Sleep -Milliseconds 500
        }
    }
}

Write-DeployLog 'deploy helper started'
Wait-ForProcessExit -ProcessName 'Maplestory'
Write-DeployLog 'Maplestory exited, waiting for target unlock'
Wait-ForFileUnlock -Path $targetDll

Copy-Item -LiteralPath $sourceDll -Destination $targetDll -Force
Copy-Item -LiteralPath $sourcePdb -Destination $targetPdb -Force

$dllInfo = Get-Item -LiteralPath $targetDll
Write-DeployLog ("deployed SS.dll length={0} lastWrite={1}" -f $dllInfo.Length, $dllInfo.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))
