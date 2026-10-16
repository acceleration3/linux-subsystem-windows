# trace-capture.ps1: an application run under a capture tool, in the guest,
# its trace left in -Out with what the replay suite needs beside it.
#
#   -Tool apitrace -Api d3d9|dxgi|gl|d3d8   Direct3D 8/9/10/11 or OpenGL calls
#   -Tool clintercept                       OpenCL kernels with their inputs and outputs
#   -Tool gfxr [-Api vulkan]                Vulkan, through the GFXReconstruct layer
#   -Tool gfxr -Api d3d12                   Direct3D 12, GFXReconstruct's d3d12.dll, dxgi.dll and
#                                           d3d12_capture.dll beside -Exe (its D3D12 build)
#   -Tool probe -Process NAME               which graphics DLLs a running process loaded
#
# What is launched: -Exe PATH [-Arguments "..."], or -Package FAMILY [-AppId ID]
# (an MSIX app, run inside its package with Invoke-CommandInDesktopPackage), or
# -SteamApp ID with -Exe naming the game's executable (launched by Steam).
#
# -Mode, how the tool gets into the process (auto picks the first that fits):
#   apitrace     inject (apitrace trace: DLL injection, the default; with -SteamApp a
#                steam_appid.txt beside -Exe so the game starts outside Steam),
#                wrappers (its DLLs copied beside -Exe: for an application that
#                misbehaves injected - Resolve's plugins fail to load - from an
#                elevated shell when -Exe is under Program Files; with -SteamApp it
#                is launched by Steam)
#   clintercept  system (System32's OpenCL.dll swapped for CLIntercept's, then put
#                back: MSIX apps; needs an elevated shell), local (CLIntercept's
#                OpenCL.dll beside -Exe), cliloader
#   gfxr         layer (Vulkan); for D3D12 the capture DLLs beside -Exe, which is
#                started directly (with -SteamApp, a steam_appid.txt beside it:
#                a game Steam starts would not get the capture's environment)
#
# -GfxrFrames 100-200 trims a GFXReconstruct capture to those frames;
# -GfxrTrigger F12 [-GfxrTriggerFrames N] captures nothing until the key is
# pressed, then N frames (until it is pressed again without N).
#
# -GlOut DIR (with -Tool clintercept): the same run's OpenGL calls too,
# through apitrace's opengl32.dll wrapper beside -Exe, as a second capture in
# DIR (capture.trace, frames.txt, capture.json, capture.done): one session
# of an application that draws with OpenGL and computes with OpenCL (Resolve)
# gives both traces. Like the wrappers mode, it needs an elevated shell when
# -Exe is under Program Files.
# -ClearFirst "PATH;PATH": files or directories deleted before the launch
# (an application's OpenCL kernel cache, so CLIntercept records sources).
#
# The application is used by hand until it exits, or closed after -Seconds.
# Then: apitrace's trace gets frames.txt (the call ending each frame, from
# apitrace dump); CLIntercept's Replay directory is moved to -Out\Replay;
# capture.json records what was run and on what; capture.done says ok or why not.
param(
    [ValidateSet('apitrace', 'clintercept', 'gfxr', 'probe')][string]$Tool = 'apitrace',
    [ValidateSet('d3d8', 'd3d9', 'dxgi', 'gl', 'opencl', 'vulkan', 'd3d12')][string]$Api = 'd3d9',
    [string]$Exe = '',
    [string]$Arguments = '',
    [string]$Package = '',
    [string]$AppId = '',
    [int]$SteamApp = 0,
    [ValidateSet('auto', 'inject', 'wrappers', 'system', 'local', 'cliloader', 'layer')][string]$Mode = 'auto',
    [string]$Out = '',
    [string]$Process = '',
    [int]$Seconds = 0,
    [string]$Tools = 'C:\av\traces\tools',
    [int]$ClKernels = 200,
    [string]$ClKernelName = '',
    [string]$GfxrFrames = '',
    [string]$GfxrTrigger = '',
    [int]$GfxrTriggerFrames = 0,
    [string]$GlOut = '',
    [string]$ClearFirst = '',
    [switch]$Repack
)
$ErrorActionPreference = 'Stop'
$Steam = 'C:\Program Files (x86)\Steam'

# Straight to standard output, never into a function's return value.
function Log($line) {
    $stamped = "[$((Get-Date).ToString('HH:mm:ss'))] $line"
    [Console]::Out.WriteLine($stamped)
    if ($script:LogFile) { Add-Content -LiteralPath $script:LogFile -Value $stamped }
}

# A console program run to its end, its output in a file: its exit code.
# (Windows PowerShell turns a native program's stderr into errors, which
# -ErrorAction Stop would throw on.)
function Run-Native($program, $arguments, $output) {
    $p = Start-Process -FilePath $program -ArgumentList $arguments -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $output -RedirectStandardError "$output.err"
    if ((Test-Path "$output.err") -and (Get-Item "$output.err").Length -gt 0) { Get-Content "$output.err" | ForEach-Object { Log "  $_" } }
    Remove-Item -Force -ErrorAction SilentlyContinue "$output.err"
    $p.ExitCode
}

function Arch-Of($path) {
    $bytes = [IO.File]::ReadAllBytes($path)
    $pe = [BitConverter]::ToInt32($bytes, 0x3C)
    switch ([BitConverter]::ToUInt16($bytes, $pe + 4)) { 0x8664 { 'x64' } 0x14c { 'x86' } default { throw "$path is no x86 or x64 executable" } }
}

function Tool-Dir($name) {
    $found = Get-ChildItem -Directory -Path $Tools -Filter "$name-*" -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1
    if (-not $found) { throw "$name is not in ${Tools}: push the tools first (traces.py push-tools)" }
    $found.FullName
}

function Wait-Target($name, $launcher) {
    $deadline = (Get-Date).AddSeconds(300)
    $target = $null
    while (-not $target) {
        $target = Get-Process -Name $name -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($target) { break }
        if ((Get-Date) -gt $deadline) { throw "$name never started" }
        if ($launcher -and $launcher.HasExited -and $launcher.ExitCode -ne 0) { throw "the launcher exited $($launcher.ExitCode) before $name started" }
        Start-Sleep -Seconds 1
    }
    Log "$name started (pid $($target.Id))"
    if ($Seconds -gt 0) {
        $closeAt = (Get-Date).AddSeconds($Seconds)
        while ((Get-Date) -lt $closeAt -and (Get-Process -Name $name -ErrorAction SilentlyContinue)) { Start-Sleep -Seconds 1 }
    }
    if ($Seconds -gt 0 -and (Get-Process -Name $name -ErrorAction SilentlyContinue)) {
        Log "closing $name after $Seconds s"
        Get-Process -Name $name -ErrorAction SilentlyContinue | ForEach-Object { [void]$_.CloseMainWindow() }
        $closeBy = (Get-Date).AddSeconds(60)
        while ((Get-Process -Name $name -ErrorAction SilentlyContinue) -and (Get-Date) -lt $closeBy) { Start-Sleep -Seconds 1 }
        Get-Process -Name $name -ErrorAction SilentlyContinue | ForEach-Object { Log "killing $name (pid $($_.Id))"; $_.Kill() }
    }
    while (Get-Process -Name $name -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }
    if ($launcher) { [void]$launcher.WaitForExit(120000) }
    Log "$name exited"
    Start-Sleep -Seconds 3
}

function Launch($program, $arguments, $directory) {
    if ($Package) {
        Log "in package $Package!${AppId}: $program $arguments"
        Invoke-CommandInDesktopPackage -PackageFamilyName $Package -AppId $AppId -Command $program -Args $arguments | Out-Null
        return $null
    }
    Log "launching $program $arguments"
    if ($arguments) { Start-Process -FilePath $program -ArgumentList $arguments -WorkingDirectory $directory -PassThru }
    else { Start-Process -FilePath $program -WorkingDirectory $directory -PassThru }
}

# apitrace's wrapper DLLs for $Api copied beside the executable (each one
# added to $script:cleanup), and the desktop's traces before the launch.
function Place-Wrappers($at, $api, $exeDir) {
    $dlls = switch ($api) { 'd3d9' { @('d3d9.dll') } 'd3d8' { @('d3d8.dll') } 'gl' { @('opengl32.dll') } 'dxgi' { @('dxgitrace.dll', 'dxgi.dll', 'd3d11.dll') } }
    foreach ($dll in $dlls) {
        $placed = Join-Path $exeDir $dll
        if (Test-Path -LiteralPath $placed) { throw "$placed exists already: not overwriting the application's own" }
        Copy-Item -LiteralPath (Join-Path $at "lib\wrappers\$dll") -Destination $placed
        $script:cleanup += $placed
    }
    $desktop = [Environment]::GetFolderPath('Desktop')
    @(Get-ChildItem -Path $desktop -Filter "$Process*.trace" -ErrorAction SilentlyContinue | ForEach-Object FullName)
}

# The trace the wrappers wrote (the largest new one on the desktop) moved to $trace.
function Collect-Wrapped($before, $trace) {
    $desktop = [Environment]::GetFolderPath('Desktop')
    $new = Get-ChildItem -Path $desktop -Filter "$Process*.trace" -ErrorAction SilentlyContinue | Where-Object { $before -notcontains $_.FullName } | Sort-Object Length -Descending
    if (-not $new) { throw "no new $Process*.trace on the desktop: the wrappers were not loaded" }
    Move-Item -Force -LiteralPath $new[0].FullName -Destination $trace
    if ($new.Count -gt 1) { Log "also made (left on the desktop): $(($new | Select-Object -Skip 1 | ForEach-Object Name) -join ', ')" }
}

# An apitrace capture finished: recompressed with -Repack, its frames listed
# into frames.txt beside it. The frame call numbers.
function Finish-Apitrace($at, $trace, $out) {
    if (-not (Test-Path -LiteralPath $trace)) { throw "no trace was written" }
    if ($Repack) {
        Log "repacking with zstd"
        $code = Run-Native (Join-Path $at 'bin\apitrace.exe') "repack --zstd `"$trace`" `"$trace.zstd`"" (Join-Path $out 'repack.txt')
        if ($code -eq 0) { Move-Item -Force -LiteralPath "$trace.zstd" -Destination $trace } else { Log "repack exited ${code}: kept the snappy trace" }
        Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $out 'repack.txt')
    }
    Log "listing frames"
    $dump = Join-Path $out 'frames-dump.txt'
    $code = Run-Native (Join-Path $at 'bin\apitrace.exe') "dump --calls=frame --arg-names=no --call-nos=yes `"$trace`"" $dump
    if ($code -ne 0) { throw "apitrace dump exited $code" }
    $listed = @(Get-Content -LiteralPath $dump | ForEach-Object { if ($_ -match '^\s*(\d+)\s') { $Matches[1] } })
    [IO.File]::WriteAllLines((Join-Path $out 'frames.txt'), [string[]]$listed)
    Remove-Item -Force -LiteralPath $dump
    Log "$($listed.Count) frames"
    , $listed
}

# capture.json and capture.done for one capture directory.
function Write-Result($out, $tool, $api, $mode, $count, $status) {
    $version = $null
    try { $version = (Get-Item -LiteralPath $Exe).VersionInfo.ProductVersion } catch { }
    $toolPin = $null
    $pinned = if ($tool -eq 'gfxr' -and $api -eq 'd3d12') { 'gfxreconstruct_d3d12' } elseif ($tool -eq 'gfxr') { 'gfxreconstruct' } else { $tool }
    try { $toolPin = Get-Content -Raw (Join-Path (Tool-Dir $pinned) 'tool.json') | ConvertFrom-Json } catch { }
    $info = [ordered]@{
        tool = $(if ($tool -eq 'gfxr') { 'gfxreconstruct' } else { $tool }); tool_version = $(if ($toolPin) { $toolPin.version } else { $null })
        api = $api
        arch = $arch; mode = $mode; exe = $Exe; arguments = $Arguments; package = $Package; app_id = $AppId; steam_app = $SteamApp
        process = $Process; seconds = $Seconds; started = $started; finished = (Get-Date).ToUniversalTime().ToString('o')
        app_version = $version; windows = (Get-CimInstance Win32_OperatingSystem).Version
        adapters = @(Get-CimInstance Win32_VideoController | ForEach-Object { "$($_.Name) ($($_.DriverVersion))" })
        frames = $count; status = $status
    }
    $info | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $out 'capture.json')
    Set-Content -LiteralPath (Join-Path $out 'capture.done') -Value $status
}

# ------------------------------------------------------------------ probe
if ($Tool -eq 'probe') {
    if (-not $Process) { throw "-Tool probe needs -Process" }
    $pattern = '^(d3d8|d3d9|d3d10|d3d10_1|d3d11|d3d12|d3d12core|dxgi|ddraw|d2d1|dcomp|opengl32|vulkan-1|opencl|nvcuda|amdhip64|wpfgfx.*|d3dcompiler_\d+|av\w+|amdxc64|igd\w+|nvoglv64|atio6axx|windows\.graphics\.capture|graphicscapture)\.dll$'
    Get-Process -Name $Process | ForEach-Object {
        "$($_.ProcessName) (pid $($_.Id))"
        $_.Modules | Where-Object { $_.ModuleName -match $pattern } | Sort-Object ModuleName | ForEach-Object { "  $($_.ModuleName)  $($_.FileName)" }
    }
    exit 0
}

if (-not $Out) { throw "-Out is required" }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$script:LogFile = Join-Path $Out 'capture-log.txt'
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $Out 'capture.done')
if ($GlOut) {
    if ($Tool -ne 'clintercept') { throw "-GlOut goes with -Tool clintercept" }
    New-Item -ItemType Directory -Force -Path $GlOut | Out-Null
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $GlOut 'capture.done'), (Join-Path $GlOut 'capture.trace')
}
$started = (Get-Date).ToUniversalTime().ToString('o')
$script:cleanup = @()
$status = 'ok'
$glStatus = 'ok'
$frames = $null
$glFrames = $null
try {
    if ($Package) {
        $pkg = Get-AppxPackage | Where-Object { $_.PackageFamilyName -eq $Package } | Select-Object -First 1
        if (-not $pkg) { throw "no package $Package is installed" }
        [xml]$manifest = Get-Content -Raw -LiteralPath (Join-Path $pkg.InstallLocation 'AppxManifest.xml')
        $apps = @($manifest.Package.Applications.Application)
        $app = if ($AppId) { $apps | Where-Object { $_.Id -eq $AppId } | Select-Object -First 1 } else { $apps[0] }
        if (-not $app) { throw "$Package has no application $AppId" }
        $AppId = $app.Id
        if (-not $Exe) { $Exe = Join-Path $pkg.InstallLocation $app.Executable }
    }
    if (-not $Exe -or -not (Test-Path -LiteralPath $Exe)) { throw "no executable: -Exe '$Exe'" }
    if (-not $Process) { $Process = [IO.Path]::GetFileNameWithoutExtension($Exe) }
    $arch = Arch-Of $Exe
    $exeDir = Split-Path -Parent $Exe
    Log "capturing $Exe ($arch) with $Tool$(if ($GlOut) { ' and apitrace (gl, wrappers)' })"
    foreach ($path in @($ClearFirst.Split(';') | Where-Object { $_.Trim() })) {
        $path = [Environment]::ExpandEnvironmentVariables($path.Trim())
        if (Test-Path -LiteralPath $path) { Remove-Item -Recurse -Force -LiteralPath $path; Log "deleted $path" }
    }

    if ($Tool -eq 'apitrace') {
        $at = Join-Path (Tool-Dir 'apitrace') $arch
        $trace = Join-Path $Out 'capture.trace'
        Remove-Item -Force -ErrorAction SilentlyContinue $trace
        # Injected, a Steam game started directly with a steam_appid.txt: the
        # wrapper DLLs were never loaded by Half-Life 2 launched through Steam.
        if ($Mode -eq 'auto') { $Mode = 'inject' }
        if ($Mode -eq 'inject') {
            if ($SteamApp -and -not (Test-Path (Join-Path $exeDir 'steam_appid.txt'))) {
                Set-Content -LiteralPath (Join-Path $exeDir 'steam_appid.txt') -Value "$SteamApp" -NoNewline
                $script:cleanup += (Join-Path $exeDir 'steam_appid.txt')
            }
            $launcher = Launch (Join-Path $at 'bin\apitrace.exe') "trace -a $Api -o `"$trace`" `"$Exe`" $Arguments" $exeDir
            Wait-Target $Process $launcher
        } elseif ($Mode -eq 'wrappers') {
            $before = Place-Wrappers $at $Api $exeDir
            if ($SteamApp) { Launch (Join-Path $Steam 'steam.exe') "-applaunch $SteamApp $Arguments" $Steam | Out-Null }
            else { Launch $Exe $Arguments $exeDir | Out-Null }
            Wait-Target $Process $null
            Collect-Wrapped $before $trace
        } else { throw "apitrace has no mode $Mode" }
        $frames = Finish-Apitrace $at $trace $Out
    } elseif ($Tool -eq 'clintercept') {
        $cli = Join-Path (Tool-Dir 'clintercept') $arch
        $dumpDir = Join-Path $Out 'cli'
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $dumpDir, (Join-Path $Out 'Replay')
        $key = 'HKCU:\Software\INTEL\IGFX\CLIntercept'
        $hadKey = Test-Path $key
        # New-Item -Force on an existing key empties it: controls set there
        # by hand (CallLogging, to see what an application calls) stay.
        if (-not $hadKey) { New-Item -Force -Path $key | Out-Null }
        # Kernels with their buffers (CaptureReplay), and every program's
        # source, options and build log (DumpProgramSource...): a program
        # that does not build never reaches an enqueue to be captured.
        $controls = [ordered]@{ CaptureReplay = 1; CaptureReplayUniqueKernels = 1; CaptureReplayNumKernelEnqueuesCapture = $ClKernels;
                                InitializeBuffers = 1; LogToFile = 1; ErrorLogging = 1; BuildLogging = 1;
                                DumpProgramSource = 1; DumpProgramBuildLogs = 1; OmitProgramNumber = 1 }
        foreach ($name in $controls.Keys) { New-ItemProperty -Force -Path $key -Name $name -PropertyType DWord -Value $controls[$name] | Out-Null }
        New-ItemProperty -Force -Path $key -Name 'DumpDir' -PropertyType String -Value $dumpDir | Out-Null
        New-ItemProperty -Force -Path $key -Name 'AppendBuildOptions' -PropertyType String -Value '-cl-kernel-arg-info' | Out-Null
        if ($ClKernelName) { New-ItemProperty -Force -Path $key -Name 'CaptureReplayKernelName' -PropertyType String -Value $ClKernelName | Out-Null }
        if ($GlOut) {
            $glAt = Join-Path (Tool-Dir 'apitrace') $arch
            $glBefore = Place-Wrappers $glAt 'gl' $exeDir
            Log "apitrace's opengl32.dll is beside $Exe until the capture ends"
        }
        try {
            if ($Mode -eq 'auto') { $Mode = if ($Package) { 'system' } else { 'local' } }
            $system = Join-Path $env:windir $(if ($arch -eq 'x86') { 'SysWOW64' } else { 'System32' })
            if ($Mode -eq 'system') {
                $real = Join-Path $system 'real_OpenCL.dll'
                if (Test-Path -LiteralPath $real) { throw "$real exists: a capture before this one did not put OpenCL.dll back" }
                Rename-Item -LiteralPath (Join-Path $system 'OpenCL.dll') -NewName 'real_OpenCL.dll'
                Copy-Item -LiteralPath (Join-Path $cli 'OpenCL.dll') -Destination (Join-Path $system 'OpenCL.dll')
                Log "CLIntercept is $system\OpenCL.dll until the capture ends"
                try {
                    $launcher = Launch $Exe $Arguments $exeDir
                    Wait-Target $Process $launcher
                } finally {
                    Remove-Item -Force -LiteralPath (Join-Path $system 'OpenCL.dll')
                    Rename-Item -LiteralPath $real -NewName 'OpenCL.dll'
                    Log "$system\OpenCL.dll is the real one again"
                }
            } elseif ($Mode -eq 'local') {
                $placed = Join-Path $exeDir 'OpenCL.dll'
                if (Test-Path -LiteralPath $placed) { throw "$placed exists already: not overwriting the application's own" }
                Copy-Item -LiteralPath (Join-Path $cli 'OpenCL.dll') -Destination $placed
                $script:cleanup += $placed
                if ($SteamApp) { Launch (Join-Path $Steam 'steam.exe') "-applaunch $SteamApp $Arguments" $Steam | Out-Null; $launcher = $null }
                else { $launcher = Launch $Exe $Arguments $exeDir }
                Wait-Target $Process $launcher
            } elseif ($Mode -eq 'cliloader') {
                $launcher = Launch (Join-Path $cli 'cliloader.exe') "`"$Exe`" $Arguments" $exeDir
                Wait-Target $Process $launcher
            } else { throw "clintercept has no mode $Mode" }
        } finally {
            if ($hadKey) { foreach ($name in @($controls.Keys) + @('DumpDir', 'AppendBuildOptions', 'CaptureReplayKernelName')) { Remove-ItemProperty -Path $key -Name $name -ErrorAction SilentlyContinue } }
            else { Remove-Item -Recurse -Force -Path $key }
        }
        $replay = Get-ChildItem -Recurse -Directory -Path $dumpDir -Filter 'Replay' -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($replay) { Move-Item -LiteralPath $replay.FullName -Destination (Join-Path $Out 'Replay') }
        $programs = @(Get-ChildItem -Recurse -File -Path $dumpDir -Include 'CLI_*_source.cl', 'CLI_*_options.txt', 'CLI_*_build_log.txt' -ErrorAction SilentlyContinue)
        if ($programs) {
            New-Item -ItemType Directory -Force -Path (Join-Path $Out 'Programs') | Out-Null
            foreach ($file in $programs) { Move-Item -Force -LiteralPath $file.FullName -Destination (Join-Path $Out 'Programs') }
        }
        Get-ChildItem -Recurse -File -Path $dumpDir -Filter 'clintercept_log.txt' | Select-Object -First 1 | ForEach-Object { Copy-Item $_.FullName (Join-Path $Out 'clintercept-log.txt') }
        if (-not $replay -and -not $programs) { throw "CLIntercept captured no kernel and no program under $dumpDir (its log: clintercept-log.txt)" }
        Remove-Item -Recurse -Force $dumpDir
        $frames = @(if ($replay) { Get-ChildItem -Directory -Path (Join-Path $Out 'Replay') -Filter 'Enqueue_*' })
        Log "$($frames.Count) kernel enqueues and $(@($programs | Where-Object Name -like '*_source.cl').Count) program sources captured"
        if ($GlOut) {
            try {
                foreach ($path in $script:cleanup) { Remove-Item -Force -ErrorAction SilentlyContinue -LiteralPath $path; Log "removed $path" }
                $script:cleanup = @()
                $glTrace = Join-Path $GlOut 'capture.trace'
                Collect-Wrapped $glBefore $glTrace
                $glFrames = Finish-Apitrace $glAt $glTrace $GlOut
            } catch {
                $glStatus = "failed: $($_.ToString())"
                Log "gl $glStatus"
            }
        }
    } elseif ($Tool -eq 'gfxr') {
        if ($arch -ne 'x64') { throw "the pinned GFXReconstruct builds are x64 only" }
        $capture = Join-Path $Out 'capture.gfxr'
        Remove-Item -Force -ErrorAction SilentlyContinue $capture
        $env:GFXRECON_CAPTURE_FILE = $capture
        $env:GFXRECON_CAPTURE_FILE_TIMESTAMP = 'false'
        if ($GfxrFrames) { $env:GFXRECON_CAPTURE_FRAMES = $GfxrFrames }
        # A hotkey (F12...) that starts a trimmed capture of -GfxrTriggerFrames
        # frames: the scene, reached by scripted input, without what led to it.
        if ($GfxrTrigger) {
            $env:GFXRECON_CAPTURE_TRIGGER = $GfxrTrigger
            if ($GfxrTriggerFrames -gt 0) { $env:GFXRECON_CAPTURE_TRIGGER_FRAMES = "$GfxrTriggerFrames" }
        }
        if ($Api -eq 'd3d12') {
            # D3D12 is captured by DLL substitution: the application loads
            # GFXReconstruct's d3d12.dll and dxgi.dll from its own directory,
            # which forward to the system's through d3d12_capture.dll.
            $gfxr = Join-Path (Tool-Dir 'gfxreconstruct_d3d12') 'x64'
            $env:GFXRECON_CAPTURE_COMPRESSION_TYPE = 'ZSTD'
            foreach ($dll in @('d3d12.dll', 'dxgi.dll', 'd3d12_capture.dll')) {
                $placed = Join-Path $exeDir $dll
                if (Test-Path -LiteralPath $placed) { throw "$placed exists already: not overwriting the application's own" }
                Copy-Item -LiteralPath (Join-Path $gfxr "d3d12_capture\$dll") -Destination $placed
                $script:cleanup += $placed
            }
            if ($SteamApp -and -not (Test-Path (Join-Path $exeDir 'steam_appid.txt'))) {
                Set-Content -LiteralPath (Join-Path $exeDir 'steam_appid.txt') -Value "$SteamApp" -NoNewline
                $script:cleanup += (Join-Path $exeDir 'steam_appid.txt')
            }
        } else {
            $gfxr = Join-Path (Tool-Dir 'gfxreconstruct') 'x64'
            $env:VK_ADD_LAYER_PATH = $gfxr
            $env:VK_INSTANCE_LAYERS = 'VK_LAYER_LUNARG_gfxreconstruct'
        }
        $launcher = Launch $Exe $Arguments $exeDir
        Wait-Target $Process $launcher
        # A trimmed capture's name says how it was trimmed (capture_trim_trigger.gfxr,
        # capture_frames_100_through_200.gfxr): it is the capture.
        if (-not (Test-Path -LiteralPath $capture)) {
            $trimmed = Get-ChildItem -Path $Out -Filter 'capture_*.gfxr' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
            if ($trimmed) { Move-Item -LiteralPath $trimmed.FullName -Destination $capture; Log "the trimmed capture $($trimmed.Name) is capture.gfxr" }
        }
        if (-not (Test-Path -LiteralPath $capture)) { throw "no capture was written: the layer was not loaded" }
        [void](Run-Native (Join-Path $gfxr 'gfxrecon-info.exe') "`"$capture`"" (Join-Path $Out 'gfxrecon-info.txt'))
    }
} catch {
    $status = "failed: $($_.ToString())"
    Log $status
} finally {
    foreach ($path in $script:cleanup) { Remove-Item -Force -ErrorAction SilentlyContinue -LiteralPath $path; Log "removed $path" }
}

$api = $(if ($Tool -eq 'clintercept') { 'opencl' } elseif ($Tool -eq 'gfxr') { $(if ($Api -eq 'd3d12') { 'd3d12' } else { 'vulkan' }) } else { $Api })
Write-Result $Out $Tool $api $Mode $(if ($frames) { $frames.Count } else { 0 }) $status
if ($GlOut) {
    if ($status -ne 'ok' -and $glStatus -eq 'ok') { $glStatus = "failed: the run failed ($status)" }
    Write-Result $GlOut 'apitrace' 'gl' 'wrappers' $(if ($glFrames) { $glFrames.Count } else { 0 }) $glStatus
    Copy-Item -Force -ErrorAction SilentlyContinue -LiteralPath (Join-Path $Out 'capture-log.txt') -Destination (Join-Path $GlOut 'capture-log.txt')
    Log "gl capture $glStatus"
}
Log "capture $status"
if ($status -ne 'ok') { exit 1 }
