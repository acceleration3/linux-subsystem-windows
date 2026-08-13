#include "lsw/protocol/fd_frame_channel.hpp"

#include <unistd.h>

namespace lsw::protocol {

FdFrameChannel::~FdFrameChannel() {
    close();
}

void FdFrameChannel::close() noexcept {
    if (io_.is_open()) {
        ::close(io_.fd());
        io_.set_fd(-1);
    }
}

}  // namespace lsw::protocol
