#pragma once

#include <string>

namespace lsw::agent::capture {

// Per-app capture-method side effects. ``Wgc`` mode is no-op on
// the file system (the WGC capture pipeline lives elsewhere); only
// ``Vulkan`` mode requires DLL plumbing — we hardlink DXVK and
// VKD3D-Proton from ``C:\ProgramData\LSW\{dxvk,vkd3d}\<arch>\*.dll``
// into the application's exe folder so the loader picks them up
// instead of the native d3d11/dxgi/d3d12 DLLs.
//
// Hardlinks (``CreateHardLinkW``) are used over copies so updating
// the LSW-wide DLLs propagates to every app, and over symbolic
// links so we don't need ``SeCreateSymbolicLinkPrivilege``. Both
// files must live on the same volume — typically true since both
// LSW and most apps live on C:.
class Setup {
public:
    // Hardlink DXVK + VKD3D-Proton DLLs (matching the exe's PE
    // architecture) into the same directory as ``app_exe_path``.
    // Existing files at the target paths are removed first so we
    // can re-link cleanly. UTF-8 path. Returns false on any I/O
    // failure (already-installed files are not failures).
    [[nodiscard]] static bool enable_vulkan_for(const std::string& app_exe_path);

    // Remove any DLL hardlinks we previously dropped next to
    // ``app_exe_path``. Files that don't look like our hardlinks
    // (e.g. the app shipped its own d3d11.dll) are left alone — we
    // only delete files that match the LSW DXVK/VKD3D names.
    [[nodiscard]] static bool disable_vulkan_for(const std::string& app_exe_path);
};

}  // namespace lsw::agent::capture
