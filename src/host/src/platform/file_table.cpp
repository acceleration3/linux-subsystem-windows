#include "lsw/host/platform/file_table.hpp"

namespace lsw::host::platform {

std::uint64_t FileTable::add(const Entry& entry, std::string path, int flags) {
    auto file = std::make_shared<File>();
    file->path = std::move(path);
    file->flags = flags;
    file->primary = entry;
    const std::lock_guard lock{mutex_};
    const std::uint64_t id = next_++;
    entries_.emplace(id, std::move(file));
    return id;
}

std::optional<FileTable::Entry> FileTable::find(std::uint64_t id) const {
    if (const auto found = file(id)) {
        return found->primary;
    }
    return std::nullopt;
}

std::shared_ptr<FileTable::File> FileTable::file(std::uint64_t id) const {
    const std::lock_guard lock{mutex_};
    const auto it = entries_.find(id);
    return it == entries_.end() ? nullptr : it->second;
}

std::shared_ptr<FileTable::File> FileTable::remove(std::uint64_t id) {
    const std::lock_guard lock{mutex_};
    const auto it = entries_.find(id);
    if (it == entries_.end()) {
        return nullptr;
    }
    auto file = std::move(it->second);
    entries_.erase(it);
    return file;
}

std::size_t FileTable::size() const {
    const std::lock_guard lock{mutex_};
    return entries_.size();
}

}  // namespace lsw::host::platform
