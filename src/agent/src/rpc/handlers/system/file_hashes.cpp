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

// More than any comparison asks about at once: the agent, the mirror's
// guest half, provision.ps1 and the loaders.
constexpr std::size_t kMaxPaths = 64;

}  // namespace

// {"paths": [string]} -> {"hashes": {path: sha256 | null}}: each file's
// SHA-256 in lowercase hex, null for one that is not there or cannot be
// read. Paths come back as they were asked.
struct FileHashesHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& /*ctx*/) override {
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

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::FileHashesHandler, ::lsw::linkproto::cmd::kFileHashes)
