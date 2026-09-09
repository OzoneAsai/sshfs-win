[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherDword -Name 'sshfs.max_conns' -Value 1

Write-Host 'SSHFS-Win max_conns reset to 1.'
Write-Host 'Reconnect mapped drives to apply the setting.'
