#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/util/base64.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/linkproto/commands.hpp"
#include "lsw/linkproto/file_transfer.hpp"

#include <stdexcept>

namespace lsw::agent::rpc::handlers {

struct UploadFileHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        auto& uploads = ctx.services.uploads();
        const auto action = params.at("action").get<std::string>();
        if (action == "begin") {
            const auto size = params.at("size");
            if (!size.is_number_unsigned())
                throw std::invalid_argument("upload size must be an unsigned integer");
            return {{"ok", true},
                    {"upload_id", uploads.begin(params.at("name").get<std::string>(),
                                                size.get<std::uint64_t>())}};
        }
        const auto id = params.at("upload_id").get<std::string>();
        if (action == "chunk") {
            const auto text = params.at("data").get<std::string>();
            if (text.size() > (linkproto::kFileChunkBytes + 2) / 3 * 4)
                throw std::invalid_argument("upload chunk is too large");
            const auto offset = params.at("offset");
            if (!offset.is_number_unsigned())
                throw std::invalid_argument("upload offset must be an unsigned integer");
            const auto bytes = util::Base64::decode(text);
            return {{"ok", true},
                    {"offset", uploads.append(id, offset.get<std::uint64_t>(), bytes)}};
        }
        if (action == "finish") {
            return {{"ok", true},
                    {"path", util::TextEncoding::to_utf8(uploads.finish(id).wstring())}};
        }
        if (action == "abort") {
            uploads.abort(id);
            return {{"ok", true}};
        }
        throw std::invalid_argument("unknown upload action");
    }

    [[nodiscard]] bool silent() const override { return true; }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UploadFileHandler, ::lsw::linkproto::cmd::kUploadFile)
