[CmdletBinding()]
param(
    [ValidateRange(2, 16)]
    [int]$Connections = 4
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherDword -Name 'sshfs.max_conns' -Value $Connections `
    -Classes @('sshfs.k', 'sshfs.kr')

Write-Host "SSHFS-Win max_conns set to $Connections."
Write-Host 'Password launcher classes remain at max_conns=1 because the credential bridge is single-connection.'
Write-Host 'Reconnect mapped drives to apply the setting.'
