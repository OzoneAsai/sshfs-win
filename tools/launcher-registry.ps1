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
                    Kind = $null
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
                Kind = $key.GetValueKind($Name)
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
            Kind = $null
            Error = $_.Exception.Message
        }
    }
}

function Resolve-SshfsWinLauncherClasses {
    param([string[]]$Classes = @())

    if ($Classes.Count -eq 0) {
        return @(Get-SshfsWinLauncherClasses)
    }
    return @($Classes)
}

function Invoke-SshfsWinLauncherValueTransaction {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [string[]]$Classes = @(),
        [Parameter(Mandatory = $true)]
        [scriptblock]$Operation
    )

    $resolvedClasses = @(Resolve-SshfsWinLauncherClasses -Classes $Classes)
    $snapshots = @{}

    # Read every participant before the first write. A missing/inaccessible key
    # therefore fails the operation without leaving a partially updated class set.
    foreach ($class in $resolvedClasses) {
        $snapshot = Get-SshfsWinLauncherValueResult -Class $class -Name $Name
        if ($snapshot.Status -notin @('Present', 'Missing')) {
            throw "Cannot start Launcher registry transaction for $class\$Name: $($snapshot.Error)"
        }
        $snapshots[$class] = $snapshot
    }

    $modified = [System.Collections.Generic.List[string]]::new()
    try {
        foreach ($class in $resolvedClasses) {
            Invoke-SshfsWinLauncherClassKey -Class $class -Writable $true -Operation {
                param($key)
                & $Operation $key $class
            } | Out-Null
            $modified.Add($class)
        }
    } catch {
        $originalError = $_.Exception.Message
        $rollbackErrors = [System.Collections.Generic.List[string]]::new()

        for ($i = $modified.Count - 1; $i -ge 0; $i--) {
            $class = $modified[$i]
            $snapshot = $snapshots[$class]
            try {
                Invoke-SshfsWinLauncherClassKey -Class $class -Writable $true -Operation {
                    param($key)
                    if ($snapshot.Status -eq 'Present') {
                        $key.SetValue($Name, $snapshot.Value, $snapshot.Kind)
                    } elseif ($key.GetValueNames() -contains $Name) {
                        $key.DeleteValue($Name, $false)
                    }
                } | Out-Null
            } catch {
                $rollbackErrors.Add("${class}: $($_.Exception.Message)")
            }
        }

        if ($rollbackErrors.Count -ne 0) {
            throw "Launcher registry transaction failed: $originalError; rollback failures: $($rollbackErrors -join '; ')"
        }
        throw "Launcher registry transaction failed and was rolled back: $originalError"
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

    Invoke-SshfsWinLauncherValueTransaction -Name $Name -Classes $Classes -Operation {
        param($key, $class)
        $key.SetValue($Name, $Value, [Microsoft.Win32.RegistryValueKind]::DWord)
        $actual = $key.GetValue($Name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        if ($null -eq $actual -or [uint32]$actual -ne $Value) {
            throw "Launcher setting verification failed: $class\$Name expected=$Value actual=$actual"
        }
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

    Invoke-SshfsWinLauncherValueTransaction -Name $Name -Classes $Classes -Operation {
        param($key, $class)
        $key.SetValue($Name, $Value, [Microsoft.Win32.RegistryValueKind]::String)
        $actual = [string]$key.GetValue(
            $Name,
            $null,
            [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames
        )
        if ($actual -ne $Value) {
            throw "Launcher setting verification failed: $class\$Name expected='$Value' actual='$actual'"
        }
    }
}

function Remove-SshfsWinLauncherValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [string[]]$Classes = @()
    )

    Invoke-SshfsWinLauncherValueTransaction -Name $Name -Classes $Classes -Operation {
        param($key, $class)
        if ($key.GetValueNames() -contains $Name) {
            $key.DeleteValue($Name, $true)
        }
        if ($key.GetValueNames() -contains $Name) {
            throw "Launcher setting removal verification failed: $class\$Name"
        }
    }
}

function Set-SshfsWinLauncherCommandLineOptions {
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$Options,
        [string[]]$Classes = @()
    )

    if ($Options.Count -eq 0) {
        throw 'At least one SSHFS option is required.'
    }

    $resolvedClasses = @(Resolve-SshfsWinLauncherClasses -Classes $Classes)
    $targets = @{}

    foreach ($class in $resolvedClasses) {
        $result = Get-SshfsWinLauncherValueResult -Class $class -Name 'CommandLine'
        if ($result.Status -ne 'Present' -or [string]::IsNullOrWhiteSpace([string]$result.Value)) {
            throw "Cannot update $class\CommandLine: value is not present and readable."
        }

        $commandLine = ([string]$result.Value).Trim()
        foreach ($entry in $Options.GetEnumerator()) {
            $name = [regex]::Escape([string]$entry.Key)
            # Supported Launcher options are scalar -o name=value tokens. Remove
            # both '-oname=value' and '-o name=value' spellings before appending
            # the canonical form, while preserving every unrelated option.
            $pattern = "(?i)(?<!\S)-o(?:\s+)?$name=[^\s]+"
            $commandLine = [regex]::Replace($commandLine, $pattern, '')
            $commandLine = ([regex]::Replace($commandLine, '\s+', ' ')).Trim()
            $commandLine += " -o $($entry.Key)=$($entry.Value)"
        }
        $targets[$class] = $commandLine
    }

    Invoke-SshfsWinLauncherValueTransaction -Name 'CommandLine' -Classes $resolvedClasses -Operation {
        param($key, $class)
        $target = [string]$targets[$class]
        $key.SetValue('CommandLine', $target, [Microsoft.Win32.RegistryValueKind]::String)
        $actual = [string]$key.GetValue(
            'CommandLine',
            $null,
            [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames
        )
        if ($actual -ne $target) {
            throw "Launcher CommandLine verification failed: $class expected='$target' actual='$actual'"
        }
    }
}
