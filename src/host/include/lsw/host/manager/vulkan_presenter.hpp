#pragma once

// The second ``Presenter`` implementation — declared, deliberately not
// implemented yet. This file records where the seam goes and, now, what
// is genuinely left.
//
// WHY IN-PROCESS IS NOT A PREFERENCE
//   A replay can only present into a window ITS OWN PROCESS owns.
//   Presenting into a window belonging to another process succeeds at
//   every Vulkan call and paints nothing — a lesson already paid for
//   once here. So a mirror handing over its ``winId()`` only works if
//   the replay runs inside the manager.
//
// WHAT IS NOW DONE
//   * ``lsw_render`` is a library, and its composition root is
//     ``lsw::render::RenderService`` — a SurfaceManager, the dispatch
//     backend and the handler chain, constructible from anywhere. The
//     standalone ``lsw-render-server`` is a thin main() over it, and
//     the manager can build the same object on a thread.
//   * ``SurfaceManager::create_from_xcb`` already accepts a foreign
//     window id, which is the call a mirror would make.
//   * The guest side that feeds it exists: a Windows ICD
//     (``lsw_icd.dll``), a shared-memory command stream, and command
//     batching.
//
// WHAT IS LEFT, AND WHY IT IS NOT WRITTEN HERE YET
//   The remaining work is the mirror integration itself: a mirror picks
//   a pixel source, ``VulkanPresenter`` binds the replay target to
//   ``QWidget::winId()``, ``has_content`` becomes "a swapchain exists",
//   ``paint`` becomes a no-op (the GPU already composited into the
//   surface), and resize has to re-create the swapchain.
//
//   None of that can be exercised on this machine. A mirror exists only
//   for a guest window, which requires a running VM; and the pixels it
//   would show require a guest application producing a command stream,
//   which is plan step E3. Writing it blind would mean a large,
//   untested change to the manager's window handling whose first real
//   run is also its first test — against the UI the user actually
//   depends on.
//
//   So the enabling refactor landed and the integration waits for E3.
//   ``ShmemPresenter`` remains the only implementation, and this
//   interface is what keeps the two from growing further apart in the
//   meantime.

#include "lsw/host/manager/presenter.hpp"
