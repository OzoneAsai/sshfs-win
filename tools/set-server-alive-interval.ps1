Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherCommandLineOptions -Options @{
    ServerAliveInterval = 30
}

Write-Host 'SSHFS-Win keepalive option set for all Launcher classes without replacing unrelated options.'
