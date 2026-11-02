#include "lsw/agent/apps/program_command.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/util/wide_string.hpp"
#include "lsw/linkproto/commands.hpp"

// clang-format off
#include <windows.h>
#include <shlwapi.h>
// clang-format on

#include <stdexcept>
#include <string>

namespace lsw::agent::rpc::handlers {

namespace {
std::wstring associated(ASSOCSTR what, const std::wstring& extension) {
    DWORD length = 0;
    const ASSOCF flags = ASSOCF_NOTRUNCATE | ASSOCF_REMAPRUNDLL;
    const HRESULT sized = ::AssocQueryStringW(flags, what, extension.c_str(), L"open", nullptr, &length);
    if (sized != S_FALSE && FAILED(sized)) {
        return {};
    }
    if (length == 0) {
        return {};
    }
    std::wstring text(length, L'\0');
    if (FAILED(::AssocQueryStringW(flags, what, extension.c_str(), L"open", text.data(), &length))) {
        return {};
    }
    text.resize(wcsnlen(text.c_str(), text.size()));
    return text;
}

}

struct FileHandlerHandler : RpcHandler {
    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }

    nlohmann::json handle(const nlohmann::json& params, RpcContext& ) override {
        const std::wstring path = util::TextEncoding::to_wide(
            Params{params, linkproto::cmd::kFileHandler}.require_string("path"));
        const auto dot = path.find_last_of(L'.');
        const auto slash = path.find_last_of(L"\\/");
        if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash) ||
            dot + 1 == path.size()) {
            throw std::runtime_error("the file has no extension, so Windows has no program for it");
        }
        const std::wstring extension = util::WideString::lower(path.substr(dot));

        const std::wstring name = associated(ASSOCSTR_FRIENDLYAPPNAME, extension);
        if (const std::wstring aumid = associated(ASSOCSTR_APPID, extension);
            aumid.find(L'!') != std::wstring::npos) {
            return {{"aumid", util::TextEncoding::to_utf8(aumid)},
                    {"name", util::TextEncoding::to_utf8(name)}};
        }
        const auto command = apps::ProgramCommand::split_for_document(
            associated(ASSOCSTR_COMMAND, extension));
        std::wstring exe = command.program.empty() ? associated(ASSOCSTR_EXECUTABLE, extension) : command.program;
        if (exe.empty()) {
            throw std::runtime_error("no program is registered for " +
                                     util::TextEncoding::to_utf8(extension) + " files");
        }
        if (util::WideString::iends_with(exe, L"\\rundll32.exe") || util::WideString::lower(exe) == L"rundll32.exe") {
            throw std::runtime_error(util::TextEncoding::to_utf8(extension) +
                                     " files are opened by rundll32, which cannot be asked to open a file from here");
        }
        if (util::WideString::iends_with(exe, L"\\openwith.exe") || util::WideString::lower(exe) == L"openwith.exe") {
            throw std::runtime_error("no program is registered for " +
                                     util::TextEncoding::to_utf8(extension) + " files");
        }
        nlohmann::json reply = {{"exe_path", util::TextEncoding::to_utf8(exe)},
                                {"name", util::TextEncoding::to_utf8(name)}};
        if (!command.leading_args.empty()) {
            reply["args"] = util::TextEncoding::to_utf8(command.leading_args);
        }
        return reply;
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        return reply.value("exe_path", reply.value("aumid", std::string{}));
    }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::FileHandlerHandler, ::lsw::linkproto::cmd::kFileHandler)
