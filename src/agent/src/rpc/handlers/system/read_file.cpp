#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/util/base64.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/unique_handle.hpp"
#include "lsw/linkproto/commands.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace lsw::agent::rpc::handlers {

namespace {

constexpr std::uint64_t kMostBytes = 1024 * 1024;

}

struct ReadFileHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ) override {
        const Params p{params, linkproto::cmd::kReadFile};
        const std::string utf8_path = p.require_string("path");
        const std::wstring path = util::TextEncoding::to_wide(utf8_path);
        const auto offset = p.get("offset", std::uint64_t{0});
        const auto length = std::min(p.get("length", kMostBytes), kMostBytes);

        win::UniqueHandle file{::CreateFileW(path.c_str(), GENERIC_READ,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                             nullptr)};
        if (!file) {
            throw std::runtime_error("read_file: cannot open " + utf8_path + ": " +
                                     win::ErrorText::last_error());
        }
        LARGE_INTEGER size{};
        if (::GetFileSizeEx(file.get(), &size) == FALSE) {
            throw std::runtime_error("read_file: cannot size " + utf8_path + ": " +
                                     win::ErrorText::last_error());
        }
        std::vector<std::uint8_t> bytes;
        const auto total = static_cast<std::uint64_t>(size.QuadPart);
        if (offset < total) {
            LARGE_INTEGER to{};
            to.QuadPart = static_cast<LONGLONG>(offset);
            bytes.resize(static_cast<std::size_t>(std::min(length, total - offset)));
            DWORD got = 0;
            if (::SetFilePointerEx(file.get(), to, nullptr, FILE_BEGIN) == FALSE ||
                ::ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &got,
                           nullptr) == FALSE) {
                throw std::runtime_error("read_file: cannot read " + utf8_path + ": " +
                                         win::ErrorText::last_error());
            }
            bytes.resize(got);
        }
        const bool eof = offset + bytes.size() >= total;
        if (eof && p.get("delete_at_eof", false)) {
            file.reset();
            if (::DeleteFileW(path.c_str()) == FALSE) {
                throw std::runtime_error("read_file: cannot delete " + utf8_path + ": " +
                                         win::ErrorText::last_error());
            }
        }
        return {{"data", util::Base64::encode(bytes)}, {"size", total}, {"eof", eof}};
    }

    [[nodiscard]] log::Level request_level() const override { return log::Level::Trace; }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        return std::to_string(reply.value("data", std::string{}).size()) + "B base64, eof=" +
               (reply.value("eof", false) ? "true" : "false");
    }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ReadFileHandler, ::lsw::linkproto::cmd::kReadFile)
