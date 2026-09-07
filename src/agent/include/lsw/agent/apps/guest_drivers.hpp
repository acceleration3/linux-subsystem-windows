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

    // Link every driver matching ``exe_path``'s PE architecture into
    // its directory, and append the variables the loaders need to
    // ``env``. Existing links of ours are replaced; anything that is
    // not a link is left where it is and reported.
    //
    // Returns false when a driver that exists could not be placed.
    // ``error_out`` explains a false. A driver that is not installed in
    // this guest is not a failure: the application then finds whatever
    // Windows has, which is slow rather than broken.
    [[nodiscard]] static bool link_beside(const std::string& exe_path,
                                          std::vector<EnvVar>* env,
                                          std::string* error_out = nullptr);

    // Remove only the links this placed. A real file of the same name
    // is an application's own driver and stays.
    [[nodiscard]] static bool unlink_beside(const std::string& exe_path,
                                            std::string* error_out = nullptr);
};

}  // namespace lsw::agent::apps
