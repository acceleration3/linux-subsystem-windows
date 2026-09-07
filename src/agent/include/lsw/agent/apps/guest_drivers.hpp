#pragma once

#include <string>
#include <utility>
#include <vector>

namespace lsw::agent::apps {

// Puts the render server's drivers next to an application, and says
// what to put in its environment so they are the ones it finds.
//
// Windows resolves an implicitly-linked DLL from the directory of the
// running image before it looks anywhere else, and for OpenGL and
// Direct3D that search order is the ONLY hook there is: ``opengl32.dll``
// beside the exe wins over the system one, and the same file in
// ``C:\ProgramData\LSW`` is never consulted. So the drivers are a
// per-application, per-launch placement rather than something the
// wizard installs once.
//
// "The directory of the running image" is not the directory of the
// executable we launched. A portable build, an installer, an updater
// and every launcher-plus-payload arrangement start a stub which spawns
// the real application out of a subdirectory — Photoshop CS6 Portable
// puts its stub at the root of the tree and the 64-bit Photoshop.exe
// three levels down — and a driver beside the stub is on no search path
// the payload ever consults. So the whole tree under the launched image
// is staged, one architecture per directory, chosen from the
// executables actually in it: the stub's directory gets the 32-bit
// build and the payload's the 64-bit one, in the same launch.
//
// Symbolic links, not copies. A link is a few bytes, updating the
// installed driver updates every application at once, and — the reason
// that matters most here — a link is self-identifying: an application
// that ships its own ``d3d11.dll`` has a real file there, and
// ``FILE_ATTRIBUTE_REPARSE_POINT`` is how the cleanup path tells what
// LSW placed from what it must not touch. The alternative, a marker
// file listing what was staged, goes stale the moment anything else
// writes to that directory.
//
// The agent runs with an elevated administrator token, which holds
// ``SeCreateSymbolicLinkPrivilege``; a hardlink and then a copy are
// tried in turn anyway, because a driver the application cannot open is
// a launch that fails for a reason nothing reports.
class GuestDrivers {
public:
    GuestDrivers() = delete;

    // Same shape as ``Launcher::EnvVar`` — one ``NAME=value``
    // override — so the two lists concatenate.
    using EnvVar = std::pair<std::string, std::string>;

    // What one launch placed. Held by that launch's ``LaunchGroup`` and
    // handed back to ``unstage`` when the last process of the launch
    // exits, so a directory is cleaned by the launch that dirtied it
    // rather than by a walk that would have to guess.
    struct Staging {
        // UTF-8, no trailing separator, the launched image's own
        // directory first. Empty when nothing was staged at all.
        std::vector<std::string> dirs;
        // False when a driver that is installed in this guest could not
        // be placed. The launch goes ahead regardless — see the note on
        // ``stage_for``.
        bool complete = true;
    };

    // Link every installed driver into each directory of ``exe_path``'s
    // tree that holds an executable, matching that directory's own PE
    // architecture, and append the variables the loaders need to
    // ``env``. Existing links of ours are replaced; anything that is
    // not a link is left where it is and reported.
    //
    // ``error_out`` explains a ``complete == false``. A driver that is
    // not installed in this guest is not a failure: the application then
    // finds whatever Windows has, which is slow rather than broken.
    //
    // Nothing is ever staged into the Windows directory. A driver there
    // is machine-wide, outlives the launch, and is the one placement
    // that can leave the guest worse than it was found.
    [[nodiscard]] static Staging stage_for(const std::string& exe_path,
                                           std::vector<EnvVar>* env,
                                           std::string* error_out = nullptr);

    // Remove what ``stage_for`` placed. Only links of ours go: a real
    // file of the same name is an application's own driver and stays.
    //
    // A directory staged by two live launches is cleaned when the
    // second of them ends, not the first.
    static void unstage(const Staging& staging);

    // Point the system OpenCL loader at the installed driver, in both
    // registry views.
    //
    // A Khronos loader reads OCL_ICD_FILENAMES through a secure-getenv
    // that returns nothing to an elevated process, and the agent hands
    // its elevated administrator token to everything it launches — so
    // the variable is set for an application that is not allowed to
    // read it, and the guest enumerated zero OpenCL platforms with a
    // working driver sitting right beside the executable. The registry
    // is the only route an elevated process is offered. (The same is
    // long known of the Vulkan loader and VK_DRIVER_FILES.)
    //
    // Machine-wide, which per-launch staging exists to avoid — but the
    // scope is honest here: the entry names the installed driver, which
    // is installed machine-wide, and there is no other OpenCL driver in
    // the guest for it to displace.
    //
    // Idempotent. Called once at agent startup, and safe to repeat.
    static void register_installed_icds();
};

}  // namespace lsw::agent::apps
