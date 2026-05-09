# VM Manager

The user-facing host application. Two phases of UX:

1. **First-run wizard**: download Windows ISO, create the VM, run an unattended install, install the LSW guest payload (ICD, DXVK, VKD3D-Proton, guest agent), and verify everything works.
2. **Manager UI**: shown for any subsequent run, or whenever the wizard finishes. Lists VMs, shows live stats, lists installed apps in each VM, launches them.

## Toolkit decision

Two acceptable choices. Pick one and don't mix.

- **Qt 6 / QML / Qt Widgets**: rich, batteries-included (charts, MVC, system tray). Heavier dep but fastest to ship a polished UI. Licensing is LGPLv3 if dynamically linked; fine.
- **GTK4 + libadwaita**: native GNOME look, lighter dep tree, harder to do real-time graphs without third-party widgets. Fits the "ships with the desktop" feeling on GNOME.

**Recommendation: Qt 6 Widgets** for v1. Cross-DE, stable APIs, good Wayland support, good integration with QProcess for libvirt/virsh tooling. Revisit GTK4 if a GNOME-native variant is wanted later.

## Module layout

```
src/manager/
├── main.cpp
├── app.{h,cpp}                  # QApplication subclass, single-instance lock
├── config/
│   ├── settings.{h,cpp}         # XDG paths, persisted state
│   └── vm_state.{h,cpp}         # serialized list of known VMs
├── iso/
│   ├── catalog.{h,cpp}          # known ISO sources, hashes
│   ├── downloader.{h,cpp}       # async HTTP download with resume
│   └── verifier.{h,cpp}         # SHA-256 / signature check
├── vm/
│   ├── libvirt_conn.{h,cpp}     # connection to qemu:///session (no root)
│   ├── domain_xml.{h,cpp}       # build libvirt XML from a template
│   ├── lifecycle.{h,cpp}        # create / start / shutdown / destroy
│   ├── monitor.{h,cpp}          # poll CPU / memory / disk / net stats
│   └── vsock.{h,cpp}            # talks to lsw-guest-agent inside the VM
├── install/
│   ├── autounattend.{h,cpp}     # generate Windows answer file
│   ├── payload_iso.{h,cpp}      # build the secondary ISO with our installers
│   └── post_install.{h,cpp}     # tracks "is the guest agent talking yet?"
├── apps/
│   ├── catalog.{h,cpp}          # list of installed Windows apps from guest agent
│   └── launcher.{h,cpp}         # asks guest agent to start an app
├── ui/
│   ├── wizard/
│   │   ├── welcome_page.{h,cpp,ui}
│   │   ├── license_page.{h,cpp,ui}        # display Windows EULA, user accepts
│   │   ├── iso_choice_page.{h,cpp,ui}     # download or pick existing
│   │   ├── download_page.{h,cpp,ui}       # progress, hash check
│   │   ├── vm_size_page.{h,cpp,ui}        # CPU / RAM / disk sliders
│   │   ├── install_page.{h,cpp,ui}        # live console viewer, progress
│   │   ├── payload_page.{h,cpp,ui}        # DXVK/VKD3D/ICD install progress
│   │   └── done_page.{h,cpp,ui}
│   ├── manager/
│   │   ├── main_window.{h,cpp,ui}
│   │   ├── vm_list_panel.{h,cpp,ui}       # left side: VM tiles with thumbnails
│   │   ├── vm_detail_panel.{h,cpp,ui}     # right side: stats, app list
│   │   ├── stats_widget.{h,cpp}           # CPU/RAM/VRAM gauges
│   │   ├── thumbnail.{h,cpp}              # periodic VM screenshot
│   │   └── app_list.{h,cpp,ui}
│   ├── prefs_dialog.{h,cpp,ui}
│   └── theme.{h,cpp}                      # light/dark, dpi, accent
└── translations/
```

## ISO acquisition

Microsoft does not host stable, scriptable, license-free Windows download URLs. Practical options:

1. **fido**-style PowerShell scraping of the official "Download Windows" page. Brittle but sourced from MS.
2. **mido** / **massgrave** approach: query MS's `vlscappdl.microsoft.com` / `software-download.microsoft.com` endpoints. Less brittle than scraping, but undocumented.
3. **User provides ISO**: always supported as a fallback.

For v1 implement (3) as the always-works path, then layer (2) on top with feature detection — if the auto-download endpoint changes, fall back to a clear "please download Windows from microsoft.com and pick the ISO" dialog. Hash-verify what we download against a small, signed manifest we ship.

The wizard surfaces the EULA in the license page before any install begins.

### Editions

- **Windows 11 Pro / Home**: the modern default. Pro is preferable because it allows easier remote management and Hyper-V-related features.
- **Windows 10 LTSC**: lighter, optional advanced choice — present in a "more options" expander.

## VM provisioning (libvirt + QEMU/KVM)

Use `qemu:///session` (rootless) so we never need privilege escalation. Storage in `~/.local/share/lsw/disks/`. Disks are qcow2 with a sensible default size (60 GB, sparse).

Domain XML template highlights:

```xml
<domain type='kvm'>
  <name>lsw-vm-{{uuid}}</name>
  <memory unit='MiB'>{{ram_mib}}</memory>
  <vcpu>{{cpus}}</vcpu>
  <os>
    <type arch='x86_64' machine='q35'>hvm</type>
    <loader readonly='yes' type='pflash'>{{ovmf_code}}</loader>
    <nvram template='{{ovmf_vars_template}}'>{{nvram_path}}</nvram>
    <boot dev='cdrom'/>
    <boot dev='hd'/>
  </os>
  <features>
    <acpi/><apic/>
    <hyperv mode='custom'>
      <relaxed state='on'/>
      <vapic state='on'/>
      <spinlocks state='on' retries='8191'/>
      <vpindex state='on'/>
      <synic state='on'/>
      <stimer state='on'/>
    </hyperv>
  </features>
  <cpu mode='host-passthrough' check='none'/>
  <clock offset='localtime'/>
  <devices>
    <emulator>/usr/bin/qemu-system-x86_64</emulator>
    <disk type='file' device='disk'>
      <driver name='qemu' type='qcow2' discard='unmap'/>
      <source file='{{disk_path}}'/>
      <target dev='vda' bus='virtio'/>
    </disk>
    <disk type='file' device='cdrom'>
      <source file='{{windows_iso}}'/>
      <target dev='sda' bus='sata'/>
      <readonly/>
    </disk>
    <disk type='file' device='cdrom'>
      <source file='{{payload_iso}}'/>
      <target dev='sdb' bus='sata'/>
      <readonly/>
    </disk>
    <disk type='file' device='cdrom'>
      <source file='{{virtio_iso}}'/>
      <target dev='sdc' bus='sata'/>
      <readonly/>
    </disk>
    <interface type='user'>
      <model type='virtio'/>
    </interface>
    <vsock model='virtio'>
      <cid auto='no' address='{{cid}}'/>
    </vsock>
    <video><model type='virtio' heads='1' primary='yes'/></video>
    <graphics type='spice' autoport='yes'>
      <listen type='address'/>
    </graphics>
    <!-- Optional shared memory for Phase 2 -->
    <!-- <shmem name='lsw-{{uuid}}'><size unit='M'>4096</size></shmem> -->
  </devices>
</domain>
```

Notes:
- OVMF firmware is required for a modern Windows install (Win11 demands UEFI + Secure Boot capable + TPM 2.0). Add `<tpm model='tpm-crb'><backend type='emulator' version='2.0'/></tpm>`.
- The `<vsock>` element is the link the render-server and ICD use.
- The Spice display is for the wizard's "watch the install happen" view; it gets hidden once the manager's per-app windows take over rendering.
- The payload ISO contains our installers and the autounattend.xml.

## Unattended install

Windows reads `autounattend.xml` from the root of any attached writable media on boot. We generate one at wizard time, burn a tiny ISO containing it plus our payload, and attach it.

Key sections of `autounattend.xml`:

- `windowsPE` pass: keyboard / locale / accept EULA / wipe disk / partition / image select.
- `specialize` pass: computer name, timezone.
- `oobeSystem` pass: skip the OOBE, create local user, autologon for the first 3 reboots, run a `FirstLogonCommand` that executes our post-install script from the payload ISO.
- `Microsoft-Windows-Shell-Setup` for the local user.
- For Win11: bypass hardware checks via `LabConfig` or by injecting registry values during `windowsPE`.

The post-install script:

1. Installs `virtio-win` drivers from the virtio ISO if they aren't already present.
2. Runs the LSW MSI which lays down the ICD DLLs and registers them.
3. Installs DXVK by copying the d3d9/d3d10/d3d11/dxgi DLLs to `%SystemRoot%\System32` (and `SysWOW64`). Or per-app via `setup_dxvk install` to a global path.
4. Installs VKD3D-Proton equivalents (`d3d12.dll`, `d3d12core.dll`).
5. Installs the LSW guest agent as a service that auto-starts.
6. Pings the host over vsock once it's up; manager UI sees this and advances to the "Done" page.

## Guest agent

A small Windows service that:
- Talks to the host on a separate vsock service GUID from the rendering one.
- Reports OS version, free RAM, disk, hostname, IP.
- Lists Start menu apps (`Get-StartApps` PowerShell, plus `%ProgramData%\Microsoft\Windows\Start Menu\Programs` and per-user equivalents). Returns `{ name, exe_path, icon_png }`.
- Launches apps on request (`CreateProcessW` from the user's session, not the service's session — uses `WTSQueryUserToken` + `CreateProcessAsUser`).
- Handles auto-update of itself, the ICD, DXVK, and VKD3D when newer versions are pushed from the host.
- Captures screenshots on request for the thumbnail in the manager UI.

The agent is also the path through which the wizard knows post-install succeeded.

## Manager UI

```
┌──────────────────────────────────────────────────────────────────┐
│  LSW                                              ☰  Preferences │
├──────────────┬───────────────────────────────────────────────────┤
│ ▢ Win11-Main │  Win11-Main                          [▶ Start]    │
│ ▢ Win10-Test │  ┌───────────────┐  CPU   ████░░░░ 41%            │
│              │  │  thumbnail    │  RAM   ███░░░░░ 7.2 / 16 GB    │
│  + New VM    │  │  (live)       │  VRAM  ██░░░░░░ 2.1 / 8 GB     │
│              │  └───────────────┘  Disk  ███████░ 38 / 60 GB     │
│              │                                                   │
│              │  Installed apps                                   │
│              │  ┌─────┬─────┬─────┐                              │
│              │  │ App │ App │ App │                              │
│              │  │  1  │  2  │  3  │                              │
│              │  └─────┴─────┴─────┘                              │
│              │  [+ Install from .exe / msi]                      │
└──────────────┴───────────────────────────────────────────────────┘
```

- VM list on the left, plus a "create new VM" entry (re-runs the wizard).
- Detail panel on the right: thumbnail (refreshed every 2s by guest agent screenshot), gauges, app grid.
- Apps are launchable — click runs `lsw-guest-agent.exe launch <app-id>` via vsock; the app's Vulkan output appears as a host window via the render-server.

Stats sources:
- CPU: libvirt `virDomainGetCPUStats`.
- RAM: libvirt `virDomainMemoryStats` (requires balloon driver, which the autoinstall sets up).
- VRAM: render-server's exporter — for each connection from this VM, sum its `VkDeviceMemory` allocations.
- Disk: stat the qcow2 file size + libvirt block stats.
- Network: libvirt block + interface stats.

## Single-instance, system tray

The manager registers a unique D-Bus name (`org.lsw.Manager`) and a system tray icon. Closing the window minimizes; quitting requires explicit Quit. Apps launched from the tray bypass the main window entirely.

## Permissions

- Needs membership in `kvm` and `libvirt` groups. The wizard checks and offers to run a one-time `usermod` (with pkexec for the privileged step).
- Needs `vhost_vsock` kernel module loaded. Wizard checks; suggests `modprobe vhost_vsock` and adding it to `/etc/modules-load.d/`.

## State on disk

```
~/.local/share/lsw/
├── disks/                 qcow2 files
├── isos/                  cached Windows + payload + virtio ISOs
├── nvram/                 per-VM OVMF_VARS files
└── vms.json               manifest of registered VMs

~/.config/lsw/
└── manager.toml           UI preferences, last-used paths

~/.cache/lsw/
└── thumbnails/            per-VM screenshots
```
