[CmdletBinding()]
param(
    [string]$OutputPath = "$env:TEMP\sshfs-win-diagnosis.txt",
    [string]$UncPath,
    [ValidatePattern('^[A-Za-z]:$')]
    [string]$Drive = 'Z:',
    [ValidatePattern('^[^-\s][^\s]*$')]
    [string]$SshHost = 'sshfs-win-diagnostic.invalid',
    [switch]$ProbeNetworkProvider
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launcher-registry.ps1')
$lines = [System.Collections.Generic.List[string]]::new()
$failureCount = 0

function Add-Line([string]$Text = '') {
    $lines.Add($Text)
    Write-Host $Text
}

function Add-Section([string]$Title) {
    Add-Line
    Add-Line "=== $Title ==="
}

function Add-Fail([string]$Text) {
    $script:failureCount++
    Add-Line "FAIL: $Text"
}

function Invoke-CapturedProcess {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [string[]]$ArgumentList = @()
    )

    $stdoutPath = [IO.Path]::GetTempFileName()
    $stderrPath = [IO.Path]::GetTempFileName()
    try {
        $start = @{
            FilePath = $FilePath
            NoNewWindow = $true
            Wait = $true
            PassThru = $true
            RedirectStandardOutput = $stdoutPath
            RedirectStandardError = $stderrPath
            ErrorAction = 'Stop'
        }
        if ($ArgumentList.Count -gt 0) {
            $start['ArgumentList'] = $ArgumentList
        }

        $process = Start-Process @start
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            StdOut = @(Get-Content -LiteralPath $stdoutPath -ErrorAction Stop)
            StdErr = @(Get-Content -LiteralPath $stderrPath -ErrorAction Stop)
        }
    } finally {
        foreach ($path in @($stdoutPath, $stderrPath)) {
            if (Test-Path -LiteralPath $path) {
                Remove-Item -LiteralPath $path -Force -ErrorAction Stop
            }
        }
    }
}

function Get-RegValueResult([string]$Path, [string]$Name) {
    if (-not (Test-Path -LiteralPath $Path)) {
        return [pscustomobject]@{
            Status = 'KeyMissing'
            Value = $null
            Error = $null
        }
    }

    try {
        $item = Get-ItemProperty -LiteralPath $Path -ErrorAction Stop
        $property = $item.PSObject.Properties[$Name]
        if ($null -eq $property) {
            return [pscustomobject]@{
                Status = 'Missing'
                Value = $null
                Error = $null
            }
        }
        return [pscustomobject]@{
            Status = 'Present'
            Value = $property.Value
            Error = $null
        }
    } catch {
        return [pscustomobject]@{
            Status = 'Error'
            Value = $null
            Error = $_.Exception.Message
        }
    }
}

Add-Line "SSHFS-Win Windows 11 diagnostic"
Add-Line "Generated: $(Get-Date -Format o)"
Add-Line "User: $env:USERDOMAIN\$env:USERNAME"
Add-Line "UserProfile: $env:USERPROFILE"

Add-Section 'Windows'
try {
    $os = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
    Add-Line "Caption: $($os.Caption)"
    Add-Line "Version: $($os.Version)"
    Add-Line "Build: $($os.BuildNumber)"
} catch {
    Add-Fail "OS query failed: $($_.Exception.Message)"
}

Add-Section 'WinFsp services'
foreach ($name in @('WinFsp.Launcher', 'WinFsp')) {
    try {
        $svc = Get-Service -Name $name -ErrorAction Stop
        Add-Line "$name status=$($svc.Status) startType=$($svc.StartType)"
        if ($name -eq 'WinFsp.Launcher' -and $svc.Status -ne 'Running') {
            Add-Fail "$name service is not running (status=$($svc.Status))"
        } elseif ($name -eq 'WinFsp' -and $svc.Status -eq 'Stopped') {
            Add-Line '  INFO: WinFsp file-system driver is demand-start; Stopped is not by itself a failure'
        }
    } catch {
        if ($_.FullyQualifiedErrorId -match 'NoServiceFoundForGivenName') {
            Add-Fail "$name service not found"
        } else {
            Add-Fail "$name service query failed: $($_.Exception.Message)"
        }
    }
}

Add-Section 'Network Provider'
$providerOrderPath = 'HKLM:\SYSTEM\CurrentControlSet\Control\NetworkProvider\Order'
$orderResult = Get-RegValueResult $providerOrderPath 'ProviderOrder'
if ($orderResult.Status -eq 'Present') {
    $order = [string]$orderResult.Value
    Add-Line "ProviderOrder: $order"
    if (($order -split ',') -notcontains 'WinFsp.Np') {
        Add-Fail 'WinFsp.Np is absent from ProviderOrder'
    }
} elseif ($orderResult.Status -eq 'Missing' -or $orderResult.Status -eq 'KeyMissing') {
    Add-Fail 'ProviderOrder is missing'
} else {
    Add-Fail "ProviderOrder could not be read: $($orderResult.Error)"
}

$np = 'HKLM:\SYSTEM\CurrentControlSet\Services\WinFsp.Np\NetworkProvider'
foreach ($n in @('Name','ProviderPath','DeviceName')) {
    $r = Get-RegValueResult $np $n
    if ($r.Status -eq 'Present') {
        Add-Line "WinFsp.Np.$n: $($r.Value)"
    } elseif ($r.Status -eq 'Error') {
        Add-Fail "WinFsp.Np.$n query failed: $($r.Error)"
    } else {
        Add-Fail "WinFsp.Np.$n is missing"
    }
}

Add-Section 'SSHFS launcher registrations'
$servicesDisplay = Get-SshfsWinLauncherServiceDisplayPath
Add-Line "Registry view: $servicesDisplay"

foreach ($cls in @('sshfs','sshfs.r','sshfs.k','sshfs.kr')) {
    Add-Line "[$cls]"

    $values = @{}
    $required = @(
        'Executable','CommandLine','RunAs','Credentials','JobControl',
        'sshfs.reconnect','sshfs.delay_connect','sshfs.sync_write',
        'sshfs.stall_warn','sshfs.handle_warn','sshfs.handle_limit','sshfs.max_conns'
    )
    $optional = @('Stderr','sshfs.rootdir')

    foreach ($name in ($required + $optional)) {
        $r = Get-SshfsWinLauncherValueResult -Class $cls -Name $name
        if ($r.Status -eq 'Present') {
            $values[$name] = $r.Value
            Add-Line "  $name=$($r.Value)"
        } elseif ($r.Status -eq 'Error') {
            Add-Fail "$cls.$name query failed: $($r.Error)"
        } elseif ($r.Status -eq 'KeyMissing') {
            Add-Fail "$cls launcher registry class is missing: $($r.Error)"
            break
        } elseif ($required -contains $name) {
            Add-Fail "$cls.$name is missing"
        }
    }

    if ($values.ContainsKey('RunAs') -and [string]$values['RunAs'] -ne '.') {
        Add-Fail "$cls.RunAs must be '.' so Launcher uses the requesting Windows user"
    }

    if ($values.ContainsKey('Executable') -and [string]::IsNullOrWhiteSpace([string]$values['Executable'])) {
        Add-Fail "$cls.Executable is empty"
    }
    if ($values.ContainsKey('CommandLine') -and [string]::IsNullOrWhiteSpace([string]$values['CommandLine'])) {
        Add-Fail "$cls.CommandLine is empty"
    }

    $expectedCredentials = if ($cls -in @('sshfs','sshfs.r')) { 1 } else { 0 }
    if ($values.ContainsKey('Credentials') -and
        [int64]$values['Credentials'] -ne $expectedCredentials) {
        Add-Fail "$cls.Credentials=$($values['Credentials']) expected=$expectedCredentials"
    }
    if ($values.ContainsKey('JobControl') -and [int64]$values['JobControl'] -ne 1) {
        Add-Fail "$cls.JobControl=$($values['JobControl']) expected=1"
    }

    foreach ($name in @('sshfs.reconnect','sshfs.delay_connect','sshfs.sync_write')) {
        if ($values.ContainsKey($name)) {
            $v = [int64]$values[$name]
            if ($v -ne 0 -and $v -ne 1) {
                Add-Fail "$cls.$name=$v expected 0 or 1"
            }
        }
    }

    if ($values.ContainsKey('sshfs.stall_warn')) {
        $v = [int64]$values['sshfs.stall_warn']
        if ($v -ne 0 -and ($v -lt 5 -or $v -gt 3600)) {
            Add-Fail "$cls.sshfs.stall_warn=$v expected 0 or 5..3600"
        }
    }
    if ($values.ContainsKey('sshfs.handle_warn')) {
        $v = [int64]$values['sshfs.handle_warn']
        if ($v -ne 0 -and ($v -lt 16 -or $v -gt 65535)) {
            Add-Fail "$cls.sshfs.handle_warn=$v expected 0 or 16..65535"
        }
    }
    if ($values.ContainsKey('sshfs.handle_limit')) {
        $v = [int64]$values['sshfs.handle_limit']
        if ($v -ne 0 -and ($v -lt 32 -or $v -gt 65535)) {
            Add-Fail "$cls.sshfs.handle_limit=$v expected 0 or 32..65535"
        }
    }
    if ($values.ContainsKey('sshfs.max_conns')) {
        $v = [int64]$values['sshfs.max_conns']
        if ($v -lt 1 -or $v -gt 16) {
            Add-Fail "$cls.sshfs.max_conns=$v expected 1..16"
        } elseif ($expectedCredentials -eq 1 -and $v -ne 1) {
            Add-Fail "$cls.sshfs.max_conns=$v is incompatible with the password credential bridge"
        }
    }
    if ($values.ContainsKey('sshfs.delay_connect') -and
        $expectedCredentials -eq 1 -and [int64]$values['sshfs.delay_connect'] -ne 0) {
        Add-Fail "$cls.sshfs.delay_connect must be 0 for password launcher classes"
    }

    $expectedRoot = if ($cls -in @('sshfs.r','sshfs.kr')) { 1 } else { 0 }
    if ($values.ContainsKey('sshfs.rootdir')) {
        $rootValue = [int64]$values['sshfs.rootdir']
        if ($rootValue -ne $expectedRoot) {
            Add-Fail "$cls.sshfs.rootdir=$rootValue expected=$expectedRoot"
        }
    } elseif ($expectedRoot -eq 1) {
        Add-Fail "$cls.sshfs.rootdir is missing"
    }
}

Add-Section 'Installed binaries'
$sshfsHome = Join-Path $env:ProgramFiles 'SSHFS-Win'
$bin = Join-Path $sshfsHome 'bin'
foreach ($name in @('sshfs-win.exe','sshfs.exe','ssh.exe')) {
    $path = Join-Path $bin $name
    if (Test-Path -LiteralPath $path) {
        try {
            $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256 -ErrorAction Stop).Hash
            $ver = (Get-Item -LiteralPath $path -ErrorAction Stop).VersionInfo.FileVersion
            Add-Line "$name present version=$ver sha256=$hash"
        } catch {
            Add-Fail "$name metadata query failed: $($_.Exception.Message)"
        }
    } else {
        Add-Fail "missing $path"
    }
}

$sshExe = Join-Path $bin 'ssh.exe'
if (Test-Path -LiteralPath $sshExe) {
    try {
        $versionResult = Invoke-CapturedProcess -FilePath $sshExe -ArgumentList @('-V')
        $versionOutput = @($versionResult.StdOut) + @($versionResult.StdErr)
        Add-Line "bundled ssh -V exit=$($versionResult.ExitCode) output=$($versionOutput -join ' ')"
        if ($versionResult.ExitCode -ne 0) {
            Add-Fail "bundled ssh -V returned exit code $($versionResult.ExitCode)"
        }
    } catch {
        Add-Fail "bundled ssh -V failed: $($_.Exception.Message)"
    }
}

Add-Section 'Effective OpenSSH configuration'
if (Test-Path -LiteralPath $sshExe) {
    try {
        $configResult = Invoke-CapturedProcess -FilePath $sshExe `
            -ArgumentList @('-G', '-o', 'BatchMode=yes', $SshHost)
        Add-Line "ssh -G host=$SshHost exit=$($configResult.ExitCode)"
        if ($configResult.ExitCode -ne 0) {
            Add-Fail "bundled ssh configuration expansion failed with exit code $($configResult.ExitCode)"
            (@($configResult.StdOut) + @($configResult.StdErr)) |
                Select-Object -First 12 |
                ForEach-Object { Add-Line "  $_" }
        } else {
            foreach ($line in $configResult.StdErr) {
                Add-Line "  ssh stderr: $line"
            }
            $interesting = @(
                'hostname','batchmode','stricthostkeychecking','connecttimeout',
                'identityagent','identityfile','userknownhostsfile','globalknownhostsfile',
                'proxycommand','proxyjump'
            )
            foreach ($line in $configResult.StdOut) {
                $name = ([string]$line -split '\s+', 2)[0].ToLowerInvariant()
                if ($interesting -contains $name) {
                    Add-Line "  $line"
                }
            }
        }
    } catch {
        Add-Fail "bundled ssh configuration expansion failed: $($_.Exception.Message)"
    }
}

Add-Section 'User SSH configuration'
$sshDir = Join-Path $env:USERPROFILE '.ssh'
foreach ($name in @('config','known_hosts','id_ed25519','id_rsa')) {
    $p = Join-Path $sshDir $name
    Add-Line "$name: $(if (Test-Path -LiteralPath $p) {'present'} else {'absent'})"
}

Add-Section 'Exploit protection'
$mitigationCommand = $null
try {
    $mitigationCommand = Get-Command Get-ProcessMitigation -ErrorAction Stop
} catch {
    Add-Line "Get-ProcessMitigation unavailable: $($_.Exception.Message)"
}
if ($null -ne $mitigationCommand) {
    foreach ($name in @('sshfs.exe','sshfs-win.exe','ssh.exe')) {
        try {
            Add-Line "[$name]"
            $m = Get-ProcessMitigation -Name $name -ErrorAction Stop | Out-String
            foreach ($l in ($m -split "`r?`n")) {
                if ($l -match 'ASLR|ForceRelocateImages|BottomUp|HighEntropy') {
                    Add-Line "  $($l.Trim())"
                }
            }
        } catch {
            if ($_.Exception.Message -match 'not found|cannot find') {
                Add-Line "  no per-image mitigation entry"
            } else {
                Add-Line "  mitigation query failed: $($_.Exception.Message)"
            }
        }
    }
}

Add-Section 'Recent WinFsp event log'
try {
    $events = Get-WinEvent -FilterHashtable @{
        LogName='Application'
        ProviderName='WinFsp'
        StartTime=(Get-Date).AddHours(-24)
    } -MaxEvents 40 -ErrorAction Stop
    foreach ($event in $events) {
        $msg = ($event.Message -replace "`r?`n", ' ')
        Add-Line "$($event.TimeCreated.ToString('o')) id=$($event.Id) level=$($event.LevelDisplayName) $msg"
    }
} catch {
    Add-Line "WinFsp event query unavailable/empty: $($_.Exception.Message)"
}

Add-Section 'Wrapper smoke'
$wrapper = Join-Path $bin 'sshfs-win.exe'
if (Test-Path -LiteralPath $wrapper) {
    try {
        $wrapperResult = Invoke-CapturedProcess -FilePath $wrapper
        Add-Line "no-arg exitCode=$($wrapperResult.ExitCode)"
        if ($wrapperResult.ExitCode -ne 2) {
            Add-Fail "wrapper no-argument contract expected exit code 2, got $($wrapperResult.ExitCode)"
        }
        $wrapperResult.StdErr |
            Select-Object -First 8 |
            ForEach-Object { Add-Line "  $_" }
    } catch {
        Add-Fail "wrapper smoke failed: $($_.Exception.Message)"
    }
}

if ($ProbeNetworkProvider) {
    Add-Section 'Network Provider probe'
    if ([string]::IsNullOrWhiteSpace($UncPath)) {
        Add-Line 'SKIP: -ProbeNetworkProvider requires -UncPath'
    } else {
        Add-Line "Drive=$Drive UNC=$UncPath"

        $driveRoot = "$Drive\"
        if ([Environment]::GetLogicalDrives() -contains $driveRoot) {
            Add-Fail "$Drive is already in use; choose a free drive letter for the probe"
        } else {
            $probe = & cmd.exe /c "net use $Drive `"$UncPath`"" 2>&1
            $rc = $LASTEXITCODE
            $probe | ForEach-Object { Add-Line "  $_" }
            Add-Line "net-use-exit=$rc"
            if ($rc -eq 0) {
                $cleanup = & cmd.exe /c "net use $Drive /delete /y" 2>&1
                $cleanupRc = $LASTEXITCODE
                $cleanup | ForEach-Object { Add-Line "  $_" }
                Add-Line "post-cleanup-exit=$cleanupRc"
                if ($cleanupRc -ne 0) {
                    Add-Fail "probe mapping succeeded but $Drive could not be removed automatically"
                }
            } else {
                Add-Fail 'Network Provider mount probe failed'
                Add-Line 'Compare this report with the SSHFS-Win launcher stderr diagnostics.'
            }
        }
    }
}

Add-Section 'Summary'
if ($failureCount -eq 0) {
    Add-Line 'Result: PASS'
} else {
    Add-Line "Result: FAIL ($failureCount required checks failed)"
}

$parent = Split-Path -Parent $OutputPath
if ($parent -and -not (Test-Path -LiteralPath $parent)) {
    New-Item -ItemType Directory -Path $parent -Force -ErrorAction Stop | Out-Null
}

# Windows PowerShell's `-Encoding utf8` writes a BOM. Emit UTF-8 explicitly
# without a BOM so reports are stable across PowerShell editions.
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllLines($OutputPath, $lines, $utf8NoBom)
Write-Host
Write-Host "Saved report: $OutputPath"

if ($failureCount -ne 0) {
    exit 1
}
