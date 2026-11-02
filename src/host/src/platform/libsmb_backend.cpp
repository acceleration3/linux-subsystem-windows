#include "lsw/host/platform/libsmb_backend.hpp"

#include "lsw/host/platform/fd_seal.hpp"
#include "lsw/host/platform/libsmb_session.hpp"

#include <cerrno>
#include <cstdio>
#include <libsmbclient.h>
#include <mutex>

namespace lsw::host::platform {

namespace {

void authenticate(SMBCCTX* context, const char*, const char*, char* workgroup, int workgroup_size,
                  char* user, int user_size, char* password, int password_size) {
    const auto* credentials =
        static_cast<const std::pair<std::string, std::string>*>(smbc_getOptionUserData(context));
    std::snprintf(workgroup, static_cast<std::size_t>(workgroup_size), "WORKGROUP");
    std::snprintf(user, static_cast<std::size_t>(user_size), "%s", credentials->first.c_str());
    std::snprintf(password, static_cast<std::size_t>(password_size), "%s",
                  credentials->second.c_str());
}

std::mutex& setup_mutex() {
    static std::mutex mutex;
    return mutex;
}

}  // namespace

LibsmbBackend::LibsmbBackend(std::string server, std::string share, std::string user,
                             std::string password, int timeout_ms)
    : base_("smb://" + server + "/" + share), credentials_(std::move(user), std::move(password)),
      timeout_ms_(timeout_ms) {}

std::unique_ptr<SmbSession> LibsmbBackend::connect(int& error) {
    const std::lock_guard lock{setup_mutex()};
    SMBCCTX* context = smbc_new_context();
    if (context == nullptr) {
        error = errno != 0 ? errno : ENOMEM;
        return nullptr;
    }
    smbc_setDebug(context, 0);
    smbc_setTimeout(context, timeout_ms_);
    smbc_setOptionFullTimeNames(context, 1);
    smbc_setOptionUseKerberos(context, 0);
    smbc_setOptionNoAutoAnonymousLogin(context, 1);
    smbc_setOptionUrlEncodeReaddirEntries(context, 0);
    smbc_setOptionUserData(context, &credentials_);
    smbc_setFunctionAuthDataWithContext(context, authenticate);
    if (smbc_init_context(context) == nullptr) {
        error = errno != 0 ? errno : EIO;
        smbc_free_context(context, 1);
        return nullptr;
    }
    auto session = std::make_unique<LibsmbSession>(context, base_);
    SmbAttr root;
    if (const int r = session->stat("/", root); r < 0) {
        error = -r;
        return nullptr;
    }
    FdSeal::close_on_exec();
    return session;
}

}  // namespace lsw::host::platform
