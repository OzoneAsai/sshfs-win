[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherDword -Name 'sshfs.delay_connect' -Value 1 `
    -Classes @('sshfs.k', 'sshfs.kr')

Write-Host 'SSHFS-Win delay_connect enabled for public-key launcher classes (sshfs.k/sshfs.kr).'
Write-Host 'Password launcher classes stay eager because their credential bridge requires startup I/O.'
Write-Host 'Reconnect mapped drives to apply the setting.'
