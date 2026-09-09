[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

$logPath = '%P\sshfs-win-diagnostics.log'
Set-SshfsWinLauncherString -Name 'Stderr' -Value $logPath

Write-Host 'SSHFS-Win diagnostics enabled.'
Write-Host 'Reconnect mapped drives to apply the setting.'
Write-Host 'Log path: %USERPROFILE%\sshfs-win-diagnostics.log'
