#pragma once

namespace lsw::suites {
class MachineLock {
public:
    MachineLock();
    MachineLock(const MachineLock&) = delete;
    MachineLock& operator=(const MachineLock&) = delete;
    ~MachineLock();

    void lock();

    void unlock();

private:
    void* handle_ = nullptr;
};
}  // namespace lsw::suites
