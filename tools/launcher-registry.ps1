Set-StrictMode -Version Latest

function Assert-SshfsWinAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Administrator privileges are required to change SSHFS-Win Launcher settings. Open PowerShell as Administrator and run the command again.'
    }
}

function Get-SshfsWinLauncherClasses {
    return @('sshfs', 'sshfs.r', 'sshfs.k', 'sshfs.kr')
}

function Get-SshfsWinLauncherServiceDisplayPath {
    return 'HKLM\SOFTWARE (32-bit view)\WinFsp\Services'
}

function Open-SshfsWinLauncherBaseKey {
    param(
        [Parameter(Mandatory = $true)]
        [bool]$Writable
    )

    return [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine,
        [Microsoft.Win32.RegistryView]::Registry32
    )
}

function Invoke-SshfsWinLauncherClassKey {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('sshfs', 'sshfs.r', 'sshfs.k', 'sshfs.kr')]
        [string]$Class,
        [Parameter(Mandatory = $true)]
        [bool]$Writable,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Operation
    )

    $base = Open-SshfsWinLauncherBaseKey -Writable $Writable
    try {
        $subPath = "SOFTWARE\WinFsp\Services\$Class"
        $key = $base.OpenSubKey($subPath, $Writable)
        if ($null -eq $key) {
            throw "WinFsp service registry key not found: $(Get-SshfsWinLauncherServiceDisplayPath)\$Class"
        }
        try {
            return & $Operation $key
        } finally {
            $key.Dispose()
        }
    } finally {
        $base.Dispose()
    }
}

function Get-SshfsWinLauncherValueResult {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('sshfs', 'sshfs.r', 'sshfs.k', 'sshfs.kr')]
        [string]$Class,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    try {
        return Invoke-SshfsWinLauncherClassKey -Class $Class -Writable $false -Operation {
            param($key)
            if ($key.GetValueNames() -notcontains $Name) {
                return [pscustomobject]@{
                    Status = 'Missing'
                    Value = $null
                    Error = $null
                }
            }
            return [pscustomobject]@{
                Status = 'Present'
                Value = $key.GetValue(
                    $Name,
                    $null,
                    [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames
                )
                Error = $null
            }
        }
    } catch {
        $status = if ($_.Exception.Message -like 'WinFsp service registry key not found:*') {
            'KeyMissing'
        } else {
            'Error'
        }
        return [pscustomobject]@{
            Status = $status
            Value = $null
            Error = $_.Exception.Message
        }
    }
}

function Set-SshfsWinLauncherDword {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [uint32]$Value,
        [string[]]$Classes = @()
    )

    if ($Classes.Count -eq 0) {
        $Classes = @(Get-SshfsWinLauncherClasses)
    }

    foreach ($class in $Classes) {
        Invoke-SshfsWinLauncherClassKey -Class $class -Writable $true -Operation {
            param($key)
            $key.SetValue($Name, $Value, [Microsoft.Win32.RegistryValueKind]::DWord)
            $actual = $key.GetValue($Name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            if ($null -eq $actual -or [uint32]$actual -ne $Value) {
                throw "Launcher setting verification failed: $class\$Name expected=$Value actual=$actual"
            }
        } | Out-Null
    }
}

function Set-SshfsWinLauncherString {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [string]$Value,
        [string[]]$Classes = @()
    )

    if ($Classes.Count -eq 0) {
        $Classes = @(Get-SshfsWinLauncherClasses)
    }

    foreach ($class in $Classes) {
        Invoke-SshfsWinLauncherClassKey -Class $class -Writable $true -Operation {
            param($key)
            $key.SetValue($Name, $Value, [Microsoft.Win32.RegistryValueKind]::String)
            $actual = [string]$key.GetValue(
                $Name,
                $null,
                [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames
            )
            if ($actual -ne $Value) {
                throw "Launcher setting verification failed: $class\$Name expected='$Value' actual='$actual'"
            }
        } | Out-Null
    }
}

function Remove-SshfsWinLauncherValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [string[]]$Classes = @()
    )

    if ($Classes.Count -eq 0) {
        $Classes = @(Get-SshfsWinLauncherClasses)
    }

    foreach ($class in $Classes) {
        Invoke-SshfsWinLauncherClassKey -Class $class -Writable $true -Operation {
            param($key)
            if ($key.GetValueNames() -contains $Name) {
                $key.DeleteValue($Name, $true)
                if ($key.GetValueNames() -contains $Name) {
                    throw "Launcher setting removal verification failed: $class\$Name"
                }
            }
        } | Out-Null
    }
}
