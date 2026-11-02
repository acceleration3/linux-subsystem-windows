#pragma once

#include "lsw/host/platform/smb_backend.hpp"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <set>

namespace fake {

using lsw::host::platform::SmbAttr;
using lsw::host::platform::SmbDirEntry;
using lsw::host::platform::SmbSession;

struct Store {
    struct Node {
        bool directory = false;
        std::string data;
        timespec mtime{1700000000, 0};
    };

    std::mutex mutex;
    std::map<std::string, Node> nodes;
    std::atomic<int> connects{0};
    std::atomic<int> live{0};
    std::atomic<int> fail_connect{0};
    std::atomic<int> break_next{0};
    std::atomic<int> opens{0};
    std::atomic<int> closes{0};
    std::set<std::size_t> threads_seen;

    Store() { nodes["/"].directory = true; }

    void add_dir(const std::string& path) {
        const std::lock_guard lock{mutex};
        nodes[path].directory = true;
    }

    void add_file(const std::string& path, std::string data) {
        const std::lock_guard lock{mutex};
        nodes[path].data = std::move(data);
    }

    std::string content(const std::string& path) {
        const std::lock_guard lock{mutex};
        const auto it = nodes.find(path);
        return it == nodes.end() ? std::string{} : it->second.data;
    }

    bool exists(const std::string& path) {
        const std::lock_guard lock{mutex};
        return nodes.count(path) != 0;
    }
};

class Session : public SmbSession {
public:
    explicit Session(Store& store) : store_(store) { ++store_.live; }

    ~Session() override { --store_.live; }

    int stat(const std::string& path, SmbAttr& out) override {
        if (const int r = injected()) {
            return r;
        }
        const std::lock_guard lock{store_.mutex};
        const auto it = store_.nodes.find(path);
        if (it == store_.nodes.end()) {
            return -ENOENT;
        }
        out = attr(it->second);
        return 0;
    }

    int list(const std::string& path, std::vector<SmbDirEntry>& out) override {
        if (const int r = injected()) {
            return r;
        }
        const std::lock_guard lock{store_.mutex};
        const auto dir = store_.nodes.find(path);
        if (dir == store_.nodes.end()) {
            return -ENOENT;
        }
        if (!dir->second.directory) {
            return -ENOTDIR;
        }
        out.push_back({".", attr(dir->second)});
        out.push_back({"..", attr(dir->second)});
        const std::string prefix = path == "/" ? "/" : path + "/";
        for (const auto& [name, node] : store_.nodes) {
            if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0 &&
                name.find('/', prefix.size()) == std::string::npos) {
                out.push_back({name.substr(prefix.size()), attr(node)});
            }
        }
        return 0;
    }

    int open(const std::string& path, int flags, Handle& handle) override {
        if (const int r = injected()) {
            return r;
        }
        const std::lock_guard lock{store_.mutex};
        auto it = store_.nodes.find(path);
        if (it == store_.nodes.end()) {
            if ((flags & O_CREAT) == 0) {
                return -ENOENT;
            }
            it = store_.nodes.emplace(path, Store::Node{}).first;
        } else if ((flags & O_CREAT) != 0 && (flags & O_EXCL) != 0) {
            return -EEXIST;
        }
        if (it->second.directory) {
            return -EISDIR;
        }
        if ((flags & O_TRUNC) != 0) {
            it->second.data.clear();
        }
        handle = next_++;
        files_[handle] = path;
        ++store_.opens;
        return 0;
    }

    std::int64_t read(Handle handle, std::uint64_t offset, char* buffer,
                      std::size_t size) override {
        if (const int r = injected()) {
            return r;
        }
        const std::lock_guard lock{store_.mutex};
        const auto it = files_.find(handle);
        if (it == files_.end()) {
            return -EBADF;
        }
        const std::string& data = store_.nodes[it->second].data;
        if (offset >= data.size()) {
            return 0;
        }
        const std::size_t n = std::min<std::size_t>(size, data.size() - offset);
        std::memcpy(buffer, data.data() + offset, n);
        return static_cast<std::int64_t>(n);
    }

    std::int64_t write(Handle handle, std::uint64_t offset, const char* buffer,
                       std::size_t size) override {
        if (const int r = injected()) {
            return r;
        }
        const std::lock_guard lock{store_.mutex};
        const auto it = files_.find(handle);
        if (it == files_.end()) {
            return -EBADF;
        }
        std::string& data = store_.nodes[it->second].data;
        if (data.size() < offset + size) {
            data.resize(offset + size);
        }
        std::memcpy(data.data() + offset, buffer, size);
        return static_cast<std::int64_t>(size);
    }

    int truncate(Handle handle, std::uint64_t size) override {
        const std::lock_guard lock{store_.mutex};
        const auto it = files_.find(handle);
        if (it == files_.end()) {
            return -EBADF;
        }
        store_.nodes[it->second].data.resize(size);
        return 0;
    }

    int close(Handle handle) override {
        ++store_.closes;
        return files_.erase(handle) != 0 ? 0 : -EBADF;
    }

    int unlink(const std::string& path) override {
        if (const int r = injected()) {
            return r;
        }
        const std::lock_guard lock{store_.mutex};
        const auto it = store_.nodes.find(path);
        if (it == store_.nodes.end()) {
            return -ENOENT;
        }
        if (it->second.directory) {
            return -EISDIR;
        }
        store_.nodes.erase(it);
        return 0;
    }

    int rmdir(const std::string& path) override {
        const std::lock_guard lock{store_.mutex};
        const auto it = store_.nodes.find(path);
        if (it == store_.nodes.end()) {
            return -ENOENT;
        }
        if (!it->second.directory) {
            return -ENOTDIR;
        }
        const std::string prefix = path + "/";
        for (const auto& [name, node] : store_.nodes) {
            if (name.compare(0, prefix.size(), prefix) == 0) {
                return -ENOTEMPTY;
            }
        }
        store_.nodes.erase(it);
        return 0;
    }

    int mkdir(const std::string& path) override {
        const std::lock_guard lock{store_.mutex};
        if (store_.nodes.count(path) != 0) {
            return -EEXIST;
        }
        store_.nodes[path].directory = true;
        return 0;
    }

    int rename(const std::string& from, const std::string& to) override {
        const std::lock_guard lock{store_.mutex};
        const auto it = store_.nodes.find(from);
        if (it == store_.nodes.end()) {
            return -ENOENT;
        }
        if (store_.nodes.count(to) != 0) {
            return -EEXIST;
        }
        store_.nodes[to] = it->second;
        store_.nodes.erase(from);
        return 0;
    }

    int utimens(const std::string& path, const timespec&, const timespec& mtime) override {
        const std::lock_guard lock{store_.mutex};
        const auto it = store_.nodes.find(path);
        if (it == store_.nodes.end()) {
            return -ENOENT;
        }
        it->second.mtime = mtime;
        return 0;
    }

    int statvfs(const std::string&, struct statvfs& out) override {
        out = {};
        out.f_bsize = 4096;
        out.f_frsize = 4096;
        out.f_blocks = 1000;
        out.f_bfree = 400;
        out.f_bavail = 400;
        out.f_namemax = 255;
        return 0;
    }

private:
    static SmbAttr attr(const Store::Node& node) {
        SmbAttr a;
        a.directory = node.directory;
        a.size = node.data.size();
        a.mtime = node.mtime;
        a.atime = node.mtime;
        a.ctime = node.mtime;
        return a;
    }

    int injected() {
        int expected = store_.break_next.load();
        while (expected > 0 && !store_.break_next.compare_exchange_weak(expected, expected - 1)) {
        }
        return expected > 0 ? -ECONNRESET : 0;
    }

    Store& store_;
    std::map<Handle, std::string> files_;
    Handle next_ = 100;
};

class Backend : public lsw::host::platform::SmbBackend {
public:
    explicit Backend(Store& store) : store_(store) {}

    std::unique_ptr<SmbSession> connect(int& error) override {
        if (const int e = store_.fail_connect.load(); e != 0) {
            error = e;
            return nullptr;
        }
        ++store_.connects;
        return std::make_unique<Session>(store_);
    }

private:
    Store& store_;
};

}  // namespace fake
