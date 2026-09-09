[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherDword -Name 'sshfs.stall_warn' -Value 0

Write-Host 'SSHFS-Win stall observer disabled.'
Write-Host 'Reconnect mapped drives to apply the setting.'
