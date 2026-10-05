#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/util/wide_string.hpp"
#include "lsw/agent/win/com.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/unique_handle.hpp"
#include "lsw/linkproto/app_package.hpp"
#include "lsw/linkproto/commands.hpp"

#include <shellapi.h>
#include <stdexcept>
#include <windows.h>

namespace lsw::agent::rpc::handlers {

struct OpenPackageHandler : RpcHandler {
    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }

    nlohmann::json handle(const nlohmann::json& params, RpcContext& /*ctx*/) override {
        const auto path = Params{params, linkproto::cmd::kOpenPackage}.require_string("path");
        if (!linkproto::is_app_package(path))
            throw std::invalid_argument("open_package requires an MSIX/APPX package or bundle");
        const auto wide = util::TextEncoding::to_wide(path);
        const DWORD attributes = ::GetFileAttributesW(wide.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
            throw std::runtime_error("cannot read package: " + path);
        // Run inside the user's helper: file associations and App Installer
        // activation belong to that user's shell, not the session-0 service.
        const win::Apartment apartment(win::Apartment::Model::SingleThreaded);
        if (!apartment.ok())
            throw std::runtime_error("cannot initialize the Windows shell");
        const auto directory = util::WideString::parent_dir(wide);
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        info.lpVerb = L"open";
        info.lpFile = wide.c_str();
        info.lpDirectory = directory ? directory->c_str() : nullptr;
        info.nShow = SW_SHOWNORMAL;
        if (!::ShellExecuteExW(&info)) {
            throw std::runtime_error(
                "Windows could not open App Installer: " + win::ErrorText::last_error() +
                ". Ensure App Installer is installed in the guest.");
        }
        const win::UniqueHandle process(info.hProcess);
        // Shell/AppModel activation may reuse a process, return no process,
        // or return a short-lived broker. None is a reliable UI mirror root.
        return {{"ok", true}, {"pid", 0}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::OpenPackageHandler, ::lsw::linkproto::cmd::kOpenPackage)
