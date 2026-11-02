#include "lsw/agent/apps/file_associations.hpp"

#include "lsw/agent/apps/program_command.hpp"
#include "lsw/agent/util/wide_string.hpp"
#include "lsw/agent/win/environment.hpp"
#include "lsw/agent/win/reg_key.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <algorithm>
#include <set>
#include <unordered_set>
#include <utility>

namespace lsw::agent::apps {

namespace {
std::wstring handler_of(const win::RegKey& classes, const std::wstring& prog_id) {
    const auto progid = classes.open_subkey(prog_id.c_str(), KEY_READ);
    if (!progid) {
        return {};
    }
    if (const auto application = progid->open_subkey(L"Application", KEY_READ)) {
        if (auto aumid = application->read_string(L"AppUserModelID"); aumid && !aumid->empty()) {
            return util::WideString::lower(*aumid);
        }
    }
    const auto shell = progid->open_subkey(L"shell", KEY_READ);
    if (!shell) {
        return {};
    }
    const std::wstring verb = shell->read_string(L"").value_or(std::wstring{L"open"});
    const std::wstring first = verb.substr(0, verb.find(L','));
    const auto verb_key = shell->open_subkey(first.empty() ? L"open" : first.c_str(), KEY_READ);
    const auto command = verb_key ? verb_key->open_subkey(L"command", KEY_READ) : std::nullopt;
    if (!command) {
        return {};
    }
    const auto line = command->read_string(L"");
    if (!line || line->empty()) {
        return {};
    }
    const std::wstring program = ProgramCommand::program_of(win::Environment::expand(*line));
    return util::WideString::iends_with(program, L".exe") ? util::WideString::lower(program)
                                                          : std::wstring{};
}

}

bool FileAssociations::excluded(const std::wstring& extension) {
    static const std::unordered_set<std::wstring> kNever = {
        L".exe", L".com", L".bat", L".cmd", L".lnk", L".msi", L".msp", L".dll", L".sys", L".scr",
        L".url", L".pif", L".cpl", L".msc", L".reg", L".vbs", L".vbe", L".js",  L".jse", L".wsf",
        L".wsh", L".ps1", L".psm1", L".inf", L".ocx", L".drv", L".appx", L".msix", L".cab",
        L".theme", L".themepack", L".library-ms", L".search-ms", L".searchconnector-ms", L".settingcontent-ms",
    };
    return kNever.contains(extension);
}

FileAssociations::Table FileAssociations::scan() {
    std::map<std::wstring, std::set<std::wstring>> found;
    const auto classes = win::RegKey::open(HKEY_CLASSES_ROOT, L"", KEY_READ);
    if (!classes) {
        return {};
    }
    std::map<std::wstring, std::wstring> handlers;
    const auto handler = [&](const std::wstring& prog_id) -> const std::wstring& {
        auto [it, fresh] = handlers.try_emplace(prog_id);
        if (fresh) {
            it->second = handler_of(*classes, prog_id);
        }
        return it->second;
    };
    for (const std::wstring& name : classes->subkey_names()) {
        if (name.size() < 2 || name.front() != L'.') {
            continue;
        }
        const std::wstring extension = util::WideString::lower(name);
        if (excluded(extension)) {
            continue;
        }
        const auto key = classes->open_subkey(name.c_str(), KEY_READ);
        if (!key) {
            continue;
        }
        std::vector<std::wstring> prog_ids;
        if (auto standard = key->read_string(L""); standard && !standard->empty()) {
            prog_ids.push_back(*standard);
        }
        if (const auto others = key->open_subkey(L"OpenWithProgids", KEY_READ)) {
            for (std::wstring& other : others->value_names()) {
                if (!other.empty()) {
                    prog_ids.push_back(std::move(other));
                }
            }
        }
        for (const std::wstring& prog_id : prog_ids) {
            if (const std::wstring& program = handler(prog_id); !program.empty()) {
                found[program].insert(extension);
            }
        }
    }
    for (const auto [root, view] : {std::pair{HKEY_LOCAL_MACHINE, static_cast<REGSAM>(KEY_WOW64_64KEY)},
                                    std::pair{HKEY_LOCAL_MACHINE, static_cast<REGSAM>(KEY_WOW64_32KEY)},
                                    std::pair{HKEY_CURRENT_USER, static_cast<REGSAM>(0)}}) {
        const auto registered = win::RegKey::open(root, L"SOFTWARE\\RegisteredApplications", KEY_READ | view);
        if (!registered) {
            continue;
        }
        for (const std::wstring& app : registered->value_names()) {
            const auto capabilities = registered->read_string(app.c_str());
            if (!capabilities || capabilities->empty()) {
                continue;
            }
            const auto key = win::RegKey::open(root, (*capabilities + L"\\FileAssociations").c_str(), KEY_READ | view);
            if (!key) {
                continue;
            }
            for (const std::wstring& name : key->value_names()) {
                const std::wstring extension = util::WideString::lower(name);
                const auto prog_id = key->read_string(name.c_str());
                if (extension.size() < 2 || extension.front() != L'.' || excluded(extension) || !prog_id) {
                    continue;
                }
                if (const std::wstring& program = handler(*prog_id); !program.empty()) {
                    found[program].insert(extension);
                }
            }
        }
    }
    Table table;
    for (auto& [program, extensions] : found) {
        table.emplace(program, std::vector<std::wstring>(extensions.begin(), extensions.end()));
    }
    return table;
}

std::vector<std::wstring> FileAssociations::of(const Table& table, const std::wstring& program) {
    const auto it = table.find(util::WideString::lower(program));
    return it == table.end() ? std::vector<std::wstring>{} : it->second;
}

}
