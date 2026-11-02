#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/file_digest.hpp"
#include "lsw/linkproto/commands.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace lsw::agent::rpc::handlers {

namespace {
constexpr std::size_t kMaxPaths = 64;

}

struct FileHashesHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ) override {
        const auto paths = params.value("paths", nlohmann::json::array());
        if (!paths.is_array() || paths.size() > kMaxPaths) {
            throw std::invalid_argument("file_hashes takes a list of at most 64 paths");
        }
        nlohmann::json hashes = nlohmann::json::object();
        for (const auto& path : paths) {
            if (!path.is_string()) {
                throw std::invalid_argument("file_hashes paths must be strings");
            }
            const std::string asked = path.get<std::string>();
            const std::string digest = win::FileDigest::sha256(
                std::filesystem::path(util::TextEncoding::to_wide(asked)));
            hashes[asked] = digest.empty() ? nlohmann::json(nullptr) : nlohmann::json(digest);
        }
        return {{"hashes", hashes}};
    }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::FileHashesHandler, ::lsw::linkproto::cmd::kFileHashes)
