[CmdletBinding()]
param(
    [ValidateRange(16, 65535)]
    [int]$Handles = 256
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherDword -Name 'sshfs.handle_warn' -Value $Handles

Write-Host "SSHFS-Win remote handle observer enabled at ${Handles}-handle steps."
Write-Host 'The observer only counts successful SFTP OPEN/OPENDIR/CLOSE operations.'
Write-Host 'It never closes, reopens, evicts, terminates, or restarts anything.'
Write-Host 'Reconnect mapped drives to apply the setting.'
