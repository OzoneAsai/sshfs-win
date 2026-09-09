Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

Set-SshfsWinLauncherString `
    -Name 'CommandLine' `
    -Value 'svc %1 %2 %U -o create_file_umask=0117,create_dir_umask=0007'

Write-Host 'SSHFS-Win shared file and directory permissions set for all Launcher classes.'
