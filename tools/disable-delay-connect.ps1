[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherDword -Name 'sshfs.delay_connect' -Value 0

Write-Host 'SSHFS-Win delay_connect disabled; eager SSH startup restored.'
Write-Host 'Reconnect mapped drives to apply the setting.'
