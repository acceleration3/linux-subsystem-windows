# Mirror latency: what Looking Glass teaches, and a plan

Looking Glass (gnif/LookingGlass) shows a Windows guest's display on a Linux
host with very low latency. This compares its design with how a guest window's
frames reach a host window in LSW (remotewin over accel-virt), and lays out what
to change, in order. Everything here comes from reading both codebases; none of
it is measured yet. Measuring is step 1.

## Where Looking Glass's low latency comes from

**Display and pacing**

- **The newest frame always wins.** The client skips to the newest message in
  its queue (`lgmpClientAdvanceToLast`). The host skips a frame rather than wait
  for a buffer ("skip the frame to keep up").
- **It draws the moment a frame arrives.** By default vsync is off, rendering is
  single-buffered (`EGL_SINGLE_BUFFER`), and swaps carry only the changed
  rectangles (`eglSwapBuffersWithDamage`, buffer age). Its docs warn that vsync
  and double buffering "can add presentation delay".
- **Just-in-time render (optional).** The client renders at the compositor's
  frame callback. On Wayland it sends the output's period and phase, from
  `wp_presentation` feedback, back to the producer. The producer then publishes
  each frame just ahead of the client's deadline.

**Getting frames out**

- **The shared buffer is released at once.** The client blits a shared frame
  into a private snapshot texture, fences the blit, and releases the shared
  buffer as soon as the fence passes. The producer always has a free buffer.
- **The producer never blocks on the GPU.** Before `Map` it sleeps for 0.8× the
  average copy time, then polls `Map` with "do not wait".
- **Realtime priority.** The producer runs at realtime GPU scheduling priority
  and MMCSS (Windows' Multimedia Class Scheduler Service).

**Cursor and threads**

- **The cursor is independent of frames.** It has its own queue and thread. A
  cursor move triggers a cursor-only redraw damaged to the old and new rects, and
  the client predicts movement before the guest confirms it.
- **One thread per job**: frames, cursor, render and cursor repaint.
- **Polling, not interrupts.** It polls shared memory every 1 ms. Its reasoning:
  virtual interrupts are expensive and batch high-rate pointer updates.

**Measurement**

- A per-stage latency graph: capture, post, copy, ready, hold, import, render,
  swap.

## Where LSW's pipeline waits today

1. **Two unsynchronised 60 Hz clocks.**
   - DWM and Windows.Graphics.Capture compose on the device's free-running
     60 Hz `DisplayClock` (`device/display/vblank_schedule.hpp`).
   - Nothing ever sends `DISPLAY_TIMING`, so it is never told the host's rate or
     phase.
   - The host compositor runs on its own clock.
   - Cost: 0–16.7 ms of phase loss per frame, plus beating between the clocks.
2. **The host draws twice and waits for vsync.**
   - remotewin draws each frame into a `QOpenGLWidget`'s FBO (`GpuPicture`).
   - Qt then composes that FBO into the window in a second full-window pass.
   - The swap uses Qt's defaults, and the repo sets no swap interval.
3. **Two round trips per frame, one through emulated networking.**
   - The guest sends `FrameInSlot` over TCP through slirp and an emulated e1000e
     NIC.
   - The host then sends `sync(id)` over the export socket. That takes the
     D3D11 device lock, drains DXVK and submits.
   - If the copy has not arrived yet, the host retries every 1 ms.
4. **Slots are held too long.**
   - A slot stays held until a later frame has been drawn and its fence has
     signalled. The guest often has no free slot.
   - With no free slot, the guest skips the frame and waits for a `SlotFree`
     round trip over TCP.
   - Frames also queue in Windows' capture pool (3 buffers), with no
     newest-frame-wins across the pipeline.
5. **The guest does everything on one thread.**
   - One loop does input injection, a full window enumeration, frame copies and
     cursor polling, with a 16 ms timeout. Input waits behind the rest.
   - Windows that keep a margin do a synchronous edge `Map` readback every
     frame (`PictureEdges::all_drawn`). That is a full guest-to-host GPU round
     trip.
6. **Input goes over slirp TCP.** Every key and mouse event takes the same
   emulated-network hop.

**Already better than Looking Glass:**

- Zero-copy pixels: the guest's slot texture is shared with the host as a
  dma-buf.
- Cursor position is the host compositor's own hardware cursor, so cursor
  movement has no latency. Only shape changes lag.
- Looking Glass's chunked copies and RGB24 packing solve a memory-bandwidth
  problem that zero-copy dma-bufs already avoid.

## Plan

1. **Measure each stage.**
   - Stamp each frame at: WGC arrival, slot copy submitted, copy executed on the
     host, export `sync` answered, host import, draw, swap, and presentation
     feedback.
   - Use one clock: the host's `CLOCK_MONOTONIC`, with the guest's stamps
     converted through a measured offset.
   - Show a per-stage breakdown, like Looking Glass's graph, in remotewin's
     debug overlay and its log.
2. **Lock the guest display clock to the host monitor.**
   - remotewin reads `wp_presentation` feedback for its output.
   - It sends the period and phase to the device over the export socket.
   - The device's `DisplayClock` adopts the period (the `DISPLAY_TIMING` the
     guest is never sent today). It places each blank so a composed frame lands
     just ahead of the compositor's deadline.
3. **Present without the extra pass.**
   - Best: give the slot's dma-buf to the compositor as a `wl_buffer` on a
     subsurface, using linux-dmabuf with explicit sync (`linux-drm-syncobj-v1`).
     That is zero GPU passes in our process.
   - Where scaling or colour keying needs a draw: a `QOpenGLWindow` that renders
     directly, with swap interval 0 and damage-only swaps.
4. **Take frames off the network.**
   - The device already counts copies into each slot. It pushes "slot N ready"
     with its sync fence over the export socket the moment the copy executes.
   - That removes `FrameInSlot` over TCP, the `sync` request and the 1 ms retry.
   - Move input to virtio-vsock (the agent's transport) instead of slirp TCP.
5. **Release slots at once, and keep only the newest frame.**
   - remotewin blits each slot into a private texture and frees the slot when
     that blit's fence passes, as Looking Glass's snapshot does.
   - Only the newest frame is kept, at every stage.
6. **Split the guest loop and make it non-blocking.**
   - Separate threads for input, capture and cursor.
   - Cursor shape from a WinEvent hook (`EVENT_OBJECT_NAMECHANGE` on
     `OBJID_CURSOR`) instead of polling.
   - The edge readback becomes non-blocking: use the previous frame's result, or
     sleep then poll with "do not wait".
   - MMCSS and realtime GPU scheduling priority for remotewin-guest.

Steps 2 and 3 are expected to give the most; step 1 says how much each step is
actually worth before and after.
