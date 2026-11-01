# Fewer copies of a window's pixels

A study of [window-buffer-flow.md](window-buffer-flow.md), whose pass names (T1, F1, D2 and so on) this note uses. Nothing here has been built or measured. No per-pass cost is known yet for T1, T3 or T7 on any window or GPU, so savings are stated as passes removed, not as time. The order below is a recommendation to test, and a measurement can reorder it.

## What the document shows

**A window's pixels are moved 4 to 7 times, and one more whole-monitor pass runs on top.**

| Path | Whole-window passes | Whole-monitor passes |
|---|---|---|
| GDI | T1, T3, T5, T7 | T2 |
| D3D12 flip | T1, T3, T5, T7 | T2 |
| D3D11 flip | F1, T1, T3, T5, T7 | T2 |
| D3D9 or D3D11 blt | D2 or B1, D3, T1, T3, T5, T7 | T2 |
| OpenGL or Vulkan | two chain passes, F1, T1, T3, T5, T7 | T2 |

Five things explain most of that:

1. **Two passes serve a desktop nobody sees.** T1 draws the window into a primary that no one on the host looks at, and T2 then copies that whole primary for a reader that LSW's default VM does not have.
2. **T5 decouples nothing.** The pool buffer stays held for as long as the slot it was copied into. T5 exists because the host needs a surface id and a per-frame count, not because of timing.
3. **F1 is a D3D11 habit.** D3D12 presents the same kind of buffer with no copy, ordering DXVK's queue after the presenting queue with a shared fence.
4. **Damage survives one hop.** Only `DxgkDdiPresent` uses it. Every other pass is whole-surface, so changing one cell in a spreadsheet moves the whole window several times.
5. **Nothing stops at the end of the line.** remotewin has no `wl_surface.frame` and ignores `suspended`, so a covered mirror keeps every pass running.

## Removing passes

**R1. Run T2 only when something reads it.** Readers are `export_image` and `compose_planes`, reached by a GL console or an export `head` request, and `presented_texture` already falls back to the live texture. Gate on a reader existing rather than on the VM's configuration, because a full-screen application's primary may be read by DWM. Removes one whole-monitor pass per DWM frame, whatever the window's size, so it helps a small window most. Small and low risk. First confirm the `D3d11HostDevice::show` rate equals DWM's frames: that DWM's buffers are primaries rests on reverse-engineering notes.

**R2. Stop capturing what cannot be seen.** Act on `configure.suspended`, and when the guest's rate is above the output's, raise `MinUpdateInterval` to the output's period rather than 0. Removes T3, T5 and T7 for a covered mirror, and the excess frames when the guest outruns the output. This matters more now that the guest can run at the monitor's rate on one output while a window sits on a slower one. Small to medium. Must not delay the commits `DisplayLock` times.

**R3. Drop F1 for D3D11 flip windows, as D3D12 already has.** Use the shared-fence ordering of `on_PresentResource12`. DXGI does not hand a flip buffer back to the application until it is released, so the copy guards only against ordering on our side. DirectComposition surfaces are already excluded, and primaries keep their copy (Subnautica and the Steam overlay draw into those). The historic bug was DWM reading a buffer the application drew into next, so the fence must be shown to prevent it before the copy goes. Medium risk. Removes one window pass for every D3D11 flip window, and for OpenGL and Vulkan too, since they end in a D3D11 chain.

**R4. Export WGC's pool buffers and remove T5.** Removes a pass and a batch, on every path. Three things block it:
- **The id.** `surface_id_reader` expects a legacy shared handle. Pool buffers are created through our own driver, which knows them, so a driver query can answer. Whether WGC's textures allow this is open.
- **The count.** The host waits for `copies >= n`, and only copies count. But D3D11 sessions share one host queue, and `FrameArrived` means DWM's fence has signalled, so a sync taken after it already orders behind DWM's render. The count looks unnecessary here, but that needs proving.
- **The buffers.** The host keeps one frame between commits and another may be in flight, while DWM renders only into a free buffer, so the pool must grow past 3. It is 3 today because with 2 about one composition in six was lost.

Margin-reserved windows keep the slot path, as they go as pixels anyway. Medium to high risk; the biggest single gain that applies to every window.

**R5. Vulkan swap chain images as chain buffers.** The two chain passes exist to get from the application's image to a DXGI buffer. A D3D12 buffer already reaches DWM as a DXVK view over its memory, and the same route would remove V1 and V2. OpenGL keeps GL3, because its rows are bottom-up, unless the flip moves to the compositing side. Medium.

## Shrinking passes that stay

**S1. Carry damage from WGC to KWin.** Request `DirtyRegionMode`, copy only dirty rows into the slot (accumulated across its 3 slots, or unnecessary after R4), and report them through `damage_buffer`, which limits T7. Nothing for a game, but most of T5 and T7 for ordinary applications, which is the case that started this work. Medium. Availability on the guest's Windows build is open, and whether T3 itself is limited to the dirty part is open too.

**S2. Mark mirrors opaque.** A window's opaque region is empty today, on an ARGB8888 buffer, so KWin has to blend it and paint what lies beneath, and direct scan-out is probably off. Monitor mirrors can always be opaque. For a window, Windows 11's rounded corners are real transparency, so declare the rectangle less its corners and exclude layered windows; a window wrongly called opaque draws black. A few lines for monitors, small for windows. Confirm with KWin's GPU time.

**S3. Scan-out-capable slots.** Modifiers are filtered for draw, blend, sample and copy, but not for scan-out. Use dma-buf feedback when a mirror covers its output. Only useful once S2 makes such a mirror eligible.

## Taking the surface instead of capturing it

Every surface already lives on the host, and the export channel hands one to a consumer by id. The driver knows no window handles, so what capture really supplies is which surfaces make up a window, and the composite of them.

`DwmGetDxSharedSurface`, undocumented, answers the first part in the guest. Probed on this guest (Windows build 26200, under avgpu) on 2026-11-01:

| Window | Class | Answer |
|---|---|---|
| Excel | `XLMAIN` | a handle (0x40001482), format 87 (BGRA8), flags 0x8, an update id |
| Character Map | `#32770` | a handle (0x40001442), format 87, flags 0x8, an update id |
| Settings | `ApplicationFrameWindow` | refused, 0xC026200B |
| Shell Experience Host | `Windows.UI.Core.CoreWindow` | refused, 0xC026200B |
| Program Manager | `Progman` | refused, 0xC026200B |

The refusal reads as `GRAPHICS_PRESENT_REDIRECTION_DISABLED`: those windows have no redirection surface. Only the handle was asked for. Whether the handle is one `surface_id_reader` can read, whether the export channel will hand that id out, and whether its pixels are the whole window are all untested.

If they hold, a classic window can skip T3 and T5, since KWin reads the redirection surface directly. GDI would then be drawn once and composed once, plus T1 while the window is visible in the guest. What stands in the way:

- **Tearing.** GDI and the present engine write those pages while KWin reads them, with no fence between them. The flow document leaves the same question open for DWM's read (§7.3, item 5).
- **Memory.** A lent surface is linear, in system memory on a dGPU, so KWin samples it over PCIe. Whether KWin takes that buffer needs checking.
- **What the surface lacks.** Anything DWM composes on top: DirectComposition visuals, a flip-model child, the shadow and the rounded corners. A window with any of those still needs capture, and telling them apart is part of this work.
- **Knowing when it changed.** The update id looks like the signal, but what moves it is unknown. Polling it per window costs a call, where capture only wakes on change.
- **Stability.** The call is undocumented, so WGC must stay as the fallback.

Modern windows have no single surface to take: what they show exists only once DWM has composed it, so they stay on capture.

## Larger changes, after those

**L1. Attach a flip window's buffer directly.** A flip buffer is already a tiled, exportable host image. Attach it as a subsurface over the client area and let capture carry only the frame around it, at the rate that changes. Removes T3 and T5 for the client area, and T7 if KWin gives it an overlay plane. Needs clear eligibility: one swap chain covering the client, no child windows, not layered. Ordinary applications built on DirectComposition do not qualify, so this is for games and video.

**L2. Avoid T1.** T1 is required by Windows, but a mirrored window's primary is never seen. Two leads: `planes()` already reports overlay planes when the device composes, and a plane bypasses T1 if DWM uses one while capturing; and whether T1 runs at all for a window covered or off-screen in the guest. Both are open questions in the flow document. Do not try to move or cloak windows without testing it, because capture of a cloaked window may stop.

**L3. Blt windows into VRAM.** D3 writes to system memory, and T1 and T3 read it back over PCIe. docs/stale-blt-frames.md describes a host VRAM texture for DWM, which has to merge with GDI's writes. Large and risky. A smaller lead is D2: the engine copies on the writer's queue, so staging could be skipped when the kernel's rectangles arrive before the application's next batch. Worth measuring how often they do.

## Where this could end

Passes of a whole window, on one GPU, before S1's damage is counted:

| Path | Today | After R1 to R5 | With L1 |
|---|---|---|---|
| GDI | 4 + monitor | 3 (T1, T3, T7) | 3 |
| D3D12 flip | 4 + monitor | 3 | 2 (T1, T7) |
| D3D11 flip | 5 + monitor | 3 | 2 |
| Vulkan | 7 + monitor | 3 | 2 |
| OpenGL | 7 + monitor | 4 | 3 |
| D3D9 or D3D11 blt | 6 + monitor | 5 (D2/B1, D3, T1, T3, T7) | 5 |

T7 is zero where KWin scans the buffer out, and T1 is zero if L2 works out.

## What to measure first

One sweep answers most of the open questions above: guest rates of 60, 120, 180, 240 and 360 Hz, idle and under load, with a small and a maximized animating window, recording GPU time per process from fdinfo beside `gpu_busy_percent` and the GPU clock.

- **KWin's share** settles S2 and S3.
- **The difference between a small and a maximized window** is T2's whole-monitor floor, which settles R1.
- **`gpu_busy_percent` against fdinfo at peak clock** says whether the "92% busy" figure in frame-pacing.md is work or low clocks. If it is low clocks, none of these passes is what limits 360 Hz.
