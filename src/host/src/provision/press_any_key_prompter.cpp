#include "lsw/host/provision/press_any_key_prompter.hpp"

#include "lsw/host/vm/domain_handle.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"

#include <QTimer>
#include <QtConcurrent>
#include <libvirt/libvirt.h>

namespace lsw::host::provision {

namespace {

// KEY_SPACE in linux/input-event-codes.h — the keycode set libvirt
// expects for VIR_KEYCODE_SET_LINUX.
constexpr unsigned int kKeySpace = 57;

}  // namespace

PressAnyKeyPrompter::PressAnyKeyPrompter(vm::LibvirtConnection& libvirt, QObject* parent)
    : QObject(parent), libvirt_(libvirt), timer_(new QTimer(this)) {
    timer_->setInterval(kIntervalMs);
    connect(timer_, &QTimer::timeout, this, &PressAnyKeyPrompter::on_tick);
}

void PressAnyKeyPrompter::start(const QUuid& uuid) {
    uuid_ = uuid;
    ticks_remaining_ = kTicks;
    timer_->start();
}

void PressAnyKeyPrompter::stop() { timer_->stop(); }

void PressAnyKeyPrompter::on_tick() {
    if (--ticks_remaining_ <= 0) {
        timer_->stop();
        return;
    }
    if (uuid_.isNull() || !libvirt_.is_open()) {
        return;
    }
    // virDomainSendKey can block on a busy libvirtd; the tick must not.
    auto* conn = libvirt_.raw();
    const auto uuid_bytes = uuid_.toRfc4122();
    (void)QtConcurrent::run([conn, uuid_bytes]() {
        auto* domain = ::virDomainLookupByUUID(
            conn, reinterpret_cast<const unsigned char*>(uuid_bytes.constData()));
        if (domain == nullptr) {
            return;
        }
        vm::DomainHandle handle{domain};
        unsigned int keycodes[1] = {kKeySpace};
        (void)::virDomainSendKey(domain, VIR_KEYCODE_SET_LINUX, /*holdtime=*/100,
                                 keycodes, /*nkeycodes=*/1, /*flags=*/0);
    });
}

}  // namespace lsw::host::provision
