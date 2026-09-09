[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherDword -Name 'sshfs.sync_write' -Value 1

Write-Host 'SSHFS-Win safe synchronous writes enabled (sshfs_sync).'
Write-Host 'Reconnect mapped drives to apply the setting.'
