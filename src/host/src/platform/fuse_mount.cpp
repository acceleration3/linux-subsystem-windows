#include "lsw/host/platform/fuse_mount.hpp"

#include "lsw/host/platform/fd_seal.hpp"

#define FUSE_USE_VERSION 312
#include <cerrno>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <mutex>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>

extern char** environ;

namespace lsw::host::platform {

struct FuseMount::State {
    std::shared_ptr<WindowsFsStack> stack;
    Options options;
    fuse* handle = nullptr;
    std::mutex mutex;
    std::condition_variable done_cv;
    bool started = false;
    bool finished = false;
    std::thread thread;

    WindowsFs& fs() { return stack->fs(); }
};

namespace {

FuseMount::State* state() {
    return static_cast<FuseMount::State*>(fuse_get_context()->private_data);
}

int op_getattr(const char* path, struct stat* st, fuse_file_info*) {
    return state()->fs().getattr(path, *st);
}

int op_readdir(const char* path, void* buf, fuse_fill_dir_t fill, off_t, fuse_file_info*,
               fuse_readdir_flags flags) {
    const fuse_fill_dir_flags plus =
        (flags & FUSE_READDIR_PLUS) != 0 ? FUSE_FILL_DIR_PLUS : static_cast<fuse_fill_dir_flags>(0);
    return state()->fs().readdir(path, [&](const std::string& name, const struct stat* st) {
        fill(buf, name.c_str(), st, 0, st != nullptr ? plus : static_cast<fuse_fill_dir_flags>(0));
    });
}

int op_open(const char* path, fuse_file_info* fi) {
    std::uint64_t fh = 0;
    const int r = state()->fs().open(path, fi->flags, fh);
    if (r == 0) {
        fi->fh = fh;
        fi->keep_cache = 0;
    }
    return r;
}

int op_create(const char* path, mode_t, fuse_file_info* fi) {
    std::uint64_t fh = 0;
    const int r = state()->fs().create(path, fi->flags, fh);
    if (r == 0) {
        fi->fh = fh;
        fi->keep_cache = 0;
    }
    return r;
}

int op_read(const char*, char* buf, size_t size, off_t offset, fuse_file_info* fi) {
    return static_cast<int>(
        state()->fs().read(fi->fh, static_cast<std::uint64_t>(offset), buf, size));
}

int op_write(const char*, const char* buf, size_t size, off_t offset, fuse_file_info* fi) {
    return static_cast<int>(
        state()->fs().write(fi->fh, static_cast<std::uint64_t>(offset), buf, size));
}

int op_fsync(const char*, int, fuse_file_info*) {
    return 0;
}

int op_release(const char*, fuse_file_info* fi) {
    return state()->fs().release(fi->fh);
}

int op_truncate(const char* path, off_t size, fuse_file_info* fi) {
    if (fi != nullptr) {
        return state()->fs().ftruncate(fi->fh, static_cast<std::uint64_t>(size));
    }
    return state()->fs().truncate(path, static_cast<std::uint64_t>(size));
}

int op_unlink(const char* path) {
    return state()->fs().unlink(path);
}

int op_rename(const char* from, const char* to, unsigned flags) {
    if ((flags & RENAME_EXCHANGE) != 0) {
        return -EINVAL;
    }
    return state()->fs().rename(from, to, (flags & RENAME_NOREPLACE) != 0);
}

int op_mkdir(const char* path, mode_t) {
    return state()->fs().mkdir(path);
}

int op_rmdir(const char* path) {
    return state()->fs().rmdir(path);
}

int op_utimens(const char* path, const timespec tv[2], fuse_file_info*) {
    return state()->fs().utimens(path, tv);
}

int op_statfs(const char* path, struct statvfs* out) {
    return state()->fs().statfs(path, *out);
}

int op_chmod(const char*, mode_t, fuse_file_info*) {
    return 0;
}

int op_chown(const char*, uid_t, gid_t, fuse_file_info*) {
    return 0;
}

void* op_init(fuse_conn_info* conn, fuse_config* cfg) {
    FuseMount::State* s = state();
    conn->max_write = 1U << 20;
    conn->max_read = 1U << 20;
    conn->want |= FUSE_CAP_READDIRPLUS;
    conn->want &= ~static_cast<unsigned>(FUSE_CAP_READDIRPLUS_AUTO);
    conn->max_readahead = 1U << 20;
    cfg->kernel_cache = 0;
    cfg->auto_cache = 0;
    cfg->use_ino = 0;
    cfg->attr_timeout = s->options.timeout;
    cfg->entry_timeout = s->options.timeout;
    cfg->negative_timeout = s->options.timeout;
    return s;
}

const fuse_operations& operations() {
    static const fuse_operations ops = [] {
        fuse_operations o{};
        o.init = op_init;
        o.getattr = op_getattr;
        o.readdir = op_readdir;
        o.open = op_open;
        o.create = op_create;
        o.read = op_read;
        o.write = op_write;
        o.fsync = op_fsync;
        o.release = op_release;
        o.truncate = op_truncate;
        o.unlink = op_unlink;
        o.rename = op_rename;
        o.mkdir = op_mkdir;
        o.rmdir = op_rmdir;
        o.utimens = op_utimens;
        o.statfs = op_statfs;
        o.chmod = op_chmod;
        o.chown = op_chown;
        return o;
    }();
    return ops;
}

void serve(const std::shared_ptr<FuseMount::State>& s) {
    fuse_loop_config* config = fuse_loop_cfg_create();
    fuse_loop_cfg_set_max_threads(config, static_cast<unsigned>(s->options.max_threads));
    fuse_loop_cfg_set_clone_fd(config, 1);
    fuse_loop_mt(s->handle, config);
    fuse_loop_cfg_destroy(config);
    fuse* f = nullptr;
    {
        const std::lock_guard lock{s->mutex};
        f = std::exchange(s->handle, nullptr);
    }
    fuse_unmount(f);
    fuse_destroy(f);
    {
        const std::lock_guard lock{s->mutex};
        s->finished = true;
    }
    s->done_cv.notify_all();
}

}  // namespace

FuseMount::FuseMount(std::shared_ptr<WindowsFsStack> stack, Options options)
    : state_(std::make_shared<State>()) {
    state_->stack = std::move(stack);
    state_->options = std::move(options);
}

FuseMount::~FuseMount() {
    stop(std::chrono::milliseconds{5000});
}

bool FuseMount::start(std::string& error) {
    State& s = *state_;
    const std::string mount_options =
        "auto_unmount,max_read=1048576,fsname=" + s.options.fsname + ",subtype=lsw-windows-fs";
    fuse_args args = FUSE_ARGS_INIT(0, nullptr);
    fuse_opt_add_arg(&args, "lsw-windows-fs");
    fuse_opt_add_arg(&args, "-o");
    fuse_opt_add_arg(&args, mount_options.c_str());
    fuse* f = fuse_new(&args, &operations(), sizeof(fuse_operations), &s);
    fuse_opt_free_args(&args);
    if (f == nullptr) {
        error = "the FUSE session could not be created";
        return false;
    }
    if (fuse_mount(f, s.options.mount_point.c_str()) != 0) {
        fuse_destroy(f);
        error = "fusermount3 could not mount " + s.options.mount_point;
        return false;
    }
    FdSeal::close_on_exec();
    s.handle = f;
    s.started = true;
    s.thread = std::thread{[st = state_] { serve(st); }};
    return true;
}

bool FuseMount::running() const {
    const std::lock_guard lock{state_->mutex};
    return state_->started && !state_->finished;
}

int FuseMount::unmount_path(const std::string& mount_point, bool lazy,
                            std::chrono::milliseconds wait) {
    const char* argv[] = {"fusermount3", lazy ? "-uz" : "-u", "-q",
                          "--",          mount_point.c_str(), nullptr};
    pid_t pid = 0;
    if (posix_spawnp(&pid, "fusermount3", nullptr, nullptr, const_cast<char* const*>(argv),
                     environ) != 0) {
        return -1;
    }
    const auto deadline = std::chrono::steady_clock::now() + wait;
    for (;;) {
        int status = 0;
        const pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
        if (done < 0 && errno != EINTR) {
            return -1;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}

bool FuseMount::stop(std::chrono::milliseconds wait) {
    State& s = *state_;
    {
        const std::lock_guard lock{s.mutex};
        if (!s.started) {
            return true;
        }
        if (s.handle != nullptr) {
            fuse_exit(s.handle);
        }
    }
    const auto deadline = std::chrono::steady_clock::now() + wait;
    std::unique_lock lock{s.mutex};
    if (!s.finished) {
        lock.unlock();
        const auto slice = wait / 2;
        if (unmount_path(s.options.mount_point, false, slice) != 0) {
            unmount_path(s.options.mount_point, true, slice);
        }
        lock.lock();
    }
    const bool done = s.done_cv.wait_until(lock, deadline, [&s] { return s.finished; });
    lock.unlock();
    if (s.thread.joinable()) {
        if (done) {
            s.thread.join();
        } else {
            s.thread.detach();
        }
    }
    return done;
}

}  // namespace lsw::host::platform
