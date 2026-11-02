#include "lsw/base/rolling_file.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace lsw::base {

namespace fs = std::filesystem;

RollingFile::RollingFile(fs::path path, Limits limits)
    : path_(std::move(path)), limits_(limits) {
    if (limits_.max_files < 1) {
        limits_.max_files = 1;
    }
}

RollingFile::~RollingFile() {
    const std::lock_guard lock{mutex_};
    close_locked();
}

fs::path RollingFile::numbered(const fs::path& path, int index) {
    if (index <= 0) {
        return path;
    }
    fs::path out = path;
    out.replace_extension();
    out += "." + std::to_string(index);
    out += path.extension();
    return out;
}

bool RollingFile::open_locked() {
    if (fd_ >= 0) {
        return true;
    }
    std::error_code ignored;
    fs::create_directories(path_.parent_path(), ignored);
    fd_ = ::open(path_.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
    if (fd_ < 0) {
        return false;
    }
    struct stat st {};
    size_ = ::fstat(fd_, &st) == 0 ? static_cast<std::uintmax_t>(st.st_size) : 0;
    return true;
}

void RollingFile::close_locked() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    size_ = 0;
}

void RollingFile::roll_locked() {
    close_locked();
    std::error_code ec;
    if (!fs::exists(path_, ec)) {
        return;
    }
    fs::remove(numbered(path_, limits_.max_files - 1), ec);
    for (int i = limits_.max_files - 2; i >= 0; --i) {
        const fs::path from = numbered(path_, i);
        if (fs::exists(from, ec)) {
            fs::rename(from, numbered(path_, i + 1), ec);
        }
    }
}

bool RollingFile::append(std::string_view text) {
    const std::lock_guard lock{mutex_};
    if (!open_locked()) {
        return false;
    }
    if (size_ > 0 && size_ + text.size() > limits_.max_bytes) {
        roll_locked();
        if (!open_locked()) {
            return false;
        }
    }
    const char* data = text.data();
    std::size_t left = text.size();
    while (left > 0) {
        const ssize_t n = ::write(fd_, data, left);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        data += n;
        left -= static_cast<std::size_t>(n);
        size_ += static_cast<std::uintmax_t>(n);
    }
    return true;
}

void RollingFile::roll_now() {
    const std::lock_guard lock{mutex_};
    close_locked();
    std::error_code ec;
    if (!fs::exists(path_, ec) || fs::file_size(path_, ec) == 0) {
        return;
    }
    roll_locked();
}

}
