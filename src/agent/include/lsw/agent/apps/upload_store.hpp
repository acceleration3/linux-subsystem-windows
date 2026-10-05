#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>

namespace lsw::agent::apps {

class UploadStore {
public:
    explicit UploadStore(std::filesystem::path root);
    ~UploadStore();

    [[nodiscard]] std::string begin(const std::string& name, std::uint64_t size);
    [[nodiscard]] std::uint64_t append(const std::string& id, std::uint64_t offset,
                                       std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::filesystem::path finish(const std::string& id);
    void abort(const std::string& id);

private:
    struct Upload {
        std::filesystem::path directory;
        std::filesystem::path destination;
        std::ofstream stream;
        std::uint64_t size = 0;
        std::uint64_t offset = 0;
        std::chrono::steady_clock::time_point touched;
    };

    void discard(Upload& upload) noexcept;
    void reap();

    std::filesystem::path root_;
    std::mutex mutex_;
    std::unordered_map<std::string, Upload> uploads_;
};

}  // namespace lsw::agent::apps
