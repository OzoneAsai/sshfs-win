$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
Assert-SshfsWinAdministrator

$sourceBase = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
    [Microsoft.Win32.RegistryHive]::LocalMachine,
    [Microsoft.Win32.RegistryView]::Registry64
)
$destinationBase = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
    [Microsoft.Win32.RegistryHive]::LocalMachine,
    [Microsoft.Win32.RegistryView]::Registry32
)

try {
    foreach ($class in Get-SshfsWinLauncherClasses) {
        $subPath = "SOFTWARE\WinFsp\Services\$class"
        $sourceKey = $sourceBase.OpenSubKey($subPath, $false)
        if ($null -eq $sourceKey) {
            throw "Source launcher registration is missing from the 64-bit view: $class"
        }

        try {
            $destinationKey = $destinationBase.CreateSubKey($subPath, $true)
            if ($null -eq $destinationKey) {
                throw "Cannot create destination launcher registration in the 32-bit view: $class"
            }

            try {
                foreach ($name in $sourceKey.GetValueNames()) {
                    $kind = $sourceKey.GetValueKind($name)
                    $value = $sourceKey.GetValue(
                        $name,
                        $null,
                        [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames
                    )
                    $destinationKey.SetValue($name, $value, $kind)
                }
            } finally {
                $destinationKey.Dispose()
            }
        } finally {
            $sourceKey.Dispose()
        }
    }
} finally {
    $destinationBase.Dispose()
    $sourceBase.Dispose()
}

Write-Host 'Copied SSHFS-Win Launcher test registrations from Registry64 to Registry32.'
