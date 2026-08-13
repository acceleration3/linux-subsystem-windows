#pragma once

#include <string>

namespace lsw::agent::apps {

// Puts DXVK / VKD3D-Proton next to an application, so its Direct3D
// calls become Vulkan calls — which the LSW ICD then carries to the
// host.
//
// Why next to the executable rather than installed once: Windows
// resolves an implicitly-linked DLL from the directory of the running
// image before it looks anywhere else, and that is the only hook there
// is. ``d3d11.dll`` in the app's folder wins over the system one; the
// same file in ``C:\ProgramData\LSW`` is never consulted. So this is
// necessarily a per-application action, taken at launch, which is why
// it lives here and not in the wizard.
//
// The wizard already installs the DLLs to ``C:\ProgramData\LSW\dxvk``
// and ``\vkd3d``; this copies the right architecture's set into the
// target's directory.
//
// It is deliberately conservative about what it overwrites. An
// application that ships its own ``d3d11.dll`` — some do — must keep
// it: replacing a file we did not place would break the app in a way
// the user did not ask for and cannot easily undo. A marker file
// records what LSW put there, so a second launch can tell its own
// copies from someone else's.
class D3dTranslation {
public:
    D3dTranslation() = delete;

    // Copy the translation DLLs into ``exe_path``'s directory, choosing
    // 32- or 64-bit to match the executable (a mismatched DLL does not
    // load, and the application dies at startup with no useful
    // message).
    //
    // Returns true when the directory ends up holding a usable set.
    // ``error_out`` explains a false. Files that already exist and were
    // not placed by LSW are left alone and reported.
    [[nodiscard]] static bool stage_for(const std::string& exe_path,
                                        std::string* error_out = nullptr);

    // Remove only the files LSW placed, per the marker. Leaves anything
    // else — including an application's own DLLs — untouched.
    [[nodiscard]] static bool unstage_for(const std::string& exe_path,
                                          std::string* error_out = nullptr);

    // True when the executable is 64-bit. Read from the PE header
    // rather than guessed: the wrong architecture's DLL simply fails to
    // load.
    [[nodiscard]] static bool is_64bit_executable(const std::string& exe_path);
};

}  // namespace lsw::agent::apps
