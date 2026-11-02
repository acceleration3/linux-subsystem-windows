#include "lsw/host/platform/smb_errno.hpp"

#include <cerrno>

namespace lsw::host::platform {

bool SmbErrno::connection_lost(int error) {
    switch (error) {
    case EIO:
    case ETIMEDOUT:
    case ECONNRESET:
    case ECONNREFUSED:
    case ECONNABORTED:
    case EPIPE:
    case ENOTCONN:
    case EHOSTUNREACH:
    case EHOSTDOWN:
    case ENETUNREACH:
    case ENETDOWN:
    case ENODEV:
        return true;
    default:
        return false;
    }
}

int SmbErrno::map(int error) {
    if (error < 0) {
        error = -error;
    }
    if (error == 0) {
        return EIO;
    }
    if (connection_lost(error)) {
        return EIO;
    }
    switch (error) {
    case EREMOTEIO:
    case EAGAIN:
    case ENOMEM:
        return EIO;
    case ENOTSUP:
        return ENOSYS;
    default:
        return error;
    }
}

}  // namespace lsw::host::platform
