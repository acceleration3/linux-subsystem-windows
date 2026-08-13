#include "lsw/protocol/posix_shm_region.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace lsw::protocol {

namespace {

[[nodiscard]] std::string shm_path(const std::string& name) {
    // POSIX wants a leading slash and no others; accept both spellings
    // from callers so a URL's path can be passed through unchanged.
    return name.empty() || name.front() == '/' ? name : "/" + name;
}

}  // namespace

PosixShmRegion::~PosixShmRegion() { close(); }

int PosixShmRegion::create(const std::string& name, std::size_t size) {
    close();
    name_ = shm_path(name);
    // A region left behind by a crashed run would otherwise make every
    // subsequent start fail with EEXIST.
    ::shm_unlink(name_.c_str());
    fd_ = ::shm_open(name_.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd_ < 0) {
        const int err = -errno;
        name_.clear();
        return err;
    }
    owns_name_ = true;
    if (::ftruncate(fd_, static_cast<off_t>(size)) != 0) {
        const int err = -errno;
        close();
        return err;
    }
    base_ = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (base_ == MAP_FAILED) {
        const int err = -errno;
        base_ = nullptr;
        close();
        return err;
    }
    size_ = size;
    return 0;
}

int PosixShmRegion::open(const std::string& name) {
    close();
    name_ = shm_path(name);
    fd_ = ::shm_open(name_.c_str(), O_RDWR, 0600);
    if (fd_ < 0) {
        const int err = -errno;
        name_.clear();
        return err;
    }
    struct stat st {};
    if (::fstat(fd_, &st) != 0 || st.st_size <= 0) {
        const int err = -errno;
        close();
        return err != 0 ? err : -EINVAL;
    }
    const auto size = static_cast<std::size_t>(st.st_size);
    base_ = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (base_ == MAP_FAILED) {
        const int err = -errno;
        base_ = nullptr;
        close();
        return err;
    }
    size_ = size;
    return 0;
}

void PosixShmRegion::close() noexcept {
    if (base_ != nullptr) {
        ::munmap(base_, size_);
        base_ = nullptr;
    }
    size_ = 0;
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (owns_name_ && !name_.empty()) {
        ::shm_unlink(name_.c_str());
    }
    owns_name_ = false;
    name_.clear();
}

}  // namespace lsw::protocol
