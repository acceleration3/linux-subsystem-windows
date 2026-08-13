#pragma once

#include <QByteArray>
#include <QObject>

#include <cstdint>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::manager {

// Fetches a guest window's icon as PNG bytes.
//
// Runs in parallel with the props fetch, but the mirror waits for both
// before showing: on Wayland the compositor resolves a toplevel's icon
// by matching its app_id to a .desktop file AT MAP TIME and caches the
// answer, so an icon that arrives after the surface is mapped is simply
// ignored and the mirror keeps a generic one forever.
class GuestWindowIconFetcher : public QObject {
    Q_OBJECT
public:
    GuestWindowIconFetcher(link::AgentRpcClient& rpc, std::uint64_t hwnd,
                           QObject* parent = nullptr);

    void start();

Q_SIGNALS:
    // Always emitted exactly once. ``png`` is empty when the guest had
    // no icon or the RPC failed — the mirror still shows, just without
    // a custom icon.
    void ready(const QByteArray& png);

private:
    link::AgentRpcClient& rpc_;
    std::uint64_t hwnd_;
};

}  // namespace lsw::host::manager
