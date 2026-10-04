#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/util/wide_string.hpp"
#include "lsw/agent/win/file_time.hpp"
#include "lsw/agent/win/unique_handle.hpp"
#include "lsw/linkproto/commands.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <algorithm>
#include <string>
#include <vector>

namespace lsw::agent::rpc::handlers {

namespace {

// One page. Big enough that an ordinary folder arrives whole, small
// enough that System32 does not have to fit in a reply slot to be
// listed at all.
constexpr int kDefaultLimit = 250;
constexpr int kMaxLimit = 1000;

// The drives, for an empty path. A fixed disk and whatever host folders
// have been mapped in look the same from here, which is the point: the
// picker's sidebar lists them together.
nlohmann::json list_drives() {
    nlohmann::json entries = nlohmann::json::array();
    const DWORD mask = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if ((mask & (1UL << i)) == 0) {
            continue;
        }
        const std::wstring root{static_cast<wchar_t>(L'A' + i), L':', L'\\'};
        wchar_t label[MAX_PATH + 1]{};
        if (::GetVolumeInformationW(root.c_str(), label, MAX_PATH, nullptr, nullptr, nullptr,
                                    nullptr, 0) == 0) {
            label[0] = L'\0';
        }
        entries.push_back({
            {"name", util::TextEncoding::to_utf8(root.substr(0, 2))},
            {"label", util::TextEncoding::to_utf8(label)},
            {"is_dir", true},
            {"size", 0},
            {"mtime", 0},
        });
    }
    return entries;
}

}  // namespace

// Read one page of a directory in the guest.
//
// The manager's file picker asks a Windows path of the machine that
// will run the program, over the connection it already has, rather than
// going through the SMB share: the share exists for the user's file
// manager and can be switched off, and it would see the guest as a
// different account than the one the program runs as.
struct ListDirHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& /*ctx*/) override {
        const auto path = params.value("path", std::string{});
        const int cursor = std::max(0, params.value("cursor", 0));
        const int limit = std::clamp(params.value("limit", kDefaultLimit), 1, kMaxLimit);
        const bool dirs_only = params.value("dirs_only", false);
        const auto suffix = util::TextEncoding::to_wide(params.value("suffix", std::string{}));

        if (path.empty()) {
            return {{"path", ""}, {"entries", list_drives()}};
        }

        std::wstring wide = util::TextEncoding::to_wide(path);
        if (!wide.empty() && wide.back() != L'\\') {
            wide.push_back(L'\\');
        }

        WIN32_FIND_DATAW found{};
        const win::UniqueFindHandle find{::FindFirstFileExW((wide + L"*").c_str(), FindExInfoBasic,
                                                            &found, FindExSearchNameMatch, nullptr,
                                                            FIND_FIRST_EX_LARGE_FETCH)};
        if (!find) {
            const DWORD error = ::GetLastError();
            return {
                {"path", path},
                {"entries", nlohmann::json::array()},
                {"error", error == ERROR_ACCESS_DENIED ? "Windows would not open this folder"
                                                       : "This folder could not be read"},
                {"error_code", static_cast<std::uint32_t>(error)},
            };
        }

        // Folders first, then files, each alphabetically: the order a
        // file manager uses, decided here so every caller gets it.
        std::vector<nlohmann::json> dirs;
        std::vector<nlohmann::json> files;
        do {
            const std::wstring name = found.cFileName;
            if (name == L"." || name == L"..") {
                continue;
            }
            const bool is_dir = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (!is_dir && (dirs_only || !util::WideString::iends_with(name, suffix))) {
                continue;
            }
            const std::uint64_t size =
                (static_cast<std::uint64_t>(found.nFileSizeHigh) << 32) | found.nFileSizeLow;
            nlohmann::json entry = {
                {"name", util::TextEncoding::to_utf8(name)},
                {"is_dir", is_dir},
                {"size", is_dir ? 0 : size},
                {"mtime", win::FileTime::to_unix_ms(found.ftLastWriteTime) / 1000},
                {"hidden", (found.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0},
            };
            (is_dir ? dirs : files).push_back(std::move(entry));
        } while (::FindNextFileW(find.get(), &found) != 0);

        const auto by_name = [](const nlohmann::json& a, const nlohmann::json& b) {
            return a.value("name", std::string{}) < b.value("name", std::string{});
        };
        std::sort(dirs.begin(), dirs.end(), by_name);
        std::sort(files.begin(), files.end(), by_name);

        std::vector<nlohmann::json> all;
        all.reserve(dirs.size() + files.size());
        all.insert(all.end(), dirs.begin(), dirs.end());
        all.insert(all.end(), files.begin(), files.end());

        nlohmann::json page = nlohmann::json::array();
        const int total = static_cast<int>(all.size());
        for (int i = cursor; i < total && i < cursor + limit; ++i) {
            page.push_back(all[static_cast<std::size_t>(i)]);
        }

        nlohmann::json reply = {
            {"path", path},
            {"entries", std::move(page)},
            {"total", total},
        };
        if (cursor + limit < total) {
            reply["next_cursor"] = cursor + limit;
        }
        return reply;
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        return std::to_string(reply.value("entries", nlohmann::json::array()).size()) + " of " +
               std::to_string(reply.value("total", 0)) + " entries";
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ListDirHandler, ::lsw::linkproto::cmd::kListDir)
