#pragma once

#include "lsw/host/manager/guest_window_props.hpp"

#include <QObject>

#include <cstdint>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::manager {

// Fetches a window's properties off the GUI thread and reports them once.
//
// The RPC blocks, and it happens during mirror construction — doing it
// inline would stall the manager every time a guest window appears.
class GuestWindowPropsFetcher : public QObject {
    Q_OBJECT
public:
    GuestWindowPropsFetcher(link::AgentRpcClient& rpc, std::uint64_t hwnd,
                            QObject* parent = nullptr);

    void start();

Q_SIGNALS:
    // Always emitted exactly once, even on RPC or parse failure — in
    // which case ``props.valid`` is false and the mirror falls back to
    // image-driven sizing. The show must not hang on a failed probe.
    void ready(const GuestWindowProps& props);

private:
    link::AgentRpcClient& rpc_;
    std::uint64_t hwnd_;
};

}  // namespace lsw::host::manager
