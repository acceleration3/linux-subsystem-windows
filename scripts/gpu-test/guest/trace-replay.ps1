# trace-replay.ps1 -Config PASS.json : one replay pass, run in the console session.
# The config (written by trace_suite.py) names the program, its argument line,
# its working directory, extra environment, where standard output and error go,
# where the result goes, a timeout and how often to sample memory:
#   {"exe", "args", "cwd", "env": {}, "stdout", "stderr", "result", "timeout_s", "sample_ms", "modules", "linger_s",
#    "share_console"}
# linger_s: the program is closed (its main window, then killed) after this
# long, and "closed" says so - for a retracer told to keep its window (-w).
# share_console: the program runs in this script's console (hidden, as the
# desktop task starts it) instead of a new one of its own - gfxrecon-replay
# ends with "Press any key to close this window" in a console only it holds,
# and waited there until the pass timed out.
# The result is JSON: exit_code, seconds, timed_out, error, samples as
# [seconds, private bytes, working set] while the process ran, and the loaded
# DLLs whose names match the "modules" pattern (which driver it really used).
# A 32-bit replayer is watched from the 32-bit PowerShell: a 64-bit one does
# not see a WOW64 process's DLLs.
param([Parameter(Mandatory = $true)][string]$Config)
$ErrorActionPreference = 'Stop'
$c = Get-Content -Raw -LiteralPath $Config | ConvertFrom-Json
$result = [ordered]@{ exit_code = $null; seconds = 0.0; timed_out = $false; closed = $false; error = $null; samples = @(); modules = @() }
$samples = New-Object System.Collections.ArrayList
$modules = New-Object 'System.Collections.Generic.SortedSet[string]'
$nextLook = 0.0
$out = $null; $err = $null
try {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $c.exe
    $psi.Arguments = $c.args
    $psi.WorkingDirectory = $c.cwd
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = -not $c.share_console
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    if ($c.env) {
        foreach ($p in $c.env.PSObject.Properties) { $psi.EnvironmentVariables[$p.Name] = [string]$p.Value }
    }
    $out = [IO.File]::Create($c.stdout)
    $err = [IO.File]::Create($c.stderr)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $process = [Diagnostics.Process]::Start($psi)
    $process.StandardInput.Close()
    $copies = @($process.StandardOutput.BaseStream.CopyToAsync($out), $process.StandardError.BaseStream.CopyToAsync($err))
    $interval = if ($c.sample_ms) { [int]$c.sample_ms } else { 500 }
    $nextSample = 0.0
    # The DLLs are looked at every 20 ms until a driver shows (a replay of a
    # few frames is over in a fraction of a second), then every 5 s.
    $driverSeen = $false
    while (-not $process.WaitForExit($(if ($driverSeen) { $interval } else { 20 }))) {
        try {
            $now = $watch.Elapsed.TotalSeconds
            if ($now -ge $nextSample) {
                $nextSample = $now + $interval / 1000.0
                $process.Refresh()
                [void]$samples.Add(@([math]::Round($now, 3), $process.PrivateMemorySize64, $process.WorkingSet64))
            }
            if ($c.modules -and ($now -ge $nextLook -or -not $driverSeen)) {
                $nextLook = $now + 5
                foreach ($m in $process.Modules) {
                    if ($m.ModuleName -match $c.modules) {
                        [void]$modules.Add($m.ModuleName.ToLower())
                        if ($m.ModuleName -match '^(av\w+|d3d10warp)\.dll$') { $driverSeen = $true }
                    }
                }
            }
        } catch { }
        if ($c.linger_s -and $watch.Elapsed.TotalSeconds -gt [double]$c.linger_s -and -not $result.closed) {
            # A pass asked to stay open (a retracer's -w) is closed once looked at.
            $result.closed = $true
            [void]$process.CloseMainWindow()
            if (-not $process.WaitForExit(5000)) { try { $process.Kill() } catch { } }
            continue
        }
        if ($watch.Elapsed.TotalSeconds -gt [double]$c.timeout_s) {
            $result.timed_out = $true
            try { $process.Kill() } catch { }
            [void]$process.WaitForExit(30000)
            break
        }
    }
    $process.WaitForExit()
    [void][Threading.Tasks.Task]::WaitAll($copies, 60000)
    $result.seconds = [math]::Round($watch.Elapsed.TotalSeconds, 3)
    $result.exit_code = $process.ExitCode
} catch {
    $result.error = $_.ToString()
} finally {
    if ($out) { $out.Close() }
    if ($err) { $err.Close() }
}
$result.samples = $samples.ToArray()
$result.modules = @($modules)
$json = $result | ConvertTo-Json -Depth 5 -Compress
[IO.File]::WriteAllText($c.result + '.tmp', $json)
Move-Item -Force -LiteralPath ($c.result + '.tmp') -Destination $c.result
"exit $($result.exit_code) in $($result.seconds) s"
