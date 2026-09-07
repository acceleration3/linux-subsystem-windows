#include "lsw/agent/apps/guest_drivers.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <array>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace lsw::agent::apps {

namespace {

enum class Arch { Unknown, X64, X86 };

// Where the wizard installs each build of every driver. One directory
// per architecture because a 32-bit process cannot load a 64-bit DLL,
// and every D3D8 / D3D9 title is 32-bit.
constexpr const char* kRoot = "C:\\ProgramData\\LSW";

// What gets linked, and out of which per-architecture directory. The
// name is the same on both sides: the application's loader looks for
// ``opengl32.dll`` by the DLL search order and for ``rvk_icd.dll`` by
// the manifest written beside it, and in neither case is there a second
// spelling to translate to.
//
// The order matters for exactly one pair: ``vulkan-1.dll`` must be
// beside the exe before a translation layer beside the exe calls into
// it, and on a guest with no GPU driver there is no loader anywhere
// else on the search path.
struct Driver {
    const char* name;
    const char* dir64;  // subdirectory of kRoot holding the x86-64 build
    const char* dir32;  // ... and the i686 one
};

constexpr std::array<Driver, 11> kDrivers = {{
    {"vulkan-1.dll", "rvk", "rvk32"},
    {"rvk_icd.dll", "rvk", "rvk32"},
    {"opengl32.dll", "rvk", "rvk32"},
    {"rvk_opencl.dll", "rvk", "rvk32"},
    {"d3d8.dll", "dxvk\\x64", "dxvk\\x32"},
    {"d3d9.dll", "dxvk\\x64", "dxvk\\x32"},
    {"d3d10core.dll", "dxvk\\x64", "dxvk\\x32"},
    {"d3d11.dll", "dxvk\\x64", "dxvk\\x32"},
    {"dxgi.dll", "dxvk\\x64", "dxvk\\x32"},
    {"d3d12.dll", "vkd3d\\x64", "vkd3d\\x86"},
    {"d3d12core.dll", "vkd3d\\x64", "vkd3d\\x86"},
}};

// The Vulkan loader reads a manifest, not a DLL, and the path in it
// must be absolute: the Windows loader opens a driver with
// ``LoadLibraryEx(LOAD_WITH_ALTERED_SEARCH_PATH)``, which rejects a
// relative path with error 87, loads nothing, and lets the first Vulkan
// call return VK_ERROR_OUT_OF_HOST_MEMORY — from which an application
// usually exits with no message at all.
constexpr const char* kIcdManifestName = "rvk_icd.json";

// The environment is one value for the whole process tree while the
// drivers are per-directory, so both loader variables carry a list. A
// Vulkan ICD or an OpenCL vendor that cannot be loaded — the 32-bit one
// named to a 64-bit process, and the other way round — is skipped by
// its loader rather than failing it, which is what makes listing every
// architecture at once safe.
constexpr wchar_t kListSeparator = L';';

// Bounds on the walk. A tree deeper or wider than this is not a
// launcher-plus-payload arrangement any more, and staging into all of
// it would be guessing rather than following the payload.
constexpr int kMaxDepth = 4;
constexpr std::size_t kMaxDirs = 64;
// How many executables in one directory are read to decide its
// architecture. A directory disagreeing with itself past the first
// dozen entries does not exist; the cap is here so a directory holding
// hundreds of them costs a bounded number of file opens.
constexpr int kMaxArchProbes = 16;

// Paths are wide from here down. The exe path arrives from the manager
// as UTF-8 and is converted once, at the boundary: the ANSI entry points
// read a path in the process code page, which is not UTF-8, so an
// application installed under a name with any non-ASCII character in it
// would have had its drivers placed somewhere else or nowhere.
[[nodiscard]] std::wstring parent_dir(const std::wstring& path) {
    const auto pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring{L"."} : path.substr(0, pos);
}

[[nodiscard]] std::wstring join(const std::wstring& dir, const std::wstring& leaf) {
    return dir + L"\\" + leaf;
}

[[nodiscard]] std::wstring join(const std::wstring& dir, const char* leaf) {
    return join(dir, util::TextEncoding::to_wide(leaf));
}

[[nodiscard]] std::wstring lower(std::wstring s) {
    for (wchar_t& c : s) {
        c = static_cast<wchar_t>(::towlower(c));
    }
    return s;
}

// A directory path in the form everything below compares: lowercased,
// with any trailing separator removed, so ``C:\Foo`` and ``c:\foo\``
// are one key.
[[nodiscard]] std::wstring dir_key(const std::wstring& dir) {
    std::wstring s = lower(dir);
    while (s.size() > 1 && (s.back() == L'\\' || s.back() == L'/')) {
        s.pop_back();
    }
    return s;
}

[[nodiscard]] std::wstring expand(const wchar_t* spec) {
    const DWORD n = ::ExpandEnvironmentStringsW(spec, nullptr, 0);
    if (n == 0) {
        return {};
    }
    std::wstring out(n, L'\0');
    if (::ExpandEnvironmentStringsW(spec, out.data(), n) == 0) {
        return {};
    }
    while (!out.empty() && out.back() == L'\0') {
        out.pop_back();
    }
    return out;
}

// Read enough of the PE header to reach IMAGE_NT_HEADERS::FileHeader::Machine.
[[nodiscard]] Arch detect_arch(const std::wstring& path) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return Arch::Unknown;
    }
    std::array<std::uint8_t, 4096> buf{};
    DWORD got = 0;
    ::ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr);
    ::CloseHandle(h);
    if (got < 0x40 + 4) {
        return Arch::Unknown;
    }
    const std::uint32_t pe_off = std::uint32_t(buf[0x3C]) | (std::uint32_t(buf[0x3D]) << 8)
                                 | (std::uint32_t(buf[0x3E]) << 16)
                                 | (std::uint32_t(buf[0x3F]) << 24);
    if (pe_off + 6 > got || buf[pe_off] != 'P' || buf[pe_off + 1] != 'E' || buf[pe_off + 2] != 0
        || buf[pe_off + 3] != 0) {
        return Arch::Unknown;
    }
    const std::uint16_t machine =
        std::uint16_t(buf[pe_off + 4]) | (std::uint16_t(buf[pe_off + 5]) << 8);
    if (machine == 0x8664) {
        return Arch::X64;
    }
    if (machine == 0x014C) {
        return Arch::X86;
    }
    return Arch::Unknown;
}

[[nodiscard]] bool exists(const std::wstring& path) {
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// A path we placed: a symbolic link, or the hardlink/copy a link
// failure fell back to. Only the reparse point can be told apart with
// certainty, which is why links are the first choice.
[[nodiscard]] bool is_our_link(const std::wstring& path) {
    const DWORD attrs = ::GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

// The Windows directory and everything under it. A driver staged there
// is not scoped to a launch: it is on the search path of every process
// in the guest, it survives the application that caused it, and
// ``vulkan-1.dll`` in particular has no system copy to be rejected by
// the "leave the application's own file alone" rule — so it would be
// created, and would then answer for every Vulkan application in the
// guest forever. Never stage into it.
[[nodiscard]] bool under_windows_dir(const std::wstring& dir) {
    static const std::wstring windows = dir_key(expand(L"%SystemRoot%"));
    if (windows.empty()) {
        return false;
    }
    const std::wstring key = dir_key(dir);
    return key == windows
           || (key.size() > windows.size() && key.compare(0, windows.size(), windows) == 0
               && key[windows.size()] == L'\\');
}

// A directory that holds unrelated applications rather than one
// application's files. The tree under it is not a payload, so it is
// staged as a single directory and not walked — an executable in
// ``C:\Program Files`` is one program, and its neighbours are not part
// of it.
[[nodiscard]] bool is_container_dir(const std::wstring& dir) {
    static const std::vector<std::wstring> containers = [] {
        std::vector<std::wstring> out;
        for (const wchar_t* spec : {L"%SystemDrive%\\", L"%ProgramFiles%", L"%ProgramW6432%",
                                    L"%ProgramFiles(x86)%", L"%ProgramData%", L"%PUBLIC%",
                                    L"%USERPROFILE%", L"%APPDATA%", L"%LOCALAPPDATA%",
                                    L"%USERPROFILE%\\Desktop", L"%USERPROFILE%\\Downloads",
                                    L"%USERPROFILE%\\Documents"}) {
            std::wstring key = dir_key(expand(spec));
            if (!key.empty()) {
                out.push_back(std::move(key));
            }
        }
        return out;
    }();
    const std::wstring key = dir_key(dir);
    for (const std::wstring& c : containers) {
        if (key == c) {
            return true;
        }
    }
    return false;
}

// The architecture the drivers in ``dir`` have to match: whatever most
// of the executables there are. A directory mixing the two is a payload
// beside its own 32-bit helper or updater, and only one build of
// ``opengl32.dll`` can hold that name — so the majority wins and a tie
// goes to x86-64, which is the application rather than the helper in
// every arrangement seen so far.
[[nodiscard]] Arch dir_arch(const std::wstring& dir) {
    WIN32_FIND_DATAW found{};
    HANDLE h = ::FindFirstFileW(join(dir, L"*.exe").c_str(), &found);
    if (h == INVALID_HANDLE_VALUE) {
        return Arch::Unknown;
    }
    int x64 = 0;
    int x86 = 0;
    int probes = 0;
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        switch (detect_arch(join(dir, found.cFileName))) {
            case Arch::X64:
                ++x64;
                break;
            case Arch::X86:
                ++x86;
                break;
            case Arch::Unknown:
                break;
        }
        ++probes;
    } while (probes < kMaxArchProbes && ::FindNextFileW(h, &found) != 0);
    ::FindClose(h);
    if (x64 == 0 && x86 == 0) {
        return Arch::Unknown;
    }
    if (x64 != x86) {
        LSW_AGENT_DEBUG("guest_drivers: " + util::TextEncoding::to_utf8(dir) + " holds "
                        + std::to_string(x64) + " x64 and " + std::to_string(x86)
                        + " x86 executables");
    }
    return x86 > x64 ? Arch::X86 : Arch::X64;
}

[[nodiscard]] bool holds_executable(const std::wstring& dir) {
    WIN32_FIND_DATAW found{};
    HANDLE h = ::FindFirstFileW(join(dir, L"*.exe").c_str(), &found);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    ::FindClose(h);
    return true;
}

// Every directory of ``root``'s tree that holds an executable, ``root``
// itself first — that one is staged whether or not anything else is,
// because it is where the image we were asked to launch lives.
//
// Reparse points are not descended: a junction pointing back up the
// tree would otherwise be walked until the depth cap stopped it, and a
// junction pointing out of it would stage a directory that is not part
// of this application at all.
[[nodiscard]] std::vector<std::wstring> exe_dirs_under(const std::wstring& root) {
    std::vector<std::wstring> out{root};
    if (is_container_dir(root) || under_windows_dir(root)) {
        return out;
    }
    std::deque<std::pair<std::wstring, int>> pending{{root, 0}};
    while (!pending.empty() && out.size() < kMaxDirs) {
        const auto [dir, depth] = pending.front();
        pending.pop_front();
        if (depth >= kMaxDepth) {
            continue;
        }
        WIN32_FIND_DATAW found{};
        HANDLE h = ::FindFirstFileW(join(dir, L"*").c_str(), &found);
        if (h == INVALID_HANDLE_VALUE) {
            continue;
        }
        do {
            const std::wstring name = found.cFileName;
            if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0
                || (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
                || name == L"." || name == L"..") {
                continue;
            }
            const std::wstring sub = join(dir, name);
            if (holds_executable(sub) && out.size() < kMaxDirs) {
                out.push_back(sub);
            }
            pending.emplace_back(sub, depth + 1);
        } while (::FindNextFileW(h, &found) != 0);
        ::FindClose(h);
    }
    return out;
}

// How many live launches staged each directory, so the first of two
// applications sharing a tree to exit does not take the second one's
// drivers with it. Lost across an agent restart, which is harmless: a
// directory whose count is gone is cleaned by whichever launch ends
// next, and the launch after that re-links it.
// Never destroyed: the reaper thread that calls ``unstage`` is joined
// during static destruction, and a counter torn down before it would be
// one it still reads.
std::mutex& refs_mutex() {
    static std::mutex* m = new std::mutex{};
    return *m;
}

std::map<std::wstring, int>& refs() {
    static auto* r = new std::map<std::wstring, int>{};
    return *r;
}

void acquire_dir(const std::wstring& dir) {
    const std::lock_guard lock(refs_mutex());
    ++refs()[dir_key(dir)];
}

// True when this was the last launch using ``dir`` and its links should
// go. An untracked directory answers true: nothing else claims it.
[[nodiscard]] bool release_dir(const std::wstring& dir) {
    const std::lock_guard lock(refs_mutex());
    const auto it = refs().find(dir_key(dir));
    if (it == refs().end()) {
        return true;
    }
    if (--it->second > 0) {
        return false;
    }
    refs().erase(it);
    return true;
}

// Replace ``target`` with a link to ``source``.
//
// Nothing is removed unless it is ours: an application that ships its
// own d3d11.dll keeps it, and gets its own Direct3D rather than a
// broken one.
[[nodiscard]] bool link_one(const std::wstring& target, const std::wstring& source,
                            std::string* error_out) {
    if (exists(target)) {
        if (!is_our_link(target)) {
            LSW_AGENT_INFO("guest_drivers: " + util::TextEncoding::to_utf8(target)
                           + " is the application's own file; left alone");
            return true;
        }
        ::DeleteFileW(target.c_str());
    }
    // SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE is rejected as an
    // invalid parameter on builds without Developer Mode, so the
    // privileged spelling is tried first and it is the fallback.
    if (::CreateSymbolicLinkW(target.c_str(), source.c_str(), 0) != FALSE
        || ::CreateSymbolicLinkW(target.c_str(), source.c_str(), 0x2 /* UNPRIVILEGED_CREATE */)
               != FALSE) {
        return true;
    }
    const DWORD link_err = ::GetLastError();
    if (::CreateHardLinkW(target.c_str(), source.c_str(), nullptr) != FALSE
        || ::CopyFileW(source.c_str(), target.c_str(), FALSE) != FALSE) {
        LSW_AGENT_DEBUG("guest_drivers: " + util::TextEncoding::to_utf8(target)
                        + " placed without a symlink (err="
                        + std::to_string(link_err) + ")");
        return true;
    }
    if (error_out != nullptr) {
        *error_out = "cannot place " + util::TextEncoding::to_utf8(target)
                     + " (symlink err=" + std::to_string(link_err)
                     + ", copy err=" + std::to_string(::GetLastError()) + ")";
    }
    return false;
}

// The manifest names the driver beside it, absolutely.
[[nodiscard]] bool write_icd_manifest(const std::wstring& dir, std::string* error_out) {
    // The manifest is JSON, which is UTF-8; the path in it is a guest
    // path, so it converts here and nowhere else.
    const std::string driver = util::TextEncoding::to_utf8(join(dir, "rvk_icd.dll"));
    std::string escaped;
    escaped.reserve(driver.size() * 2);
    for (const char c : driver) {
        if (c == '\\') {
            escaped += "\\\\";
        } else {
            escaped += c;
        }
    }
    const std::wstring manifest = join(dir, kIcdManifestName);
    // Through std::filesystem::path: libstdc++ has no wide-string
    // ofstream constructor, and narrowing the path back would undo the
    // point of holding it wide.
    std::ofstream out{std::filesystem::path{manifest},
                      std::ios::trunc | std::ios::binary};
    if (!out) {
        if (error_out != nullptr) {
            *error_out = "cannot write " + util::TextEncoding::to_utf8(manifest);
        }
        return false;
    }
    out << "{\n"
        << "    \"file_format_version\": \"1.0.0\",\n"
        << "    \"ICD\": {\n"
        << "        \"library_path\": \"" << escaped << "\",\n"
        << "        \"api_version\": \"1.3.0\"\n"
        << "    }\n"
        << "}\n";
    return out.good();
}

[[nodiscard]] std::string join_list(const std::vector<std::wstring>& paths) {
    std::wstring out;
    for (const std::wstring& p : paths) {
        if (!out.empty()) {
            out += kListSeparator;
        }
        out += p;
    }
    return util::TextEncoding::to_utf8(out);
}

// Link every installed driver of ``arch`` into ``dir``. Returns false
// when one that exists could not be placed; ``vulkan_icd`` and
// ``opencl_icd`` say which loaders now have something to find there.
[[nodiscard]] bool stage_dir(const std::wstring& dir, Arch arch, bool* vulkan_icd,
                             bool* opencl_icd, std::string* error_out) {
    bool all_ok = true;
    for (const auto& driver : kDrivers) {
        const std::wstring source =
            join(join(util::TextEncoding::to_wide(kRoot),
                      arch == Arch::X64 ? driver.dir64 : driver.dir32),
                 driver.name);
        if (!exists(source)) {
            continue;  // that driver is not installed in this guest
        }
        if (!link_one(join(dir, driver.name), source, error_out)) {
            all_ok = false;
            continue;
        }
        const std::string linked{driver.name};
        *vulkan_icd = *vulkan_icd || linked == "rvk_icd.dll";
        *opencl_icd = *opencl_icd || linked == "rvk_opencl.dll";
    }
    return all_ok;
}

}  // namespace

GuestDrivers::Staging GuestDrivers::stage_for(const std::string& exe_path,
                                              std::vector<EnvVar>* env,
                                              std::string* error_out) {
    Staging staged;
    const std::wstring exe = util::TextEncoding::to_wide(exe_path);
    const Arch launched_arch = detect_arch(exe);
    if (launched_arch == Arch::Unknown) {
        if (error_out != nullptr) {
            *error_out = "cannot read the PE architecture of " + exe_path;
        }
        staged.complete = false;
        return staged;
    }

    std::vector<std::wstring> manifests;
    for (const std::wstring& dir : exe_dirs_under(parent_dir(exe))) {
        if (under_windows_dir(dir)) {
            LSW_AGENT_INFO("guest_drivers: " + util::TextEncoding::to_utf8(dir)
                           + " is inside Windows; nothing staged there");
            continue;
        }
        // A directory whose executables cannot be read falls back to the
        // architecture of the one we launched, which is at worst the
        // guess the single-directory staging always made.
        Arch arch = dir_arch(dir);
        if (arch == Arch::Unknown) {
            arch = launched_arch;
        }
        LSW_AGENT_INFO("guest_drivers: staging " + std::string(arch == Arch::X64 ? "x64" : "x86")
                       + " drivers in " + util::TextEncoding::to_utf8(dir));

        bool vulkan_icd = false;
        bool opencl_icd = false;  // placed, but found through the registry
        if (!stage_dir(dir, arch, &vulkan_icd, &opencl_icd, error_out)) {
            staged.complete = false;
        }
        if (vulkan_icd) {
            if (write_icd_manifest(dir, error_out)) {
                manifests.push_back(join(dir, kIcdManifestName));
            } else {
                staged.complete = false;
            }
        }
        staged.dirs.push_back(util::TextEncoding::to_utf8(dir));
        acquire_dir(dir);
    }

    if (env == nullptr) {
        return staged;
    }
    // The render server's region is an IVSHMEM BAR — a different device
    // from the one carrying the frame ring, which is why the agent no
    // longer has to let go of anything for an application to render.
    env->push_back({"RVK_SERVER", "ivshmem"});
    if (!manifests.empty()) {
        // Both spellings: VK_ICD_FILENAMES is what a loader older
        // than 1.3.207 reads, and a guest may have either.
        const std::string list = join_list(manifests);
        env->push_back({"VK_DRIVER_FILES", list});
        env->push_back({"VK_ICD_FILENAMES", list});
    }
    // No OCL_ICD_FILENAMES. Windows ships the Khronos ICD loader as
    // OpenCL.dll, and it reads that variable through a secure-getenv
    // which hands nothing to an elevated process — which every launch of
    // ours is. Setting it produced a guest that enumerated zero OpenCL
    // platforms with a working driver linked beside the executable; the
    // registry entry ``register_installed_icds`` writes is what the
    // loader actually reads. Left unset rather than set-and-ignored, so
    // a loader that does read it cannot also find the staged copy and
    // enumerate every platform twice.
    return staged;
}

void GuestDrivers::unstage(const Staging& staging) {
    for (const std::string& utf8_dir : staging.dirs) {
        const std::wstring dir = util::TextEncoding::to_wide(utf8_dir);
        if (!release_dir(dir)) {
            LSW_AGENT_DEBUG("guest_drivers: " + utf8_dir
                            + " is still in use by another launch; left staged");
            continue;
        }
        for (const auto& driver : kDrivers) {
            const std::wstring target = join(dir, driver.name);
            if (!is_our_link(target)) {
                continue;
            }
            if (::DeleteFileW(target.c_str()) == FALSE) {
                LSW_AGENT_WARN("guest_drivers: cannot remove "
                               + util::TextEncoding::to_utf8(target)
                               + " err=" + std::to_string(::GetLastError()));
            }
        }
        ::DeleteFileW(join(dir, kIcdManifestName).c_str());
        LSW_AGENT_INFO("guest_drivers: unstaged " + utf8_dir);
    }
}

void GuestDrivers::register_installed_icds() {
    // One entry per architecture, each in the registry view a process of
    // that architecture reads: a 32-bit application is redirected to
    // WOW6432Node and never sees the 64-bit key, so registering only one
    // leaves the other half of the guest with no driver at all.
    struct Entry {
        const char* dir;
        REGSAM view;
        const char* label;
    };
    static constexpr std::array<Entry, 2> kEntries = {{
        {"rvk", KEY_WOW64_64KEY, "x64"},
        {"rvk32", KEY_WOW64_32KEY, "x86"},
    }};

    for (const auto& entry : kEntries) {
        const std::wstring driver =
            join(join(util::TextEncoding::to_wide(kRoot), entry.dir), "rvk_opencl.dll");
        if (!exists(driver)) {
            continue;  // that build is not installed in this guest
        }
        HKEY key = nullptr;
        const LSTATUS opened = ::RegCreateKeyExW(
            HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenCL\\Vendors", 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | entry.view, nullptr, &key, nullptr);
        if (opened != ERROR_SUCCESS) {
            LSW_AGENT_WARN("guest_drivers: cannot open the " + std::string(entry.label)
                           + " OpenCL vendor key (err=" + std::to_string(opened) + ")");
            continue;
        }
        // The value NAME is the driver path; the data is a DWORD the
        // loader reads as "0 means use this one". An unusual shape, and
        // the one the Khronos loader defines.
        const DWORD enabled = 0;
        const LSTATUS set = ::RegSetValueExW(
            key, driver.c_str(), 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&enabled), sizeof(enabled));
        ::RegCloseKey(key);
        if (set != ERROR_SUCCESS) {
            LSW_AGENT_WARN("guest_drivers: cannot register "
                           + util::TextEncoding::to_utf8(driver)
                           + " (err=" + std::to_string(set) + ")");
            continue;
        }
        LSW_AGENT_INFO("guest_drivers: registered the " + std::string(entry.label)
                       + " OpenCL driver " + util::TextEncoding::to_utf8(driver));
    }
}

}  // namespace lsw::agent::apps
