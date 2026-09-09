[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherDword -Name 'sshfs.handle_warn' -Value 0

Write-Host 'SSHFS-Win remote handle observer disabled.'
Write-Host 'Reconnect mapped drives to apply the setting.'
