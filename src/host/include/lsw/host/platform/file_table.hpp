#pragma once

#include "lsw/host/platform/smb_session.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lsw::host::platform {
class FileTable {
public:
    struct Entry {
        std::size_t slot = 0;
        std::uint64_t generation = 0;
        SmbSession::Handle handle = 0;
    };

    struct File {
        std::string path;
        int flags = 0;
        Entry primary;
        std::mutex mutex;
        std::vector<Entry> lanes;
        bool striping = true;
    };

    [[nodiscard]] std::uint64_t add(const Entry& entry, std::string path, int flags);

    [[nodiscard]] std::optional<Entry> find(std::uint64_t id) const;

    [[nodiscard]] std::shared_ptr<File> file(std::uint64_t id) const;

    std::shared_ptr<File> remove(std::uint64_t id);

    [[nodiscard]] std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<File>> entries_;
    std::uint64_t next_ = 1;
};

}  // namespace lsw::host::platform
