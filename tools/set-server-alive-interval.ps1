Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherString `
    -Name 'CommandLine' `
    -Value 'svc %1 %2 %U -o ServerAliveInterval=30'

Write-Host 'SSHFS-Win keepalive option set for all Launcher classes.'
