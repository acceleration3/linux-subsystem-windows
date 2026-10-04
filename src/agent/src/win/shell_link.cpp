#include "lsw/agent/win/shell_link.hpp"

#include "lsw/agent/win/com.hpp"
#include "lsw/agent/win/com_ptr.hpp"

// clang-format off
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
// clang-format on

#include <iterator>

namespace lsw::agent::win {

std::optional<ShellLink::Target> ShellLink::resolve(const std::wstring& lnk_path, PathMode mode) {
    // Declared first so it is torn down last: every interface below must
    // be released before CoUninitialize.
    const Apartment apartment{Apartment::Model::SingleThreaded};

    ComPtr<IShellLinkW> link;
    if (::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                           reinterpret_cast<void**>(link.GetAddressOf())) != S_OK ||
        !link) {
        return std::nullopt;
    }
    ComPtr<IPersistFile> file;
    if (link.As(&file) != S_OK || !file || file->Load(lnk_path.c_str(), STGM_READ) != S_OK) {
        return std::nullopt;
    }

    Target target;
    wchar_t path[MAX_PATH] = {};
    if (link->GetPath(path, MAX_PATH, nullptr,
                      mode == PathMode::Raw ? SLGP_RAWPATH : SLGP_UNCPRIORITY) == S_OK) {
        target.path = path;
    }
    // Twice INFOTIPSIZE: the shell's own limit on arguments, with room.
    wchar_t args[2 * INFOTIPSIZE] = {};
    if (link->GetArguments(args, static_cast<int>(std::size(args))) == S_OK) {
        target.args = args;
    }
    wchar_t dir[MAX_PATH] = {};
    if (link->GetWorkingDirectory(dir, MAX_PATH) == S_OK) {
        target.working_dir = dir;
    }
    return target;
}

}  // namespace lsw::agent::win
