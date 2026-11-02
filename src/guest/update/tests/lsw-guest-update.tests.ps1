# lsw-guest-update.tests.ps1 - scenario tests for lsw-guest-update.ps1,
# run inside the Windows guest as SYSTEM (docs/guest-update.md, "In the
# guest"). scripts/test-guest-update.sh puts it there and starts it.
#
#   lsw-guest-update.tests.ps1 [-ScriptUnderTest <path>] [-LogFile <path>] [-Only <letters>]
#
# Each scenario builds a staging directory from the files installed now
# (so a current stage changes nothing), with a manifest computed from them
# in the script's format, runs the script under test from it with
# -NoRestart (the guest is never restarted from here), and checks the
# outcome in update-result.json, release.json, update.log, the service, port
# 5102 and the files. What a scenario changed is put back before the next.
#
# It is run the way production runs the script: a one-shot scheduled task
# as SYSTEM, never a child of the agent, which the script stops and starts.
# Output: one PASS/FAIL line per check in -LogFile, then
#   SUMMARY: <n> passed, <n> failed
# as the last line.
#
# The script under test is staged from -ScriptUnderTest (default: the
# installed one), so the installed script is not replaced by the checkout's
# before it has passed; the installed one is put back after each scenario.

param(
    [string] $ScriptUnderTest = 'C:\ProgramData\LSW\lsw-guest-update.ps1',
    [string] $LogFile = 'C:\ProgramData\LSW\update-tests.log',
    [string] $Only = ''
)

$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'

$Lsw = 'C:\ProgramData\LSW'
$WorkDir = Join-Path $Lsw 'update-tests-work'
$BaseDir = Join-Path $WorkDir 'base'
$ReleaseJson = Join-Path $Lsw 'release.json'
$ResultJson = Join-Path $Lsw 'update-result.json'
$UpdateLog = Join-Path $Lsw 'update.log'
$InstalledScript = Join-Path $Lsw 'lsw-guest-update.ps1'
$PreviousDir = Join-Path $Lsw 'previous'
$PendingDir = Join-Path $Lsw 'pending-loaders'
$Marker = 'C:\av\installed.sha256'
$SessionKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager'
$SharesPseudo = 'C:\ProgramData\LSW\update\shares.txt'
$Utf8 = New-Object System.Text.UTF8Encoding($false)
$StepNames = @('verify', 'stop', 'agent and mirror', 'driver', 'loaders', 'shared folders', 'service and firewall', 'start')

$script:Passed = 0
$script:Failed = 0

# ---- reporting ----

function Say([string] $Text) {
    $line = '{0} {1}' -f (Get-Date -Format 'HH:mm:ss'), $Text
    Add-Content -LiteralPath $LogFile -Value $line -Encoding Ascii
    Write-Host $line
}

function Check([string] $Label, [bool] $Condition, [string] $Detail = '') {
    if ($Condition) {
        $script:Passed++
        Say ('PASS ' + $Label)
    } else {
        $script:Failed++
        Say ('FAIL ' + $Label + $(if ($Detail) { ' :: ' + $Detail } else { '' }))
    }
}

# ---- reading the guest ----

function Sha([string] $Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLower()
}

function Get-AgentPid {
    $service = Get-CimInstance Win32_Service -Filter "Name='lsw-agent'" -ErrorAction SilentlyContinue
    if ($service) { return [int] $service.ProcessId }
    return 0
}

function Test-Port5102 {
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $pending = $client.BeginConnect('127.0.0.1', 5102, $null, $null)
        if (-not $pending.AsyncWaitHandle.WaitOne(1500)) { return $false }
        $client.EndConnect($pending)
        return $client.Connected
    } catch {
        return $false
    } finally {
        $client.Close()
    }
}

function Wait-Agent([int] $Seconds = 120) {
    $until = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $until) {
        $service = Get-Service -Name 'lsw-agent' -ErrorAction SilentlyContinue
        if ($service -and $service.Status -eq 'Running' -and (Test-Port5102)) { return $true }
        Start-Sleep -Seconds 1
    }
    return $false
}

function Read-Json([string] $Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    try { return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json) } catch { return $null }
}

function Get-StepStatus($Result, [string] $StepName) {
    $found = @($Result.steps | Where-Object { $_.name -eq $StepName })
    if ($found.Count -eq 0) { return '(absent)' }
    return [string] $found[0].status
}

function Format-Steps($Result) {
    if (-not $Result) { return '(no result)' }
    $text = @($Result.steps | ForEach-Object { $_.name + '=' + $_.status }) -join ', '
    return ('ok=' + $Result.ok + ' restart=' + $Result.restart + ' [' + $text + '] ' + $Result.message)
}

# The recovery actions of the lsw-agent service as sc.exe reports them.
function Get-Recovery {
    $text = sc.exe qfailure lsw-agent | Out-String
    $reset = ''
    if ($text -match 'RESET_PERIOD[^:]*:\s*(\d+)') { $reset = $Matches[1] }
    $delays = @([regex]::Matches($text, 'RESTART -- Delay = (\d+) milliseconds') | ForEach-Object { $_.Groups[1].Value })
    return ('restart/' + ($delays -join ',restart/') + ' reset ' + $reset)
}

$WantedRecovery = 'restart/5000,restart/5000,restart/30000 reset 86400'

function Get-Pending {
    $item = Get-ItemProperty -LiteralPath $SessionKey -Name PendingFileRenameOperations -ErrorAction SilentlyContinue
    if ($item) { return @($item.PendingFileRenameOperations) }
    return $null
}

# ---- the stage ----

$script:Orig = @{}          # guest path -> sha256 of what is installed at the start
$script:SharesText = ''
$script:SavedMarker = $null
$script:SavedRelease = $null
$script:SavedResult = $null
$script:SavedLog = $null
$script:SavedPending = $null

function Stage-File([string] $Root, [string] $GuestPath) {
    return Join-Path $Root ($GuestPath -replace '^([A-Za-z]):', '$1')
}

function Driver-Lines([hashtable] $Map) {
    $guests = @($Map.Keys | Where-Object {
        $_ -eq 'C:\av\provision.ps1' -or $_ -eq 'C:\av\adapters.txt' -or $_.StartsWith('C:\av\package\')
    })
    $lines = @($guests | ForEach-Object { $Map[$_] + '  ' + $_ })
    [Array]::Sort($lines, [StringComparer]::Ordinal)
    return ($lines -join "`n")
}

# The files that exist now, as the release would list them.
function Enumerate-Installed {
    $paths = @(
        'C:\ProgramData\LSW\lsw-agent.exe', 'C:\ProgramData\LSW\remotewin-guest.exe',
        'C:\ProgramData\LSW\lsw-guest-update.ps1', 'C:\av\provision.ps1', 'C:\av\adapters.txt')
    foreach ($dir in @('C:\av\package', 'C:\av\loaders', 'C:\ProgramData\LSW\virtiofs')) {
        $paths += @(Get-ChildItem -LiteralPath $dir -Recurse -File -Force | ForEach-Object { $_.FullName })
    }
    return $paths
}

function Build-Base {
    if (Test-Path -LiteralPath $WorkDir) { Remove-Item -LiteralPath $WorkDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $BaseDir | Out-Null
    foreach ($guest in (Enumerate-Installed)) {
        $script:Orig[$guest] = Sha $guest
        $to = Stage-File $BaseDir $guest
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
        Copy-Item -LiteralPath $guest -Destination $to -Force
    }
    # The script under test is what is staged and run; the installed one is
    # kept to be put back.
    Copy-Item -LiteralPath $ScriptUnderTest -Destination (Stage-File $BaseDir $InstalledScript) -Force
    Copy-Item -LiteralPath $InstalledScript -Destination (Join-Path $WorkDir 'installed-script.ps1') -Force

    # The mounts the guest has now, so that the shares step changes none.
    $saved = 'C:\Program Files\LSW\virtiofs\shares.txt'
    $script:SharesText = 'none'
    if (Test-Path -LiteralPath $saved) {
        $joined = (@(Get-Content -LiteralPath $saved | Where-Object { $_.Trim() }) | ForEach-Object { $_.Trim() }) -join ','
        if ($joined) { $script:SharesText = $joined }
    }
}

# A copy of the base with its manifest; $Tweak may change staged files
# (and must say so with Stage-Rehash) before the manifest is computed.
function New-Stage([string] $StageName, [scriptblock] $Tweak = $null) {
    $path = Join-Path $WorkDir $StageName
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
    robocopy $BaseDir $path /E /NFL /NDL /NJH /NJS /NP | Out-Null
    $map = @{}
    foreach ($guest in $script:Orig.Keys) { $map[$guest] = Sha (Stage-File $path $guest) }
    $sharesFile = Stage-File $path $SharesPseudo
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $sharesFile) | Out-Null
    [System.IO.File]::WriteAllText($sharesFile, $script:SharesText, $Utf8)
    [System.IO.File]::WriteAllText((Join-Path $path 'shares.txt'), $script:SharesText, $Utf8)
    $map[$SharesPseudo] = Sha $sharesFile
    $stageObject = [pscustomobject]@{ Path = $path; Map = $map; Release = ''; ManifestText = '' }
    if ($Tweak) { & $Tweak $stageObject }
    $keys = @($stageObject.Map.Keys)
    [Array]::Sort($keys, [StringComparer]::Ordinal)
    $text = ''
    foreach ($key in $keys) { $text += $stageObject.Map[$key] + '  ' + $key + "`n" }
    [System.IO.File]::WriteAllText((Join-Path $path 'manifest.txt'), $text, $Utf8)
    $stageObject.ManifestText = $text
    $stageObject.Release = ([System.BitConverter]::ToString(
        [System.Security.Cryptography.SHA256]::Create().ComputeHash($Utf8.GetBytes($text))) -replace '-', '').ToLower()
    return $stageObject
}

# After a tweak that changed a staged file and means the manifest to follow.
function Stage-Rehash($StageObject, [string] $GuestPath) {
    $StageObject.Map[$GuestPath] = Sha (Stage-File $StageObject.Path $GuestPath)
}

# ---- running the script under test ----

function Invoke-Update($StageObject, [string[]] $Extra = @(), [string] $CultureName = '') {
    $logFrom = 0
    if (Test-Path -LiteralPath $UpdateLog) { $logFrom = (Get-Item -LiteralPath $UpdateLog).Length }
    $pidBefore = Get-AgentPid
    $under = Join-Path $StageObject.Path 'C\ProgramData\LSW\lsw-guest-update.ps1'
    $started = Get-Date
    if ($CultureName) {
        $command = "[Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::GetCultureInfo('$CultureName'); " +
            "Write-Output ('culture=' + [Threading.Thread]::CurrentThread.CurrentCulture.Name + ' year=' + (Get-Date).ToString('yyyy')); " +
            "& '$under' -Stage '$($StageObject.Path)' -Release $($StageObject.Release) -NoRestart $($Extra -join ' '); exit `$LASTEXITCODE"
        $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -Command $command 2>&1 | Out-String
    } else {
        $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $under -Stage $StageObject.Path `
            -Release $StageObject.Release -NoRestart @Extra 2>&1 | Out-String
    }
    $exitCode = $LASTEXITCODE
    $logText = ''
    if (Test-Path -LiteralPath $UpdateLog) {
        $bytes = [System.IO.File]::ReadAllBytes($UpdateLog)
        if ($bytes.Length -gt $logFrom) { $logText = [System.Text.Encoding]::ASCII.GetString($bytes, $logFrom, $bytes.Length - $logFrom) }
    }
    Say ('  ran in {0:N0} s, exit {1}' -f ((Get-Date) - $started).TotalSeconds, $exitCode)
    return [pscustomobject]@{
        ExitCode  = $exitCode
        Output    = $output
        Result    = Read-Json $ResultJson
        Release   = Read-Json $ReleaseJson
        Log       = $logText
        PidBefore = $pidBefore
        Started   = $started
    }
}

# k: the recovery actions are the real ones after every run.
function Check-Recovery([string] $Label) {
    $have = Get-Recovery
    Check ('k. recovery actions are the real ones after ' + $Label) ($have -eq $WantedRecovery) ('have ' + $have)
}

# ---- putting back ----

# Everything a scenario may have changed; says what it had to put back.
function Reset-Guest([string] $Label) {
    $repaired = @()
    Wait-Agent 120 | Out-Null

    $stageDir = Join-Path $WorkDir $Label
    if (Test-Path -LiteralPath $stageDir) { Remove-Item -LiteralPath $stageDir -Recurse -Force -ErrorAction SilentlyContinue }

    if ((Sha $ReleaseJson) -ne $script:SavedRelease.Hash) {
        [System.IO.File]::WriteAllBytes($ReleaseJson, $script:SavedRelease.Bytes)
    }
    $original = Join-Path $WorkDir 'installed-script.ps1'
    if ((Sha $InstalledScript) -ne (Sha $original)) {
        Copy-Item -LiteralPath $original -Destination $InstalledScript -Force
    }

    # Pending renames this scenario registered, and the copies they point at.
    $pending = Get-Pending
    $same = ($null -eq $pending -and $null -eq $script:SavedPending) -or
        ($null -ne $pending -and $null -ne $script:SavedPending -and (($pending -join "`n") -eq ($script:SavedPending -join "`n")))
    if (-not $same) {
        if ($null -eq $script:SavedPending) {
            Remove-ItemProperty -LiteralPath $SessionKey -Name PendingFileRenameOperations -ErrorAction SilentlyContinue
        } else {
            Set-ItemProperty -LiteralPath $SessionKey -Name PendingFileRenameOperations -Value ([string[]] $script:SavedPending)
        }
        $repaired += 'pending renames'
    }
    if (Test-Path -LiteralPath $PendingDir) { Remove-Item -LiteralPath $PendingDir -Recurse -Force; $repaired += 'pending-loaders' }

    if (Test-Path -LiteralPath $PreviousDir) {
        Get-ChildItem -LiteralPath $PreviousDir -Force | Remove-Item -Recurse -Force
    }

    # Installed files back to what they were; files that were not there go.
    foreach ($guest in $script:Orig.Keys) {
        if ((Sha $guest) -ne $script:Orig[$guest] -and $guest -ne $InstalledScript) {
            if ($guest -like '*lsw-agent.exe' -or $guest -like '*remotewin-guest.exe') {
                sc.exe stop lsw-agent | Out-Null
                Start-Sleep -Seconds 5
                Get-Process -Name 'lsw-agent', 'remotewin-guest' -ErrorAction SilentlyContinue | Stop-Process -Force
            }
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $guest) | Out-Null
            Copy-Item -LiteralPath (Stage-File $BaseDir $guest) -Destination $guest -Force
            $repaired += $guest
            if ($guest -like '*lsw-agent.exe') { sc.exe start lsw-agent | Out-Null }
        }
    }
    foreach ($dir in @('C:\av\package', 'C:\av\loaders', 'C:\ProgramData\LSW\virtiofs')) {
        foreach ($path in @(Get-ChildItem -LiteralPath $dir -Recurse -File -Force | ForEach-Object { $_.FullName })) {
            if (-not $script:Orig.ContainsKey($path)) { Remove-Item -LiteralPath $path -Force; $repaired += ('extra ' + $path) }
        }
    }
    $haveMarker = ''
    if (Test-Path -LiteralPath $Marker) { $haveMarker = [System.IO.File]::ReadAllText($Marker, $Utf8) }
    if ($haveMarker -cne $script:SavedMarker) {
        [System.IO.File]::WriteAllText($Marker, $script:SavedMarker, $Utf8)
        $repaired += 'driver marker'
    }
    Wait-Agent 120 | Out-Null
    if ($repaired.Count -gt 0) { Say ('  put back after ' + $Label + ': ' + ($repaired -join '; ')) }
}

# True when no installed file differs from what it was at the start.
function Test-Pristine {
    foreach ($guest in $script:Orig.Keys) {
        if ($guest -ne $InstalledScript -and (Sha $guest) -ne $script:Orig[$guest]) { return $false }
    }
    return $true
}

function Dir-Empty([string] $Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $true }
    return @(Get-ChildItem -LiteralPath $Path -Force).Count -eq 0
}

# ---- scenarios ----

# The checks every run that goes through all steps shares.
function Check-Steps([string] $Tag, $Run, [hashtable] $Expected) {
    foreach ($stepName in $StepNames) {
        if (-not $Expected.ContainsKey($stepName)) { continue }
        $have = Get-StepStatus $Run.Result $stepName
        Check ($Tag + ' step "' + $stepName + '" is ' + $Expected[$stepName]) ($have -eq $Expected[$stepName]) ('is ' + $have + '; ' + (Format-Steps $Run.Result))
    }
}

function Scenario-A {
    $st = New-Stage 'a'
    $releaseBefore = $script:SavedRelease.Hash
    $run = Invoke-Update $st
    Check 'a. exit 0 and result ok' ($run.ExitCode -eq 0 -and $run.Result.ok -eq $true) (Format-Steps $run.Result)
    Check-Steps 'a.' $run @{
        'verify' = 'done'; 'stop' = 'done'; 'agent and mirror' = 'skipped'; 'driver' = 'skipped'; 'loaders' = 'skipped'
        'shared folders' = 'done'; 'service and firewall' = 'skipped'; 'start' = 'done' }
    Check 'a. no restart marked' ($run.Result.restart -eq $false) ('restart=' + $run.Result.restart)
    Check 'a. release.json has the stage release' ($run.Release -and $run.Release.release -eq $st.Release -and $run.Release.manifest -ceq $st.ManifestText) ('release=' + $run.Release.release + ' want ' + $st.Release)
    Check 'a. release.json is newly written' ((Sha $ReleaseJson) -ne $releaseBefore)
    Check 'a. agent answering on 5102 afterwards' (Wait-Agent 90)
    Check 'a. agent was restarted (new process)' ((Get-AgentPid) -ne $run.PidBefore) ('pid ' + $run.PidBefore)
    Check-Recovery 'a'
}

function Scenario-Rejected([string] $Tag, [scriptblock] $Spoil, [string] $MessagePart) {
    $st = New-Stage $Tag
    & $Spoil $st
    $releaseBefore = $script:SavedRelease.Hash
    $pidBefore = Get-AgentPid
    $run = Invoke-Update $st
    $verifyFailed = (Get-StepStatus $run.Result 'verify') -eq 'failed'
    Check ($Tag + '. run ends at verify, ok:false, exit 1') ($run.ExitCode -eq 1 -and $run.Result.ok -eq $false -and $verifyFailed) (Format-Steps $run.Result)
    Check ($Tag + '. message names verify and the file') ($run.Result.message -like ('verify:*' + $MessagePart + '*')) ('message: ' + $run.Result.message)
    $later = @('stop', 'agent and mirror', 'driver', 'loaders', 'shared folders', 'service and firewall')
    $allNotRun = @($later | Where-Object { (Get-StepStatus $run.Result $_) -ne 'not run' }).Count -eq 0
    Check ($Tag + '. later steps not run, start skipped') ($allNotRun -and (Get-StepStatus $run.Result 'start') -eq 'skipped') (Format-Steps $run.Result)
    Check ($Tag + '. agent never stopped (same PID)') ($pidBefore -ne 0 -and (Get-AgentPid) -eq $pidBefore -and $run.PidBefore -eq $pidBefore) ('before ' + $pidBefore + ' after ' + (Get-AgentPid))
    Check ($Tag + '. release.json unchanged') ((Sha $ReleaseJson) -eq $releaseBefore)
    Check ($Tag + '. nothing replaced (files, previous\)') ((Test-Pristine) -and (Dir-Empty $PreviousDir)) 'an installed file changed or previous\ is not empty'
    Check ($Tag + '. agent answering on 5102') (Test-Port5102)
    Check-Recovery $Tag
}

function Scenario-B {
    # One byte of one staged file differs; the manifest is the original's.
    Scenario-Rejected 'b' {
        param($st)
        $victim = Stage-File $st.Path 'C:\av\loaders\x86\OpenCL.dll'
        $bytes = [System.IO.File]::ReadAllBytes($victim)
        $bytes[100] = $bytes[100] -bxor 0xFF
        [System.IO.File]::WriteAllBytes($victim, $bytes)
    } 'C:\av\loaders\x86\OpenCL.dll'
}

function Scenario-C {
    Scenario-Rejected 'c' {
        param($st)
        Remove-Item -LiteralPath (Stage-File $st.Path 'C:\av\package\avvk.json') -Force
    } 'C:\av\package\avvk.json'
}

function Scenario-D {
    # An agent that never serves: the script must put the previous one back.
    $bogus = {
        param($st)
        Copy-Item -LiteralPath 'C:\Windows\System32\timeout.exe' -Destination (Stage-File $st.Path 'C:\ProgramData\LSW\lsw-agent.exe') -Force
        Stage-Rehash $st 'C:\ProgramData\LSW\lsw-agent.exe'
    }
    $st = New-Stage 'd' $bogus
    $releaseBefore = $script:SavedRelease.Hash
    $run = Invoke-Update $st
    Check 'd. ok:false, exit 1' ($run.ExitCode -eq 1 -and $run.Result.ok -eq $false) (Format-Steps $run.Result)
    Check 'd. message names the start step' ($run.Result.message -like 'start:*') ('message: ' + $run.Result.message)
    Check 'd. start failed, agent and mirror done' ((Get-StepStatus $run.Result 'start') -eq 'failed' -and (Get-StepStatus $run.Result 'agent and mirror') -eq 'done') (Format-Steps $run.Result)
    Check 'd. the previous agent was put back' ((Sha 'C:\ProgramData\LSW\lsw-agent.exe') -eq $script:Orig['C:\ProgramData\LSW\lsw-agent.exe'] -and $run.Log -match 'putting the previous one back') 'the installed agent differs from the original'
    Check 'd. previous agent answering on 5102' (Wait-Agent 90)
    Check 'd. release.json unchanged' ((Sha $ReleaseJson) -eq $releaseBefore)
    Check-Recovery 'd'
}

function Scenario-E {
    $st = New-Stage 'e'
    $run = Invoke-Update $st @('-Force')
    $pendingNow = Get-Pending
    Check 'e. exit 0 and result ok' ($run.ExitCode -eq 0 -and $run.Result.ok -eq $true) (Format-Steps $run.Result)
    $all = @{}
    foreach ($stepName in $StepNames) { $all[$stepName] = 'done' }
    Check-Steps 'e.' $run $all
    Check 'e. restart recorded true, not performed' ($run.Result.restart -eq $true -and $run.Log -match 'a restart is due; -NoRestart') ('restart=' + $run.Result.restart)
    Check 'e. driver re-signed and installed (provision sign and install ran)' ($run.Log -match 'provision sign exited (0|3010)' -and $run.Log -match 'provision install exited (0|3010)')
    $want = Driver-Lines $st.Map
    $have = ''
    if (Test-Path -LiteralPath $Marker) { $have = [System.IO.File]::ReadAllText($Marker, $Utf8) }
    Check 'e. installed.sha256 marker equals the driver lines' ($have -ceq $want) ('marker ' + $have.Length + ' bytes, want ' + $want.Length)
    Check 'e. marker written by this run' ((Test-Path -LiteralPath $Marker) -and (Get-Item -LiteralPath $Marker).LastWriteTime -ge $run.Started.AddSeconds(-2))
    Check 'e. release.json has the stage release' ($run.Release -and $run.Release.release -eq $st.Release)
    Check 'e. agent answering on 5102 afterwards' (Wait-Agent 90)
    if ($null -ne $pendingNow) { Say '  note: forced loader copy was pending a reboot for some file (put back by the reset)' }
    Check-Recovery 'e'
}

function Scenario-F {
    $extra = 'C:\ProgramData\LSW\virtiofs\extra-test.txt'
    $deep = 'C:\ProgramData\LSW\virtiofs\old\installer.msi'
    Set-Content -LiteralPath $extra -Value 'not in the release' -Encoding Ascii
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $deep) | Out-Null
    Set-Content -LiteralPath $deep -Value 'an old installer' -Encoding Ascii
    $st = New-Stage 'f'
    $run = Invoke-Update $st
    Check 'f. exit 0 and result ok' ($run.ExitCode -eq 0 -and $run.Result.ok -eq $true) (Format-Steps $run.Result)
    Check 'f. the extra files are gone' (-not (Test-Path -LiteralPath $extra) -and -not (Test-Path -LiteralPath $deep)) 'an extra file is still in virtiofs\'
    Check 'f. shared folders step done' ((Get-StepStatus $run.Result 'shared folders') -eq 'done') (Format-Steps $run.Result)
    $kept = @($script:Orig.Keys | Where-Object { $_.StartsWith('C:\ProgramData\LSW\virtiofs\') -and (Sha $_) -ne $script:Orig[$_] }).Count -eq 0
    Check 'f. the release files in virtiofs\ are intact' $kept
    Check-Recovery 'f'
}

function Scenario-G {
    Remove-Item -LiteralPath $Marker -Force
    $st = New-Stage 'g'
    $run = Invoke-Update $st
    Check 'g. exit 0 and result ok' ($run.ExitCode -eq 0 -and $run.Result.ok -eq $true) (Format-Steps $run.Result)
    Check 'g. driver step ran instead of skipping' ((Get-StepStatus $run.Result 'driver') -eq 'done' -and $run.Log -match 'provision install exited (0|3010)') (Format-Steps $run.Result)
    $want = Driver-Lines $st.Map
    $have = ''
    if (Test-Path -LiteralPath $Marker) { $have = [System.IO.File]::ReadAllText($Marker, $Utf8) }
    Check 'g. the marker is written after it' ($have -ceq $want) ('marker ' + $have.Length + ' bytes, want ' + $want.Length)
    Check 'g. restart recorded' ($run.Result.restart -eq $true)
    Check 'g. agent answering on 5102 afterwards' (Wait-Agent 90)
    Check-Recovery 'g'
}

function Scenario-H {
    $target = Join-Path $env:SystemRoot 'System32\vulkan-1.dll'
    $st = New-Stage 'h' {
        param($st)
        $path = Stage-File $st.Path 'C:\av\loaders\x64\vulkan-1.dll'
        $bytes = [System.IO.File]::ReadAllBytes($path)
        $longer = New-Object byte[] ($bytes.Length + 4096)
        [Array]::Copy($bytes, $longer, $bytes.Length)
        [System.IO.File]::WriteAllBytes($path, $longer)
        Stage-Rehash $st 'C:\av\loaders\x64\vulkan-1.dll'
    }
    $before = Sha $target
    # Open for reading only: nobody may write or delete it while held.
    $held = [System.IO.File]::Open($target, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    try {
        $run = Invoke-Update $st
    } finally {
        $held.Close()
    }
    Check 'h. exit 0 and result ok' ($run.ExitCode -eq 0 -and $run.Result.ok -eq $true) (Format-Steps $run.Result)
    Check 'h. the copy failed and was left to the next boot' ($run.Log -match 'System32\\vulkan-1\.dll is in use; replaced at the next boot') 'no "replaced at the next boot" line'
    $pending = Get-Pending
    $registered = $null -ne $pending -and (@($pending | Where-Object { $_ -like '*System32\vulkan-1.dll' }).Count -gt 0)
    Check 'h. MoveFileEx pending rename registered for System32\vulkan-1.dll' $registered ('PendingFileRenameOperations: ' + (($pending) -join ' | '))
    Check 'h. the held file was not replaced now' ((Sha $target) -eq $before)
    Check 'h. restart recorded' ($run.Result.restart -eq $true)
    Check 'h. loaders step done' ((Get-StepStatus $run.Result 'loaders') -eq 'done') (Format-Steps $run.Result)
    Check 'h. agent answering on 5102 afterwards' (Wait-Agent 90)
    Check-Recovery 'h'
    # Reset-Guest removes the pending rename, its copy and C:\av's loader.
}

function Scenario-I {
    $st = New-Stage 'i'
    $releaseBefore = $script:SavedRelease.Hash
    $pidBefore = Get-AgentPid
    $held = New-Object System.Threading.Mutex($false, 'Global\lsw-guest-update')
    $taken = $held.WaitOne(10000)
    try {
        $run = Invoke-Update $st
    } finally {
        if ($taken) { $held.ReleaseMutex() }
        $held.Dispose()
    }
    Check 'i. the harness held the lock' $taken
    Check 'i. second instance: ok:false "another update is running", exit 1' ($run.ExitCode -eq 1 -and $run.Result.ok -eq $false -and $run.Result.message -like 'another update is running*') (Format-Steps $run.Result)
    Check 'i. no steps ran' (@($run.Result.steps | Where-Object { $_ }).Count -eq 0) (Format-Steps $run.Result)
    Check 'i. agent never stopped (same PID)' ($pidBefore -ne 0 -and (Get-AgentPid) -eq $pidBefore) ('before ' + $pidBefore + ' after ' + (Get-AgentPid))
    Check 'i. release.json unchanged, nothing replaced' ((Sha $ReleaseJson) -eq $releaseBefore -and (Test-Pristine) -and (Dir-Empty $PreviousDir))
    Check 'i. the staging directory was left alone' (Test-Path -LiteralPath $st.Path)
    Check-Recovery 'i'
}

function Scenario-J {
    $iso = '^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$'
    foreach ($culture in @('fi-FI', 'th-TH')) {
        $tag = 'j(' + $culture + ')'
        $st = New-Stage ('j-' + $culture)
        $run = Invoke-Update $st @('-Version', '9.8.7-test') $culture
        $year = ''
        if ($run.Output -match 'year=(\d+)') { $year = $Matches[1] }
        Check ($tag + ' the run really had that culture') ($run.Output -match ('culture=' + $culture)) ('output: ' + $run.Output.Trim())
        if ($culture -eq 'th-TH') {
            Check ($tag + ' and its calendar is not the Gregorian one') ($year -eq '2569') ('year=' + $year)
        }
        Check ($tag + ' exit 0 and result ok') ($run.ExitCode -eq 0 -and $run.Result.ok -eq $true) (Format-Steps $run.Result)
        Check ($tag + ' release.json has -Release and -Version') ($run.Release -and $run.Release.release -eq $st.Release -and $run.Release.version -eq '9.8.7-test') ('release.json: ' + ($run.Release | ConvertTo-Json -Compress))
        $finished = [string] $run.Result.finished
        $parsed = [datetime]::MinValue
        $isDate = [datetime]::TryParseExact($finished, "yyyy-MM-dd'T'HH:mm:ss.fff'Z'", [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::AdjustToUniversal -bor [Globalization.DateTimeStyles]::AssumeUniversal, [ref] $parsed)
        Check ($tag + ' update-result.json finished is invariant ISO with milliseconds') ($finished -match $iso -and $isDate -and [math]::Abs(([datetime]::UtcNow - $parsed).TotalMinutes) -lt 15) ('finished=' + $finished)
        $applied = [string] $run.Release.applied
        Check ($tag + ' release.json applied is the same form') ($applied -match $iso) ('applied=' + $applied)
        Check ($tag + ' agent answering on 5102 afterwards') (Wait-Agent 90)
        Check-Recovery $tag
        Reset-Guest ('j-' + $culture)
    }
}

# ---- the run ----

function Run-Scenario([string] $Letter, [string] $Title, [scriptblock] $Body) {
    if ($Only -and $Only.IndexOf($Letter) -lt 0) { return }
    Say ('--- ' + $Letter + ': ' + $Title)
    $sceneName = $Letter
    try {
        & $Body
    } catch {
        $script:Failed++
        Say ('FAIL ' + $Letter + '. scenario threw :: ' + $_.Exception.Message + ' at ' + $_.InvocationInfo.PositionMessage)
    }
    try { Reset-Guest $sceneName } catch { Say ('reset after ' + $Letter + ' threw: ' + $_.Exception.Message) }
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogFile) | Out-Null
Set-Content -LiteralPath $LogFile -Value ('=== lsw-guest-update scenario tests, script under test ' + $ScriptUnderTest) -Encoding Ascii
Say ('PowerShell ' + $PSVersionTable.PSVersion + ' as ' + [System.Security.Principal.WindowsIdentity]::GetCurrent().Name)

try {
    # What is put back at the end (and between scenarios).
    $script:SavedRelease = @{ Bytes = [System.IO.File]::ReadAllBytes($ReleaseJson); Hash = (Sha $ReleaseJson) }
    $script:SavedResult = @{ Bytes = [System.IO.File]::ReadAllBytes($ResultJson) }
    $script:SavedLog = @{ Length = (Get-Item -LiteralPath $UpdateLog).Length }
    $script:SavedMarker = [System.IO.File]::ReadAllText($Marker, $Utf8)
    $script:SavedPending = Get-Pending
    Build-Base
    Say ('base stage: ' + $script:Orig.Count + ' files; shares "' + $script:SharesText + '"')

    Check '0. start state: agent running and answering on 5102' (Wait-Agent 60)
    Check '0. start state: the driver marker equals the installed driver files' ($script:SavedMarker -ceq (Driver-Lines $script:Orig)) 'the installed files do not match the marker; the scenarios below assume a current guest'
    Check '0. start state: loaders in System32 equal the installed ones' ((Sha (Join-Path $env:SystemRoot 'System32\vulkan-1.dll')) -eq $script:Orig['C:\av\loaders\x64\vulkan-1.dll'])
    Check-Recovery '(start)'

    Run-Scenario 'a' 'current stage, not forced' { Scenario-A }
    Run-Scenario 'b' 'a staged byte differs from the manifest' { Scenario-B }
    Run-Scenario 'c' 'a staged file is missing' { Scenario-C }
    Run-Scenario 'd' 'agent that never listens' { Scenario-D }
    Run-Scenario 'f' 'extra file in virtiofs' { Scenario-F }
    Run-Scenario 'h' 'loader held open in System32' { Scenario-H }
    Run-Scenario 'i' 'a second instance while one runs' { Scenario-I }
    Run-Scenario 'j' '-Version, -Release, culture-proof timestamps' { Scenario-J }
    Run-Scenario 'g' 'driver marker missing' { Scenario-G }
    Run-Scenario 'e' 'forced run with the current files' { Scenario-E }
}
catch {
    $script:Failed++
    Say ('FAIL harness threw :: ' + $_.Exception.Message + ' at ' + $_.InvocationInfo.PositionMessage)
}
finally {
    # The logs the scenarios appended to are put back as they were.
    try {
        if ($script:SavedResult) { [System.IO.File]::WriteAllBytes($ResultJson, $script:SavedResult.Bytes) }
        if ($script:SavedLog) {
            $keep = [System.IO.File]::ReadAllBytes($UpdateLog)
            $trimmed = New-Object byte[] ([math]::Min($keep.Length, $script:SavedLog.Length))
            [Array]::Copy($keep, $trimmed, $trimmed.Length)
            [System.IO.File]::WriteAllBytes($UpdateLog, $trimmed)
        }
        Reset-Guest 'end'
        Remove-Item -LiteralPath $WorkDir -Recurse -Force -ErrorAction SilentlyContinue
    } catch {
        Say ('final reset threw: ' + $_.Exception.Message)
    }
    Check 'z. nothing left: no pending renames, previous\ empty, release.json as it was' (
        ((Get-Pending) -join '|') -eq ($script:SavedPending -join '|') -and (Dir-Empty $PreviousDir) -and
        -not (Test-Path -LiteralPath $PendingDir) -and (Sha $ReleaseJson) -eq $script:SavedRelease.Hash)
    Say ('SUMMARY: {0} passed, {1} failed' -f $script:Passed, $script:Failed)
    schtasks.exe /Delete /TN lsw-update-tests /F 2>&1 | Out-Null
}
