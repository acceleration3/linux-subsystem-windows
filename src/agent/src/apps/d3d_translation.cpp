#include "lsw/agent/apps/d3d_translation.hpp"

#include "lsw/agent/log.hpp"

#include <windows.h>

#include <array>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace lsw::agent::apps {

namespace {

// Where the wizard's setup steps unpack the two translation layers.
constexpr const char* kDxvkRoot = "C:\\ProgramData\\LSW\\dxvk";
constexpr const char* kVkd3dRoot = "C:\\ProgramData\\LSW\\vkd3d";

// The DLL names each layer provides. Not every release ships every one
// — a missing source file is skipped rather than treated as an error,
// because which of these exist is the upstream project's business and
// changes between versions.
constexpr std::array<const char*, 4> kDxvkDlls = {"d3d9.dll", "d3d10core.dll", "d3d11.dll",
                                                  "dxgi.dll"};
constexpr std::array<const char*, 2> kVkd3dDlls = {"d3d12.dll", "d3d12core.dll"};

// Names the marker file, listing what LSW placed. Without it a second
// launch could not tell its own copies from an application's own DLLs,
// and would either refuse forever or clobber something it should not.
constexpr const char* kMarkerName = "lsw-d3d-staged.txt";

[[nodiscard]] std::string directory_of(const std::string& path) {
    const auto slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string{"."} : path.substr(0, slash);
}

[[nodiscard]] std::string join(const std::string& dir, const std::string& name) {
    return dir + "\\" + name;
}

[[nodiscard]] bool file_exists(const std::string& path) {
    const DWORD attrs = ::GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void set_err(std::string* error_out, const std::string& msg) {
    LSW_AGENT_WARN(std::string{"d3d: "} + msg);
    if (error_out != nullptr) {
        *error_out = msg;
    }
}

[[nodiscard]] std::set<std::string> read_marker(const std::string& dir) {
    std::set<std::string> names;
    std::ifstream in{join(dir, kMarkerName)};
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (!line.empty()) {
            names.insert(line);
        }
    }
    return names;
}

[[nodiscard]] bool write_marker(const std::string& dir, const std::set<std::string>& names) {
    std::ofstream out{join(dir, kMarkerName), std::ios::trunc};
    if (!out) {
        return false;
    }
    for (const auto& n : names) {
        out << n << "\n";
    }
    return static_cast<bool>(out);
}

// Copy one DLL, respecting anything already there that is not ours.
// Returns true when the destination ends up holding our copy.
[[nodiscard]] bool place(const std::string& src, const std::string& dir, const std::string& name,
                         const std::set<std::string>& ours, std::vector<std::string>& refused) {
    if (!file_exists(src)) {
        return false;  // this release does not ship it
    }
    const std::string dest = join(dir, name);
    if (file_exists(dest) && ours.count(name) == 0) {
        // Somebody else's file. Leaving it is the only safe choice: it
        // may be the application's own, and overwriting it would break
        // the app in a way nobody asked for.
        refused.push_back(name);
        return false;
    }
    if (::CopyFileA(src.c_str(), dest.c_str(), FALSE) == FALSE) {
        LSW_AGENT_WARN(std::string{"d3d: copy failed "} + src + " -> " + dest
                       + " err=" + std::to_string(::GetLastError()));
        return false;
    }
    return true;
}

}  // namespace

bool D3dTranslation::is_64bit_executable(const std::string& exe_path) {
    std::ifstream in{exe_path, std::ios::binary};
    if (!in) {
        // Unreadable: assume 64-bit, which is what essentially every
        // Windows application is now. Being wrong here fails loudly at
        // launch rather than silently.
        return true;
    }
    // PE: 'MZ' at 0, the PE header offset at 0x3C, 'PE\0\0' there, then
    // the machine word.
    char mz[2]{};
    in.read(mz, 2);
    if (in.gcount() != 2 || mz[0] != 'M' || mz[1] != 'Z') {
        return true;
    }
    in.seekg(0x3C);
    std::uint32_t pe_offset = 0;
    in.read(reinterpret_cast<char*>(&pe_offset), sizeof(pe_offset));
    if (in.gcount() != sizeof(pe_offset)) {
        return true;
    }
    in.seekg(pe_offset);
    char sig[4]{};
    in.read(sig, 4);
    if (in.gcount() != 4 || sig[0] != 'P' || sig[1] != 'E') {
        return true;
    }
    std::uint16_t machine = 0;
    in.read(reinterpret_cast<char*>(&machine), sizeof(machine));
    if (in.gcount() != sizeof(machine)) {
        return true;
    }
    return machine != IMAGE_FILE_MACHINE_I386;
}

bool D3dTranslation::stage_for(const std::string& exe_path, std::string* error_out) {
    if (exe_path.empty()) {
        set_err(error_out, "no executable path");
        return false;
    }
    const std::string dir = directory_of(exe_path);
    const bool is64 = is_64bit_executable(exe_path);
    // DXVK ships x64/x32; VKD3D-Proton ships x64/x86. The names differ,
    // which is exactly the sort of thing that is easier to write down
    // than to remember.
    const std::string dxvk_arch = is64 ? "x64" : "x32";
    const std::string vkd3d_arch = is64 ? "x64" : "x86";

    std::set<std::string> ours = read_marker(dir);
    std::set<std::string> placed;
    std::vector<std::string> refused;

    for (const char* name : kDxvkDlls) {
        if (place(join(join(kDxvkRoot, dxvk_arch), name), dir, name, ours, refused)) {
            placed.insert(name);
        }
    }
    for (const char* name : kVkd3dDlls) {
        if (place(join(join(kVkd3dRoot, vkd3d_arch), name), dir, name, ours, refused)) {
            placed.insert(name);
        }
    }

    if (placed.empty()) {
        set_err(error_out,
                std::string{"no translation DLLs staged into "} + dir
                    + " (is DXVK/VKD3D installed under C:\\ProgramData\\LSW?)");
        return false;
    }
    if (!refused.empty()) {
        std::string list;
        for (const auto& n : refused) {
            list += (list.empty() ? "" : ", ") + n;
        }
        // Not fatal: the layers we did place still work. But an app
        // shipping its own d3d11.dll will keep using it, and that is
        // worth saying rather than leaving as a mystery.
        LSW_AGENT_WARN("d3d: left existing DLLs alone in " + dir + ": " + list);
    }

    // Remember everything ours, old and new, so a later unstage removes
    // exactly what we added.
    for (const auto& n : ours) {
        placed.insert(n);
    }
    if (!write_marker(dir, placed)) {
        LSW_AGENT_WARN("d3d: could not write the marker in " + dir
                       + "; a later unstage will not know what to remove");
    }
    LSW_AGENT_INFO("d3d: staged " + std::to_string(placed.size()) + " DLL(s) ("
                   + (is64 ? "x64" : "x86") + ") into " + dir);
    return true;
}

bool D3dTranslation::unstage_for(const std::string& exe_path, std::string* error_out) {
    if (exe_path.empty()) {
        set_err(error_out, "no executable path");
        return false;
    }
    const std::string dir = directory_of(exe_path);
    const auto ours = read_marker(dir);
    if (ours.empty()) {
        return true;  // nothing of ours here
    }
    for (const auto& name : ours) {
        const std::string path = join(dir, name);
        if (file_exists(path) && ::DeleteFileA(path.c_str()) == FALSE) {
            LSW_AGENT_WARN("d3d: could not remove " + path + " err="
                           + std::to_string(::GetLastError()));
        }
    }
    ::DeleteFileA(join(dir, kMarkerName).c_str());
    LSW_AGENT_INFO("d3d: unstaged translation DLLs from " + dir);
    return true;
}

}  // namespace lsw::agent::apps
