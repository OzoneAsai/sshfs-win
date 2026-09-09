$ErrorActionPreference = 'Stop'

$logPath = Join-Path $env:USERPROFILE 'sshfs-win-diagnostics.log'

if (-not (Test-Path $logPath)) {
    Write-Host "No diagnostics log found at $logPath"
    exit 1
}

Get-Content -Path $logPath -Tail 200
