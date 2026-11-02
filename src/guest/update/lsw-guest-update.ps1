# lsw-guest-update.ps1 - brings what LSW puts into Windows to one release.
#
# The manager (services::GuestUpdate) downloads every file of its release
# into a staging directory and runs this from it as SYSTEM in a one-shot
# scheduled task, not through the agent: the work stops the agent.
#
#   lsw-guest-update.ps1 -Stage <dir> -Release <hex> [-Version <x.y.z>] [-Force] [-NoRestart]
#
# The staging directory holds each file under its guest path with the
# drive's colon removed (<dir>\C\ProgramData\LSW\lsw-agent.exe is
# C:\ProgramData\LSW\lsw-agent.exe), manifest.txt (one line per file,
# "<sha256>  <guest path>") and shares.txt (the value for
# lsw-shares.ps1 -Shares). docs/guest-update.md has the steps and why.
#
# Every step is idempotent and is skipped when what it puts in place
# already matches the manifest, unless -Force. The first step that fails
# ends the work, except that the agent is always started again (step 8),
# so the guest is never left without one.
#
# Written to C:\ProgramData\LSW:
#   update.log          every step, appended
#   update-result.json  { ok, finished, restart, steps, message }, every run
#   (times are UTC "yyyy-MM-ddTHH:mm:ss.fffZ")
#   release.json        { release, version, applied, manifest }, only when every step
#                       succeeded
# Two runs never overlap: the whole run holds the mutex Global\lsw-guest-update.
# A restart a step marked is done last, with shutdown /r /t 5, except with
# -NoRestart: Windows does not allow a restart from SetupComplete, where the
# first install runs this from the payload ISO's copy of the bundle. The
# restart is then only recorded in update-result.json ("restart": true), and
# the first boot after setup is the restart. There the agent cannot listen
# yet (it waits for the wizard user's desktop), so step 8 waits for the
# service to run instead.

param(
    [Parameter(Mandatory = $true)] [string] $Stage,
    [Parameter(Mandatory = $true)] [string] $Release,
    # The LSW version that made the bundle, for the manager to name it by.
    # Not part of the manifest: two versions with the same files are the
    # same release.
    [string] $Version = '',
    [switch] $Force,
    [switch] $NoRestart
)

$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'

$Root = 'C:\ProgramData\LSW'
$Log = Join-Path $Root 'update.log'
$ResultFile = Join-Path $Root 'update-result.json'
$ReleaseFile = Join-Path $Root 'release.json'
$Previous = Join-Path $Root 'previous'
$Pending = Join-Path $Root 'pending-loaders'
$AgentExe = Join-Path $Root 'lsw-agent.exe'
$MirrorExe = Join-Path $Root 'remotewin-guest.exe'
$ServiceName = 'lsw-agent'
$AgentPort = 5102
$DriverMarker = 'C:\av\installed.sha256'
$Utf8 = New-Object System.Text.UTF8Encoding($false)

New-Item -ItemType Directory -Force -Path $Root | Out-Null

function L([string] $Message) {
    ('{0} {1}' -f (Get-Date -Format s), $Message) | Add-Content -LiteralPath $Log -Encoding Ascii
}

# The form the host parses (Qt::ISODateWithMs): milliseconds, because the
# host compares the agent's start with it to a second.
function Utc-Now {
    return (Get-Date).ToUniversalTime().ToString("yyyy'-'MM'-'dd'T'HH':'mm':'ss'.'fff'Z'", [Globalization.CultureInfo]::InvariantCulture)
}

function Write-Result([bool] $Succeeded, [string] $Text, $StepList) {
    $outcome = [ordered]@{
        ok       = $Succeeded
        finished = Utc-Now
        restart  = $script:Restart
        steps    = @($StepList)
        message  = $Text
    }
    try {
        [System.IO.File]::WriteAllText($ResultFile, ($outcome | ConvertTo-Json -Depth 4 -Compress), $Utf8)
    } catch {
        L ('could not write ' + $ResultFile + ': ' + $_.Exception.Message)
    }
}

# ---- one run at a time ----

$script:Restart = $false
$script:UpdateLock = New-Object System.Threading.Mutex($false, 'Global\lsw-guest-update')
$locked = $false
try {
    $locked = $script:UpdateLock.WaitOne(5000)
} catch [System.Threading.AbandonedMutexException] {
    # The run that held it died; the lock is ours.
    $locked = $true
}
if (-not $locked) {
    L 'another update is running in Windows; this one does nothing'
    Write-Result $false 'another update is running in Windows' @()
    exit 1
}

# ---- the manifest ----

$script:Manifest = @{}
$script:ManifestText = ''

function Stage-Path([string] $GuestPath) {
    return Join-Path $Stage ($GuestPath -replace '^([A-Za-z]):', '$1')
}

function Sha([string] $Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLower()
}

# Whether the file at $Path is the manifest's $GuestPath, byte for byte.
function Is-Current([string] $Path, [string] $GuestPath) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    return (Sha $Path) -eq $script:Manifest[$GuestPath]
}

# The manifest's guest paths under a directory, or equal to a file.
function Manifest-Under([string] $Prefix) {
    return @($script:Manifest.Keys | Where-Object { $_ -eq $Prefix -or $_.StartsWith($Prefix.TrimEnd('\') + '\') } | Sort-Object)
}

# Whether a directory holds exactly the manifest's files under it: none
# missing, none changed, none extra.
function Tree-Is-Current([string] $Dir) {
    $want = Manifest-Under $Dir
    foreach ($guest in $want) {
        if (-not (Is-Current $guest $guest)) { return $false }
    }
    if (Test-Path -LiteralPath $Dir -PathType Container) {
        $have = @(Get-ChildItem -LiteralPath $Dir -Recurse -File -Force -ErrorAction SilentlyContinue |
            ForEach-Object { $_.FullName })
        foreach ($path in $have) {
            if ($want -notcontains $path) { return $false }
        }
    }
    return $true
}

# ---- doing things ----

# Copies keep trying for ten seconds: a process that was just killed
# still holds its image for a moment.
function Copy-Retry([string] $From, [string] $To) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $To) | Out-Null
    for ($i = 0; ; $i++) {
        try {
            Copy-Item -LiteralPath $From -Destination $To -Force -ErrorAction Stop
            return
        } catch {
            if ($i -ge 20) { throw }
            Start-Sleep -Milliseconds 500
        }
    }
}

# Puts the staged file in place when it differs; says whether it did.
function Place([string] $GuestPath) {
    if (-not $Force -and (Is-Current $GuestPath $GuestPath)) { return $false }
    Copy-Retry (Stage-Path $GuestPath) $GuestPath
    L ('placed ' + $GuestPath)
    return $true
}

# Makes $To exactly $From. robocopy's codes below 8 are all successes.
function Mirror-Tree([string] $From, [string] $To) {
    robocopy $From $To /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw ('robocopy ' + $From + ' -> ' + $To + ' exited ' + $LASTEXITCODE) }
}

# A native command's output goes to the log, its exit code back.
function Run-Native([string] $Label, [string] $File, [string[]] $Arguments) {
    $out = & $File @Arguments 2>&1 | Out-String
    $rc = $LASTEXITCODE
    L ($Label + ' exited ' + $rc + ":`n" + $out.TrimEnd())
    return $rc
}

# Up is listening, or only running under -NoRestart (see the header).
function Agent-Up {
    if ($NoRestart) {
        $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
        return $null -ne $service -and $service.Status -eq 'Running'
    }
    return Agent-Listening
}

function Agent-Listening {
    $listeners = [System.Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners()
    return @($listeners | Where-Object { $_.Port -eq $AgentPort }).Count -gt 0
}

function Wait-Listening([int] $Seconds) {
    $until = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $until) {
        if (Agent-Up) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return (Agent-Up)
}

function Kill-Processes([string] $Name) {
    Get-Process -Name $Name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

# The service restarts itself when it is killed; mid-run that would start
# the agent on files still being replaced. Step 7 puts the real actions
# back, and step 8 if step 7 did not run.
$script:RecoveryOff = $false
$script:RecoveryOn = $false

function Set-Recovery([bool] $Real) {
    if ($Real) {
        $out = sc.exe failure $ServiceName reset= 86400 actions= restart/5000/restart/5000/restart/30000 2>&1 | Out-String
        sc.exe failureflag $ServiceName 1 2>&1 | Out-Null
        $script:RecoveryOn = $true
    } else {
        $out = sc.exe failure $ServiceName reset= 0 actions= '""' 2>&1 | Out-String
        $script:RecoveryOff = $true
        $script:RecoveryOn = $false
    }
    L ('lsw-agent recovery' + $(if ($Real) { '' } else { ' off' }) + ': ' + $out.Trim())
}

function Stop-Agent {
    if (Get-Service -Name $ServiceName -ErrorAction SilentlyContinue) {
        Set-Recovery $false
        sc.exe stop $ServiceName | Out-Null
        $until = (Get-Date).AddSeconds(30)
        while ((Get-Date) -lt $until -and (Get-Service -Name $ServiceName).Status -ne 'Stopped') {
            Start-Sleep -Milliseconds 500
        }
        L ('service ' + (Get-Service -Name $ServiceName).Status)
    }
    # The session helpers are the same image and outlive a stopped
    # service; a service that did not stop in time is killed here too.
    Kill-Processes 'lsw-agent'
}

# ---- the steps ----

$script:Steps = @()
$script:Failure = $null
$script:State = 'done'
$script:Stopped = $false
$script:Replaced = $false

# -Always runs even after a failure: only step 8 is.
function Run-Step([string] $Name, [scriptblock] $Body, [switch] $Always) {
    if ($script:Failure -and -not $Always) {
        $script:Steps += [pscustomobject]@{ name = $Name; status = 'not run'; message = '' }
        return
    }
    L ('step ' + $Name)
    $script:State = 'done'
    $message = ''
    try {
        & $Body
    } catch {
        $script:State = 'failed'
        $message = $_.Exception.Message
        L ('FAILED ' + $Name + ': ' + $message)
        if (-not $script:Failure) { $script:Failure = @{ Step = $Name; Message = $message } }
    }
    L ('step ' + $Name + ': ' + $script:State)
    $script:Steps += [pscustomobject]@{ name = $Name; status = $script:State; message = $message }
}

function Step-Verify {
    $manifestPath = Join-Path $Stage 'manifest.txt'
    if (-not (Test-Path -LiteralPath $manifestPath)) { throw 'manifest.txt is not in the staging directory' }
    $script:ManifestText = [System.IO.File]::ReadAllText($manifestPath, $Utf8)
    foreach ($line in ($script:ManifestText -split "`n")) {
        $line = $line.TrimEnd("`r")
        if (-not $line) { continue }
        if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { throw ('not a manifest line: ' + $line) }
        $script:Manifest[$Matches[2]] = $Matches[1]
    }
    foreach ($guest in $script:Manifest.Keys) {
        $path = Stage-Path $guest
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw ('staged file missing: ' + $guest) }
        if ((Sha $path) -ne $script:Manifest[$guest]) { throw ('staged file does not match the manifest: ' + $guest) }
    }
    L ('verified ' + $script:Manifest.Count + ' file(s)')
}

function Step-Stop {
    # Set first: a stop that fails half way has still stopped something.
    $script:Stopped = $true
    Kill-Processes 'remotewin-guest'
    Stop-Agent
}

function Step-AgentAndMirror {
    $agent = $Root + '\lsw-agent.exe'
    $mirror = $Root + '\remotewin-guest.exe'
    $update = $Root + '\lsw-guest-update.ps1'
    $ours = @($agent, $mirror) | Where-Object { $script:Manifest.ContainsKey($_) }
    $differs = @($ours | Where-Object { $Force -or -not (Is-Current $_ $_) })
    if ($differs.Count -gt 0) {
        # Kept for step 8 to put back. Only what is there to keep.
        New-Item -ItemType Directory -Force -Path $Previous | Out-Null
        foreach ($path in $ours) {
            if (Test-Path -LiteralPath $path) { Copy-Retry $path (Join-Path $Previous (Split-Path -Leaf $path)) }
        }
        $script:Replaced = $true
        foreach ($path in $differs) { Place $path | Out-Null }
    }
    # The script itself is next to them so the next run can start from it.
    if ($script:Manifest.ContainsKey($update)) { Place $update | Out-Null }
    if ($differs.Count -eq 0) { $script:State = 'skipped' }
}

# The manifest's lines of every file the driver step owns, sorted.
function Driver-Lines {
    $guests = @($script:Manifest.Keys | Where-Object {
        $_ -eq 'C:\av\provision.ps1' -or $_ -eq 'C:\av\adapters.txt' -or $_.StartsWith('C:\av\package\')
    })
    $lines = @($guests | ForEach-Object { $script:Manifest[$_] + '  ' + $_ })
    [Array]::Sort($lines, [StringComparer]::Ordinal)
    return ($lines -join "`n")
}

function Step-Driver {
    $set = @('C:\av\provision.ps1', 'C:\av\adapters.txt') | Where-Object { $script:Manifest.ContainsKey($_) }
    # The files alone prove nothing: they are put in place before the
    # install, which may fail. Only the marker, written after it succeeded,
    # says this package is installed.
    $wanted = Driver-Lines
    $have = ''
    if (Test-Path -LiteralPath $DriverMarker -PathType Leaf) { $have = [System.IO.File]::ReadAllText($DriverMarker, $Utf8) }
    $current = $have -ceq $wanted -and (Tree-Is-Current 'C:\av\package')
    foreach ($path in $set) { if (-not (Is-Current $path $path)) { $current = $false } }
    if ($current -and -not $Force) { $script:State = 'skipped'; return }
    Remove-Item -LiteralPath $DriverMarker -Force -ErrorAction SilentlyContinue

    foreach ($path in $set) { Place $path | Out-Null }
    Mirror-Tree (Stage-Path 'C:\av\package') 'C:\av\package'

    # Signing rewrites the INF for the adapters present now and signs
    # avgpu.sys, so it happens on a copy: the package as shipped stays
    # what the manifest says it is.
    Mirror-Tree 'C:\av\package' 'C:\av\resign\package'
    $provision = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'C:\av\provision.ps1', '-Package', 'C:\av\resign\package')
    # provision.ps1 exits 3010 when it wants Windows restarted, which is
    # how -Stage install ends on success.
    $signed = Run-Native 'provision sign' 'powershell.exe' ($provision + @('-Stage', 'sign'))
    if ($signed -ne 0 -and $signed -ne 3010) { throw ('provision.ps1 -Stage sign exited ' + $signed) }
    $installed = Run-Native 'provision install' 'powershell.exe' ($provision + @('-Stage', 'install', '-NoVerifier'))
    if ($installed -ne 0 -and $installed -ne 3010) { throw ('provision.ps1 -Stage install exited ' + $installed) }
    [System.IO.File]::WriteAllText($DriverMarker, $wanted, $Utf8)
    $script:Restart = $true
}

# For a loader that is in use: Windows replaces it at the next boot.
function Schedule-Replace([string] $From, [string] $To) {
    if (-not ('LswUpdate.Native' -as [type])) {
        Add-Type -Namespace LswUpdate -Name Native -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
public static extern bool MoveFileEx(string existing, string target, int flags);
'@
    }
    # The staging directory is gone before the boot that moves it.
    New-Item -ItemType Directory -Force -Path $Pending | Out-Null
    $kept = Join-Path $Pending ((Split-Path -Leaf (Split-Path -Parent $From)) + '-' + (Split-Path -Leaf $From))
    Copy-Item -LiteralPath $From -Destination $kept -Force
    $delayUntilReboot = 4
    $replaceExisting = 1
    if (-not [LswUpdate.Native]::MoveFileEx($kept, $To, $delayUntilReboot -bor $replaceExisting)) {
        throw ('MoveFileEx for ' + $To + ' failed: ' + [System.Runtime.InteropServices.Marshal]::GetLastWin32Error())
    }
    L ($To + ' is in use; replaced at the next boot')
    $script:Restart = $true
}

function Step-Loaders {
    $names = @('vulkan-1.dll', 'OpenCL.dll')
    $targets = @{ x64 = Join-Path $env:SystemRoot 'System32'; x86 = Join-Path $env:SystemRoot 'SysWOW64' }
    $placed = $false
    foreach ($guest in (Manifest-Under 'C:\av\loaders')) {
        if (Place $guest) { $placed = $true }
    }
    foreach ($arch in @('x64', 'x86')) {
        foreach ($name in $names) {
            $source = 'C:\av\loaders\' + $arch + '\' + $name
            if (-not $script:Manifest.ContainsKey($source)) { continue }
            $target = Join-Path $targets[$arch] $name
            if (-not $Force -and (Is-Current $target $source)) { continue }
            $placed = $true
            try {
                [System.IO.File]::Copy($source, $target, $true)
                L ('placed ' + $target)
            } catch [System.IO.IOException], [System.UnauthorizedAccessException] {
                Schedule-Replace $source $target
            }
        }
    }
    if (-not $placed) { $script:State = 'skipped' }
}

function Step-Shares {
    $dir = $Root + '\virtiofs'
    $current = Tree-Is-Current $dir
    if (-not $current -or $Force) {
        # Exactly the bundle's, so an old WinFsp installer cannot be picked.
        if (Test-Path -LiteralPath (Stage-Path $dir)) { Mirror-Tree (Stage-Path $dir) $dir }
        elseif (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    }
    $sharesScript = Join-Path $dir 'lsw-shares.ps1'
    if (-not (Test-Path -LiteralPath $sharesScript)) {
        L 'the bundle has no guest half for shared folders'
        $script:State = 'skipped'
        return
    }
    $sharesPath = Join-Path $Stage 'shares.txt'
    $shares = 'none'
    if (Test-Path -LiteralPath $sharesPath) {
        $text = ([System.IO.File]::ReadAllText($sharesPath, $Utf8)).Trim()
        if ($text) { $shares = $text }
    }
    # Always run: it is idempotent and the mounts are what the manager
    # wants now, which no staged file says has already been done.
    $rc = Run-Native 'lsw-shares' 'powershell.exe' @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $sharesScript, '-Install', '-Shares', $shares)
    if ($rc -eq 7) { $script:Restart = $true }
    elseif ($rc -ne 0) { throw ('lsw-shares.ps1 exited ' + $rc) }
}

# What setup creates (autounattend.xml, SetupComplete): the service, and
# the two rules that let the host and the stream in.
function Step-Service {
    $bin = 'C:\ProgramData\LSW\lsw-agent.exe --service'
    # Always, even when all else is current: the stop step turned it off.
    if (Get-Service -Name $ServiceName -ErrorAction SilentlyContinue) { Set-Recovery $true }
    $key = 'HKLM:\SYSTEM\CurrentControlSet\Services\' + $ServiceName
    $props = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
    $serviceOk = $props -and $props.ImagePath -eq $bin -and $props.Start -eq 2 -and $props.ObjectName -eq 'LocalSystem' -and $props.FailureActions
    $rules = @(
        @{ Name = 'LSW Agent'; Port = 5102 },
        @{ Name = 'LSW Stream'; Port = 5103 }
    )
    $rulesMissing = @($rules | Where-Object { (netsh advfirewall firewall show rule "name=$($_.Name)" | Out-String) -notmatch ('\b' + $_.Port + '\b') })
    if ($serviceOk -and $rulesMissing.Count -eq 0 -and -not $Force) { $script:State = 'skipped'; return }

    if (-not $serviceOk -or $Force) {
        if (Get-Service -Name $ServiceName -ErrorAction SilentlyContinue) {
            $out = sc.exe config $ServiceName binPath= $bin start= auto obj= LocalSystem 2>&1 | Out-String
        } else {
            $out = sc.exe create $ServiceName binPath= $bin start= auto obj= LocalSystem DisplayName= 'LSW Guest Agent' 2>&1 | Out-String
        }
        L ('lsw-agent service: ' + $out.Trim())
        sc.exe description $ServiceName 'The LSW manager''s control channel into this VM.' 2>&1 | Out-Null
        Set-Recovery $true
    }
    foreach ($rule in $rules) {
        if (-not $Force -and $rulesMissing -notcontains $rule) { continue }
        # Added after a delete so a rule that exists with other settings
        # ends up as setup makes it.
        netsh advfirewall firewall delete rule "name=$($rule.Name)" | Out-Null
        $out = netsh advfirewall firewall add rule "name=$($rule.Name)" dir=in action=allow protocol=TCP "localport=$($rule.Port)" profile=any | Out-String
        L ('firewall ' + $rule.Name + ': ' + $out.Trim())
    }
}

function Step-Start {
    if ($script:RecoveryOff -and -not $script:RecoveryOn) { Set-Recovery $true }
    # Nothing was stopped when the run ended before step 2.
    if (-not $script:Stopped) { $script:State = 'skipped'; return }
    sc.exe start $ServiceName 2>&1 | Out-String | ForEach-Object { L ('lsw-agent start: ' + $_.Trim()) }
    if (Wait-Listening 60) { return }

    if ($script:Replaced) {
        L ('the agent is not listening on ' + $AgentPort + ' after 60 s; putting the previous one back')
        Kill-Processes 'remotewin-guest'
        Stop-Agent
        foreach ($path in @($AgentExe, $MirrorExe)) {
            $kept = Join-Path $Previous (Split-Path -Leaf $path)
            if (Test-Path -LiteralPath $kept) { Copy-Retry $kept $path }
        }
        # Stop-Agent turned the recovery actions off again.
        Set-Recovery $true
        sc.exe start $ServiceName 2>&1 | Out-String | ForEach-Object { L ('lsw-agent start: ' + $_.Trim()) }
        $back = Wait-Listening 60
        L ('previous agent listening: ' + $back)
        throw ('the new agent did not listen on ' + $AgentPort + ' within 60 s; the previous one was put back' + $(if ($back) { '' } else { ' and does not listen either' }))
    }
    throw ('the agent did not listen on ' + $AgentPort + ' within 60 s')
}

# ---- the run ----

L ('=== update to release ' + $Release + ($(if ($Force) { ' (forced)' } else { '' })) + ' from ' + $Stage)

Run-Step 'verify' { Step-Verify }
Run-Step 'stop' { Step-Stop }
Run-Step 'agent and mirror' { Step-AgentAndMirror }
Run-Step 'driver' { Step-Driver }
Run-Step 'loaders' { Step-Loaders }
Run-Step 'shared folders' { Step-Shares }
Run-Step 'service and firewall' { Step-Service }
Run-Step 'start' { Step-Start } -Always

$ok = -not $script:Failure
$message = if ($ok) { 'updated to release ' + $Release } else { $script:Failure.Step + ': ' + $script:Failure.Message }

# 9. Record. release.json first, so a result that says ok always has it.
if ($ok) {
    try {
        # Not $release: PowerShell's names ignore case, so that is the
        # [string] parameter $Release, and the table would be stored as its
        # type name.
        $record = [ordered]@{
            release  = $Release
            version  = $Version
            applied  = Utc-Now
            manifest = $script:ManifestText
        }
        [System.IO.File]::WriteAllText($ReleaseFile, ($record | ConvertTo-Json -Compress), $Utf8)
    } catch {
        $ok = $false
        $message = 'record: ' + $_.Exception.Message
        L ('FAILED ' + $message)
    }
}
Write-Result $ok $message $script:Steps
L ('=== ' + $message)

Remove-Item -LiteralPath $Stage -Recurse -Force -ErrorAction SilentlyContinue
if ($script:Restart -and $NoRestart) {
    L 'a restart is due; -NoRestart, so it is left to the next boot'
} elseif ($script:Restart) {
    L 'restarting Windows'
    shutdown.exe /r /t 5 /c 'LSW: restart to finish updating'
}
$script:UpdateLock.ReleaseMutex()
if ($ok) { exit 0 } else { exit 1 }
