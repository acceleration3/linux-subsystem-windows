#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::host::link {

class AgentRpcClient;

class AgentGuestFiles {
public:
    explicit AgentGuestFiles(AgentRpcClient& rpc) : rpc_(rpc) {}

    static constexpr std::uint64_t kChunkBytes = 1024 * 1024;

    [[nodiscard]] bool read(const std::string& guest_path, QByteArray& out, std::string& error,
                            bool delete_when_done = false);

    [[nodiscard]] bool read_to_file(const std::string& guest_path, const QString& local_path,
                                    std::string& error, bool delete_when_done = false);

    struct Packed {
        bool ok = false;
        std::string path;
        std::uint64_t size = 0;
        std::string error;
    };
    [[nodiscard]] Packed pack_logs();

    struct Cleared {
        bool ok = false;
        int deleted = 0;
        int emptied = 0;
        std::vector<std::string> skipped;
        std::string error;
    };
    [[nodiscard]] Cleared clear_logs();

private:
    AgentRpcClient& rpc_;
};

}
