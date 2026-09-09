[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Remove-SshfsWinLauncherValue -Name 'Stderr'

Write-Host 'SSHFS-Win diagnostics disabled.'
Write-Host 'Reconnect mapped drives to apply the setting.'
