[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherDword -Name 'sshfs.reconnect' -Value 1

Write-Host 'SSHFS-Win automatic reconnect enabled.'
Write-Host 'Reconnect mapped drives to apply the setting.'
Write-Host 'Open file handles from before a transport interruption are not preserved by SSHFS reconnect.'
