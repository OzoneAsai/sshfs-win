[CmdletBinding()]
param(
    [ValidateRange(32, 65535)]
    [int]$Handles = 512
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherDword -Name 'sshfs.handle_limit' -Value $Handles

Write-Host "SSHFS-Win remote handle admission limit enabled at ${Handles} handles per SSH connection."
Write-Host 'At the limit, new SFTP OPEN/OPENDIR operations fail with EMFILE.'
Write-Host 'Existing open handles are never closed or reopened by this policy.'
Write-Host 'Reconnect mapped drives to apply the setting.'
