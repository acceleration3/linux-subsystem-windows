param(
    [switch] $Install,
    [string] $Shares
)

$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'

$InstallDir = 'C:\Program Files\LSW\virtiofs'
$SavedList = Join-Path $InstallDir 'shares.txt'
$LogDir = 'C:\ProgramData\LSW\logs'
$Log = Join-Path $LogDir 'shares.log'
$TaskName = 'lsw-shares'
$FsClass = 'virtiofs'
$DeviceMatch = 'PCI\VEN_1AF4&DEV_105A*'
$LauncherSddl = 'D:P(A;;RPWPLC;;;SY)(A;;RPWPLC;;;BA)'

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

function Roll-Log([string] $LogFile) {
    try {
        if ((Test-Path -LiteralPath $LogFile) -and (Get-Item -LiteralPath $LogFile).Length -gt 2MB) {
            $stem = $LogFile.Substring(0, $LogFile.Length - 4)
            Remove-Item -LiteralPath ($stem + '.2.log') -Force -ErrorAction SilentlyContinue
            if (Test-Path -LiteralPath ($stem + '.1.log')) {
                Move-Item -LiteralPath ($stem + '.1.log') -Destination ($stem + '.2.log') -Force
            }
            Move-Item -LiteralPath $LogFile -Destination ($stem + '.1.log') -Force
        }
    } catch { }
}

Roll-Log $Log

function L([string] $Message) {
    ('{0} {1}' -f (Get-Date -Format s), $Message) | Add-Content -LiteralPath $Log -Encoding Ascii
}

$script:Result = 0
function Fail([int] $Code, [string] $Message) {
    L ('FAILED: ' + $Message)
    if ($script:Result -eq 0) { $script:Result = $Code }
}

function Parse-Shares([string] $Text) {
    $list = @()
    if ($Text -eq 'none' -or [string]::IsNullOrWhiteSpace($Text)) { return ,$list }
    foreach ($item in ($Text -split '[,\r\n]+')) {
        $item = $item.Trim()
        if (-not $item) { continue }
        if ($item -notmatch '^([A-Za-z0-9_.-]+)=([A-Za-z]):?(?:=([A-Za-z0-9+/]*={0,2}))?$') {
            Fail 1 ('not a share: "' + $item + '" (want tag=X:=name)')
            return $null
        }
        $list += [pscustomobject]@{ Tag = $Matches[1]; Drive = $Matches[2].ToUpper() + ':'; Name = [string] $Matches[3] }
    }
    return ,$list
}

function Set-DriveNames($Wanted, $Before) {
    $icons = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\DriveIcons'
    foreach ($share in @($Before)) {
        if (@($Wanted | Where-Object { $_.Drive -eq $share.Drive }).Count -eq 0) {
            Remove-Item -LiteralPath (Join-Path $icons $share.Drive.TrimEnd(':')) -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    foreach ($share in @($Wanted)) {
        if (-not $share.Name) { continue }
        $name = [System.Text.Encoding]::UTF8.GetString([System.Convert]::FromBase64String($share.Name))
        $key = Join-Path (Join-Path $icons $share.Drive.TrimEnd(':')) 'DefaultLabel'
        New-Item -Path $key -Force | Out-Null
        Set-Item -LiteralPath $key -Value $name
    }
}

function WinFsp-Dir {
    $key = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\WOW6432Node\WinFsp' -ErrorAction SilentlyContinue
    if ($key -and $key.InstallDir -and (Test-Path -LiteralPath $key.InstallDir)) { return $key.InstallDir }
    return $null
}

function WinFsp-Version {
    $entries = Get-ItemProperty -Path @(
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*') -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -like 'WinFsp*' -and $_.DisplayVersion }
    $best = $null
    foreach ($entry in $entries) {
        try { $v = [version] $entry.DisplayVersion } catch { continue }
        if (-not $best -or $v -gt $best) { $best = $v }
    }
    return $best
}

function Launchctl {
    $dir = WinFsp-Dir
    if (-not $dir) { return $null }
    return (Join-Path $dir 'bin\launchctl-x64.exe')
}

function Running-Instances {
    $running = @{}
    $launchctl = Launchctl
    if (-not $launchctl) { return $running }
    foreach ($line in (& $launchctl list 2>&1)) {
        if ("$line" -match ('^' + $FsClass + ' (\S+)\s*$')) {
            $name = $Matches[1]
            $info = (& $launchctl info $FsClass $name 2>&1) | Select-Object -Skip 2 -First 1
            $running[$name] = "$info"
        }
    }
    return $running
}

function Stop-Instance([string] $Name) {
    $out = (& (Launchctl) stop $FsClass $Name 2>&1) -join ' '
    L ('stopped ' + $Name + ': ' + $out)
}

function Install-Driver([string] $From) {
    $inf = Join-Path $From 'viofs.inf'
    $line = Select-String -LiteralPath $inf -Pattern '^\s*DriverVer\s*=\s*[^,]*,\s*([0-9.]+)' | Select-Object -First 1
    if (-not $line) { Fail 1 ($inf + ' has no DriverVer'); return }
    $want = $line.Matches[0].Groups[1].Value
    $devices = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
                 Where-Object { $_.InstanceId -like $DeviceMatch })
    if ($devices.Count -eq 0) {
        $out = (& pnputil.exe /add-driver $inf 2>&1) -join ' '
        $rc = $LASTEXITCODE
        L ('no virtio-fs device; pnputil /add-driver exited ' + $rc + ': ' + $out)
        if ($rc -ne 0 -and $rc -ne 259) { Fail 2 ('pnputil /add-driver exited ' + $rc) }
        return
    }
    $current = $true
    foreach ($device in $devices) {
        $version = (Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_DriverVersion' -ErrorAction SilentlyContinue).Data
        L ('device ' + $device.InstanceId + ': ' + $device.Status + ', problem ' + $device.Problem + ', driver ' + $version)
        if ($device.Status -ne 'OK' -or $version -ne $want) { $current = $false }
    }
    if ($current) { L ('viofs ' + $want + ' already drives every device'); return }

    $out = (& pnputil.exe /add-driver $inf /install 2>&1) -join "`n"
    $rc = $LASTEXITCODE
    L ('pnputil /add-driver /install exited ' + $rc + ":`n" + $out)
    if ($rc -ne 0 -and $rc -ne 259 -and $rc -ne 3010 -and $rc -ne 1641) {
        Fail 2 ('pnputil /add-driver /install exited ' + $rc)
        return
    }
    foreach ($device in @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
                          Where-Object { $_.InstanceId -like $DeviceMatch })) {
        if ($device.Status -eq 'OK') { continue }
        if ($rc -eq 3010 -or $rc -eq 1641) {
            Fail 7 ('the driver is installed and ' + $device.InstanceId + ' starts once Windows restarts')
        } else {
            Fail 5 ($device.InstanceId + ' still has no working driver (' + $device.Problem + ')')
        }
    }
}

function Install-WinFsp([string] $From) {
    $msi = Get-ChildItem -LiteralPath $From -Filter 'winfsp-*.msi' | Select-Object -First 1
    if (-not $msi) { Fail 1 ('no winfsp-*.msi in ' + $From); return }
    $want = [version] ($msi.BaseName -replace '^winfsp-', '')
    $have = WinFsp-Version
    if ($have -and $have -ge $want -and (WinFsp-Dir)) {
        L ('WinFsp ' + $have + ' installed (shipped: ' + $want + ')')
        return
    }
    L ('installing WinFsp ' + $want + ' (installed: ' + $(if ($have) { $have } else { 'none' }) + ')')
    $msiLog = Join-Path $LogDir 'winfsp-install.log'
    Roll-Log $msiLog
    $process = Start-Process -FilePath msiexec.exe -Wait -PassThru -ArgumentList @(
        '/i', ('"' + $msi.FullName + '"'), '/qn', '/norestart', '/l*v', ('"' + $msiLog + '"'))
    $rc = $process.ExitCode
    L ('msiexec exited ' + $rc + ' (its log: ' + $msiLog + ')')
    if ($rc -eq 3010 -or $rc -eq 1641) { Fail 7 'WinFsp is installed and wants Windows restarted'; return }
    if ($rc -ne 0 -or -not (WinFsp-Dir)) { Fail 3 ('msiexec exited ' + $rc); return }
}

function Install-Files([string] $From) {
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    $exe = Join-Path $InstallDir 'virtiofs.exe'
    $new = Join-Path $From 'virtiofs.exe'
    if (-not (Test-Path -LiteralPath $exe) -or
        (Get-FileHash -LiteralPath $exe).Hash -ne (Get-FileHash -LiteralPath $new).Hash) {
        foreach ($name in (Running-Instances).Keys) { Stop-Instance $name }
        Start-Sleep -Seconds 1
        try {
            Copy-Item -LiteralPath $new -Destination $exe -Force -ErrorAction Stop
            L ('installed ' + $exe)
        } catch {
            Fail 1 ('could not install ' + $exe + ': ' + $_.Exception.Message)
            return
        }
    }
    if ($From -ne $InstallDir) {
        Copy-Item -LiteralPath (Join-Path $From 'lsw-shares.ps1') -Destination (Join-Path $InstallDir 'lsw-shares.ps1') -Force
    }
}

function Register-Launcher {
    $dir = WinFsp-Dir
    if (-not $dir) { Fail 3 'WinFsp is not installed'; return }
    $exe = Join-Path $InstallDir 'virtiofs.exe'
    $out = (& (Join-Path $dir 'bin\fsreg.bat') $FsClass $exe '-t %1 -m %2' ('"' + $LauncherSddl + '"') 2>&1) -join ' '
    $rc = $LASTEXITCODE
    $key = Get-ItemProperty -LiteralPath ('HKLM:\SOFTWARE\WOW6432Node\WinFsp\Services\' + $FsClass) -ErrorAction SilentlyContinue
    if ($rc -ne 0 -or -not $key -or $key.Executable -ne $exe -or $key.CommandLine -ne '-t %1 -m %2') {
        Fail 4 ('fsreg exited ' + $rc + ': ' + $out)
        return
    }
    L ('registered ' + $FsClass + ' with the WinFsp launcher: ' + $exe + ' ' + $key.CommandLine)
}

function Register-BootTask {
    $script = Join-Path $InstallDir 'lsw-shares.ps1'
    try {
        $action = New-ScheduledTaskAction -Execute 'powershell.exe' `
            -Argument ('-NoProfile -ExecutionPolicy Bypass -File "' + $script + '"')
        $trigger = New-ScheduledTaskTrigger -AtStartup
        $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
        $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
            -ExecutionTimeLimit (New-TimeSpan -Minutes 10)
        Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Principal $principal `
            -Settings $settings -Description 'Mounts the folders LSW shares from the host.' -Force -ErrorAction Stop | Out-Null
        L ('boot task ' + $TaskName + ' registered')
    } catch {
        Fail 1 ('could not register the boot task: ' + $_.Exception.Message)
    }
}

function Wait-Ready {
    $deadline = (Get-Date).AddSeconds(90)
    while ((Get-Date) -lt $deadline) {
        $devices = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
                     Where-Object { $_.InstanceId -like $DeviceMatch })
        $launcher = Get-Service -Name 'WinFsp.Launcher' -ErrorAction SilentlyContinue
        if ($launcher -and $launcher.Status -ne 'Running') {
            Start-Service -Name 'WinFsp.Launcher' -ErrorAction SilentlyContinue
        }
        $ok = @($devices | Where-Object { $_.Status -eq 'OK' }).Count
        if ($devices.Count -gt 0 -and $ok -eq $devices.Count -and $launcher -and $launcher.Status -eq 'Running') {
            return $true
        }
        Start-Sleep -Seconds 2
    }
    foreach ($device in $devices) { L ('device ' + $device.InstanceId + ': ' + $device.Status + ', problem ' + $device.Problem) }
    if ($devices.Count -eq 0) { L 'no virtio-fs device: the machine has no shared folder' }
    if (-not $launcher) { L 'no WinFsp.Launcher service' } else { L ('WinFsp.Launcher: ' + $launcher.Status) }
    return $false
}

function Mount-Shares($Wanted) {
    $launchctl = Launchctl
    $running = Running-Instances
    $names = @($Wanted | ForEach-Object { 'lsw-' + $_.Tag })
    foreach ($name in @($running.Keys)) {
        if ($names -notcontains $name) { Stop-Instance $name; $running.Remove($name) }
    }
    if ($Wanted.Count -eq 0) { L 'no shares to mount'; return }
    if (-not $launchctl) { Fail 3 'WinFsp is not installed'; return }
    if (-not (Wait-Ready)) { Fail 5 'the virtio-fs devices or the WinFsp launcher did not come up'; return }

    foreach ($share in $Wanted) {
        $name = 'lsw-' + $share.Tag
        $root = $share.Drive + '\'
        $ours = $running.ContainsKey($name) -and
                $running[$name] -match ('-t "?' + [regex]::Escape($share.Tag) + '"? -m "?' + [regex]::Escape($share.Drive) + '"?')
        if ($ours -and (Test-Path -LiteralPath $root)) {
            L ($share.Drive + ' is mounted (' + $name + ', tag ' + $share.Tag + ')')
            continue
        }
        if ($running.ContainsKey($name)) { Stop-Instance $name; Start-Sleep -Seconds 1 }
        if (Test-Path -LiteralPath $root) {
            Fail 6 ($share.Drive + ' is taken by something else in Windows; tag ' + $share.Tag + ' is not mounted')
            continue
        }
        $out = (& $launchctl start $FsClass $name $share.Tag $share.Drive 2>&1) -join ' '
        L ('launchctl start ' + $FsClass + ' ' + $name + ' ' + $share.Tag + ' ' + $share.Drive + ': ' + $out)
        $deadline = (Get-Date).AddSeconds(20)
        while (-not (Test-Path -LiteralPath $root) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
        if (Test-Path -LiteralPath $root) {
            L ($share.Drive + ' mounted (tag ' + $share.Tag + ')')
        } else {
            Fail 6 ($share.Drive + ' did not appear for tag ' + $share.Tag + ' (launchctl: ' + $out + ')')
        }
    }
}

$mutex = New-Object System.Threading.Mutex($false, 'Global\lsw-shares')
$owned = $false
try { $owned = $mutex.WaitOne(300000) } catch [System.Threading.AbandonedMutexException] { $owned = $true }

try {
    L ('start: ' + $(if ($Install) { '-Install ' } else { '' }) +
       $(if ($PSBoundParameters.ContainsKey('Shares')) { '-Shares ' + $Shares } else { '(saved list)' }) +
       ' as ' + [System.Security.Principal.WindowsIdentity]::GetCurrent().Name)

    $before = @()
    if (Test-Path -LiteralPath $SavedList) {
        $before = @(Parse-Shares ((Get-Content -LiteralPath $SavedList) -join ','))
    }
    if ($PSBoundParameters.ContainsKey('Shares')) {
        $wanted = Parse-Shares $Shares
    } elseif (Test-Path -LiteralPath $SavedList) {
        $wanted = Parse-Shares ((Get-Content -LiteralPath $SavedList) -join ',')
    } else {
        $wanted = @()
    }
    if ($null -eq $wanted) { $wanted = @() }

    if ($Install) {
        $from = $PSScriptRoot
        foreach ($file in @('viofs.inf', 'viofs.sys', 'viofs.cat', 'virtiofs.exe', 'lsw-shares.ps1')) {
            if (-not (Test-Path -LiteralPath (Join-Path $from $file))) { Fail 1 ($file + ' is missing from ' + $from) }
        }
        if ($script:Result -eq 0) {
            Install-WinFsp $from
            Install-Driver $from
            Install-Files $from
            Register-Launcher
            Register-BootTask
        }
    }

    if ($PSBoundParameters.ContainsKey('Shares') -and $script:Result -eq 0) {
        New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
        Set-Content -LiteralPath $SavedList -Encoding Ascii -Value @($wanted | ForEach-Object { $_.Tag + '=' + $_.Drive + $(if ($_.Name) { '=' + $_.Name } else { '' }) })
    }
    if ($script:Result -eq 0) {
        Set-DriveNames $wanted $before
        Mount-Shares $wanted
    }
    L ('done, exit ' + $script:Result)
} finally {
    if ($owned) { $mutex.ReleaseMutex() }
}
exit $script:Result
