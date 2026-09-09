[CmdletBinding()]
param(
    [ValidateRange(1,86400)]
    [int]$DurationSeconds = 120,
    [ValidateRange(100,60000)]
    [int]$IntervalMilliseconds = 1000,
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'

$rows = [System.Collections.Generic.List[object]]::new()
$deadline = [DateTime]::UtcNow.AddSeconds($DurationSeconds)

while ([DateTime]::UtcNow -lt $deadline) {
    $now = [DateTime]::UtcNow.ToString('o')
    try {
        $processes = @(Get-Process -Name 'sshfs' -ErrorAction Stop)
    } catch {
        if ($_.FullyQualifiedErrorId -match 'NoProcessFoundForGivenName') {
            $processes = @()
        } else {
            throw
        }
    }

    if ($processes.Count -eq 0) {
        $rows.Add([pscustomobject]@{
            TimestampUtc = $now
            ProcessId = $null
            HandleCount = $null
            WorkingSet64 = $null
        })
    } else {
        foreach ($process in $processes) {
            $rows.Add([pscustomobject]@{
                TimestampUtc = $now
                ProcessId = $process.Id
                HandleCount = $process.HandleCount
                WorkingSet64 = $process.WorkingSet64
            })
        }
    }

    Start-Sleep -Milliseconds $IntervalMilliseconds
}

if ($OutputPath) {
    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force -ErrorAction Stop | Out-Null
    }

    # ConvertTo-Csv returns strings. Write them explicitly as UTF-8 without BOM;
    # Windows PowerShell's Export-Csv -Encoding UTF8 emits a BOM.
    $csv = $rows | ConvertTo-Csv -NoTypeInformation
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllLines($OutputPath, $csv, $utf8NoBom)
    Write-Host "Wrote $($rows.Count) samples to $OutputPath"
} else {
    $rows | Format-Table -AutoSize
}
