[CmdletBinding()]
param(
    [ValidateRange(5, 3600)]
    [int]$Seconds = 60
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherDword -Name 'sshfs.stall_warn' -Value $Seconds

Write-Host "SSHFS-Win stall observer enabled at ${Seconds}s."
Write-Host 'This observer only logs; it never terminates or restarts a mount.'
Write-Host 'Reconnect mapped drives to apply the setting.'
