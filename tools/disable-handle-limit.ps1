[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherDword -Name 'sshfs.handle_limit' -Value 0

Write-Host 'SSHFS-Win remote handle admission limit disabled.'
Write-Host 'Reconnect mapped drives to apply the setting.'
