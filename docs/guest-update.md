# Updating LSW in Windows

What LSW puts into the Windows guest is brought to the release the
manager ships by one mechanism, which runs on its own when the two
differ and on demand from Settings. It replaces the four that grew up
separately (the agent and mirror swap at start-up, the shared-folder
upload, the Graphics page's driver offer, and nothing at all for the
loaders in System32, the service, the firewall rules and the setup
scripts), each of which updated part of the guest and none of which
recorded what the guest was left with.

## The release

`GuestBundle` (host) lists every file LSW puts into the guest, as the
manager would ship it now, each with its SHA-256 and the path it takes
in the guest:

| from (host, through its locator)       | to (guest)                                   |
| -------------------------------------- | -------------------------------------------- |
| lsw-guest-agent.exe                    | `C:\ProgramData\LSW\lsw-agent.exe`           |
| remotewin-guest.exe                    | `C:\ProgramData\LSW\remotewin-guest.exe`     |
| av-drivers/provision.ps1               | `C:\av\provision.ps1`                        |
| av-drivers/package/*                   | `C:\av\package\*` (see the driver step)      |
| av-drivers/loaders/{x64,x86}/*         | `C:\av\loaders\{x64,x86}\*`                  |
| vulkan-1.dll x64 / x86                 | `C:\av\loaders\{x64,x86}\vulkan-1.dll`       |
| guest-virtiofs/*                       | `C:\ProgramData\LSW\virtiofs\*`              |
| lsw-guest-update.ps1                   | `C:\ProgramData\LSW\lsw-guest-update.ps1`    |
| adapters.txt (made from this host)     | `C:\av\adapters.txt`                         |

The mount list for `lsw-shares.ps1 -Shares` is part of the release too: it
is carried in the manifest as a file of its own at the fixed pseudo path
`C:\ProgramData\LSW\update\shares.txt`, so a changed list is a new
release. (It is staged like the others, and also served as `shares.txt`.)
`adapters.txt` carries the Update Catalog's driver versions, which the
manager keeps per GPU (`driver-versions.json` in its data directory) and
falls back on when a lookup fails, so that a failed lookup does not change
the release.

The manifest is one line per file, `<sha256>  <guest path>`, sorted by
guest path. The release is the SHA-256 of the manifest's text: two
managers that would put the same bytes in the same places have the same
release, whatever their version numbers, and a rebuild that changes one
byte is a new release.

The guest's release is `C:\ProgramData\LSW\release.json`, written by the
update script only once every step has succeeded:

```json
{ "release": "<hex>", "version": "<x.y.z>", "applied": "<ISO 8601>", "manifest": "<the manifest's text>" }
```

`version` is the LSW project version of the manager that made the bundle,
passed to the script as `-Version`. It is not in the manifest and does not
change the release; it only lets the interface say "LSW 0.2.0" instead of a
hash. The agent reports it as `release_version` (empty when absent).

The agent reports it in `system_info` as `release` (empty when the file
is missing or unreadable), and the last run's outcome as `last_update`,
read from `C:\ProgramData\LSW\update-result.json`, which carries
`finished` (UTC, with milliseconds: `2026-11-02T10:15:30.123Z`): the host
takes a result to be this run's when `finished` differs from the one
`system_info` gave before the run started. `applied` in `release.json`
has the same form. The host's check that the agent is one of a boot after
the update is `agent_started > finished + 1 s`.

## The run

### On the host (`GuestUpdate`)

0. Shows "Preparing files" at once (every `run()` and `retry()`), asks
   `system_info` for the last result as the baseline (3 tries, 2 s apart;
   if Windows does not answer, the run fails), and builds the bundle.
1. Starts a `GuestFileServer` serving every bundle file, and
   `manifest.txt` and `shares.txt` (the drive letters and tags to mount),
   at `http://10.0.2.2:<port>/<n>`.
2. Sends one `run_script` (SYSTEM, session 0) that downloads every file
   with curl into `C:\ProgramData\LSW\update\<release>\` (first
   exiting with code 10, "An update is still running in Windows.", when
   `schtasks /Query` says the task `lsw-update` is Running, so a stage in
   use is not removed) by its guest
   path, then registers and starts the one-shot scheduled task
   `lsw-update` running `lsw-guest-update.ps1` from there, and returns.
   The task, not the agent, does the work, because the work stops the
   agent. This needs nothing an old agent lacks: `run_script` and curl
   are in every release.
3. Waits for the agent to go and come back reporting the new release,
   across the guest restart when there is one, for up to 15 minutes.
   Success is a `last_update` of this run (its `finished` differs from the
   one before the run) that is `ok` with `release == shipped release`, and,
   when it marked a restart, an agent started after it. A failed
   `last_update` of this run is checked first and is reported with its
   message and the guest log `C:\ProgramData\LSW\update.log`. The wait
   for the send script's end also gives way when the agent stops
   answering or a result of this run appears, because the task stops the
   agent. When the 15 minutes end with nothing from this run, the host
   asks a short `run_script` about `update-result.json` (a run_script's
   output does not come back, so its exit code tells: no result of this
   run, a failed one, an ok one) and says which; with none it says that
   Windows did not report the new release and a previous LSW agent may
   have been put back.

### In the guest (`lsw-guest-update.ps1 -Stage <dir> -Release <hex> [-Version <x.y.z>] [-Force] [-NoRestart]`)

The staging directory holds each file under its guest path with the
drive's colon removed (`<dir>\C\ProgramData\LSW\lsw-agent.exe` is
`C:\ProgramData\LSW\lsw-agent.exe`, `<dir>\C\av\package\avgpu.sys`
is `C:\av\package\avgpu.sys`), plus `manifest.txt` and `shares.txt`
(the value for `lsw-shares.ps1 -Shares`) at its top.

The script takes the named mutex `Global\lsw-guest-update` for its whole
run; when it cannot within 5 s it writes a failed `update-result.json`
("another update is running in Windows") and exits without touching
anything.

Every step is idempotent, logs to `update.log`, and is skipped when what
it puts in place already matches the manifest, unless `-Force`.

1. **Verify.** Every staged file's SHA-256 against `manifest.txt`. Any
   mismatch or missing file ends the run here, with nothing changed.
2. **Stop.** Every `remotewin-guest.exe`, then the `lsw-agent` service:
   its recovery actions are disabled first (`sc failure ... actions= ""`,
   so a kill is not answered by a restart mid-run), then stop, wait 30 s,
   then kill.
3. **Agent and mirror.** The current ones are copied to
   `C:\ProgramData\LSW\previous\`, the new ones copied in.
4. **Driver.** Skipped only when the marker `C:\av\installed.sha256`
   equals the manifest lines (sorted) of the files this step owns
   (`C:\av\package\*`, `provision.ps1`, `adapters.txt`) and those files
   are current; the files alone prove nothing, being put in place before an
   install that may fail. Otherwise (or `-Force`) the marker is removed,
   `provision.ps1` and the package are copied in, the package
   is signed and installed exactly as `GuestDriverInstaller` does it
   (`provision.ps1 -Stage sign`, `-Stage install -NoVerifier`, from a
   copy of the package in `C:\av\resign\package`, because signing rewrites
   the INF and signs `avgpu.sys`), and the marker is written once the
   install succeeded, so a failure leaves none.
   Marks a restart.
5. **Loaders.** `vulkan-1.dll` and `OpenCL.dll` in System32 (x64) and
   SysWOW64 (x86) are made equal to `C:\av\loaders`. A file in use is
   replaced at the next boot (`MoveFileEx` with
   `MOVEFILE_DELAY_UNTIL_REBOOT`), which marks a restart.
6. **Shared folders.** `C:\ProgramData\LSW\virtiofs` is made exactly the
   bundle's (files not in it are deleted, so an old WinFsp installer
   cannot be picked), then `lsw-shares.ps1 -Install -Shares <list>`.
   Its exit 7 marks a restart.
7. **Service and firewall.** The `lsw-agent` service's binPath, start
   type and recovery actions (always re-applied here, even when all else
   is current), and the "LSW Agent" (5102) and "LSW
   Stream" (5103) rules, are set to what setup sets.
8. **Start.** The recovery actions are re-applied if step 7 did not run.
   The agent service is started and must listen on 5102
   within 60 s (under `-NoRestart` only be running: the agent keeps its
   port closed until the wizard user's desktop is usable, which setup has
   not reached yet). If it does not, the previous agent and mirror are put
   back and started, and the run fails.
9. **Record.** `release.json` is written, then `update-result.json`
   (`{ ok, restart, steps: [...], message }`), and the staging directory
   is removed. A marked restart is done with `shutdown /r /t 5`, unless
   `-NoRestart` was given (the first install, below): it is then only
   recorded in the result (`restart: true`).

A failure in any step after 2 still runs 8, so the guest is never left
without an agent, and writes `update-result.json` with the step and its
message; `release.json` is left as it was.

## Triggers

- **Automatically**, from `GuestUpkeep`, once per guest boot: when the
  agent first answers and `system_info.release` differs from the shipped
  release. Not forced: only what differs is replaced.
- **On demand**, from Settings, "Update LSW in Windows": forced, so every
  file is replaced and every step runs, whatever the guest reports.

While a run is going, `StatusHub` reports it (the step the host is on),
and nothing else updates the guest. The MCP `redeploy_agent` tool keeps
its quick swap of the agent and mirror alone, for development.
The MCP `guest_update` tool (`force`, default true) runs this same update
and returns when it ends, with the outcome and the guest's steps; it is an
error while a run is going. `scripts/test-guest-update.sh` runs the script's
scenario tests (`src/guest/update/tests/lsw-guest-update.tests.ps1`) in the
guest, as a one-shot SYSTEM task like production.

## What the interface shows

Everything comes from `StatusHub`'s `GuestUpdateStatus` (state, step,
message, the shipped and guest releases and versions, and the guest's steps
of the run once it has ended):

- Settings, "Update LSW in Windows": a sub-line (the release Windows has,
  the step while running, or why it failed) and the forced "Update" button,
  disabled while a run is going or the agent does not answer.
- `UpdateDialog`: a modal that opens whenever a run starts, automatic or
  from Settings, and not for a check that finds Windows current. It follows
  the host's steps (Preparing files, Sending files to Windows, Updating
  Windows, Restarting Windows when the run reaches it, Done), cannot be
  closed while the run goes, and at the end lists the guest's steps
  (updated, already current, failed) with Close, and "Try again" after a
  failure.
- The status bar and the notice for a failed run, as before.

## A new host GPU

The Graphics page no longer offers to install the driver for new adapters
or to update it. A new host GPU changes `adapters.txt`, hence the release,
hence an update on the next boot, which installs the driver for it. To do
it sooner, "Update" in Settings.

## First install

A fresh guest goes through the same script, so that it ends with the same
`release.json` and its first steady boot needs no update.

On the host, `PayloadIsoStager` builds a `GuestBundle` (with the mounts of
the shares in the settings, as `GuestUpkeep`'s update would) before it
renders the answer file, which carries the bundle's release and version
(`{{release}}`, `{{version}}`). The payload ISO (volume `LSW_PAYLOAD`)
holds the bundle under `\update\` in the staging layout the script reads
(`<drive>\<path>` without the colon, plus `manifest.txt` and `shares.txt`),
so its files are byte for byte what an update would ship. Nothing of the
bundle is on the ISO anywhere else.

In the guest, Setup's specialize pass only makes `C:\ProgramData\LSW` and
its ACL. `SetupComplete.cmd` runs `LswPromote.ps1` (SYSTEM), which:

1. finds the `LSW_PAYLOAD` volume and copies its `\update` to
   `C:\ProgramData\LSW\update\<first 16 hex of the release>` (`xcopy`
   without `/K`, which drops the CD's read-only attribute);
2. runs `lsw-guest-update.ps1 -Stage <that> -Release <hex> -Version <ver>
   -NoRestart` from the copy and logs to `promote.log`. The script does
   everything an update does: agent and mirror, driver (sign, install),
   loaders, shared folders, the service and the firewall rules, and writes
   `release.json`. `-NoRestart` because Windows does not allow a restart
   from SetupComplete;
3. promotes the wizard user to Administrators and writes `promoted.flag`.

The driver's sign stage turns test signing on, which the kernel reads at
boot; `avgpu.sys` first starts on the first boot after setup, when the
manager restarts the guest into its steady definition. Nothing between
SetupComplete and that boot needs it (the install-time display is the VGA
adapter, and the agent and the post-install steps do not use the GPU).

A failed first-install update leaves no `release.json`, and nothing here
retries: the first steady boot's `GuestUpkeep` finds the guest at no
release and runs the update from the manager. `GuestUpkeep` does not run
during the install (the definition is still the installing one).
