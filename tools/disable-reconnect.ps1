[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherDword -Name 'sshfs.reconnect' -Value 0

Write-Host 'SSHFS-Win automatic reconnect disabled.'
Write-Host 'Reconnect mapped drives to apply the setting.'
