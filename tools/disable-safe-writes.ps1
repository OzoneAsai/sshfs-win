[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherDword -Name 'sshfs.sync_write' -Value 0

Write-Host 'SSHFS-Win synchronous write safety disabled; asynchronous SSHFS writes restored.'
Write-Host 'This may improve throughput, but write() can complete before the SFTP server reply.'
Write-Host 'Reconnect mapped drives to apply the setting.'
