#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/util/text_encoding.hpp"

#include "lsw/linkproto/commands.hpp"

// clang-format off
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
// clang-format on

#include <stdexcept>
#include <string>

namespace lsw::agent::rpc::handlers {

// What a .lnk points at.
//
// Configuring an application by picking its Start menu shortcut is the
// obvious thing to do, and storing the shortcut itself is the wrong
// answer: an installer's uninstaller deletes it and the entry stops
// working for a reason nobody can see. So the target is resolved here,
// once, and the entry keeps its arguments and working folder too.
struct ResolveShortcutHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& /*ctx*/) override {
        const auto path = params.value("path", std::string{});
        if (path.empty()) {
            throw std::invalid_argument("resolve_shortcut: missing path");
        }

        // The agent's threads are not guaranteed to have COM up, and
        // asking twice is not an error worth failing over.
        const HRESULT init = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool uninit = SUCCEEDED(init);

        nlohmann::json reply = {{"target", ""}, {"args", ""}, {"workdir", ""}};
        IShellLinkW* link = nullptr;
        if (SUCCEEDED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_IShellLinkW,
                                         reinterpret_cast<void**>(&link)))) {
            IPersistFile* file = nullptr;
            if (SUCCEEDED(link->QueryInterface(IID_IPersistFile,
                                               reinterpret_cast<void**>(&file)))) {
                if (SUCCEEDED(file->Load(util::TextEncoding::to_wide(path).c_str(),
                                         STGM_READ))) {
                    wchar_t buffer[MAX_PATH]{};
                    if (SUCCEEDED(link->GetPath(buffer, MAX_PATH, nullptr,
                                                SLGP_UNCPRIORITY))) {
                        reply["target"] = util::TextEncoding::to_utf8(buffer);
                    }
                    wchar_t args[INFOTIPSIZE]{};
                    if (SUCCEEDED(link->GetArguments(args, INFOTIPSIZE))) {
                        reply["args"] = util::TextEncoding::to_utf8(args);
                    }
                    wchar_t dir[MAX_PATH]{};
                    if (SUCCEEDED(link->GetWorkingDirectory(dir, MAX_PATH))) {
                        reply["workdir"] = util::TextEncoding::to_utf8(dir);
                    }
                }
                file->Release();
            }
            link->Release();
        }
        if (uninit) {
            ::CoUninitialize();
        }
        return reply;
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        const auto target = reply.value("target", std::string{});
        return target.empty() ? "unresolved" : target;
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ResolveShortcutHandler,
                 ::lsw::linkproto::cmd::kResolveShortcut)
