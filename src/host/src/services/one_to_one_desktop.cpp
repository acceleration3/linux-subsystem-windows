#include "lsw/host/services/one_to_one_desktop.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/services/domain_inputs.hpp"

#include <utility>

namespace lsw::host::services {

namespace {

using K = config::ManagerSettings::Key;

}

OneToOneDesktop::OneToOneDesktop(config::ManagerSettings& settings, Guest guest, QObject* parent,
                                 int settle_ms)
    : QObject(parent), settings_(settings), guest_(std::move(guest)), settle_(this) {
    settle_.setSingleShot(true);
    settle_.setInterval(settle_ms);
    connect(&settle_, &QTimer::timeout, this, &OneToOneDesktop::follow);
    connect(&settings_, &config::ManagerSettings::changed, this, [this](const QString& key) {
        if (key != K::kOneToOneDesktop) {
            return;
        }
        if (enabled()) {
            follow();
        } else {
            settle_.stop();
            restore();
        }
    });
}

bool OneToOneDesktop::enabled() const {
    return settings_.bool_value(K::kOneToOneDesktop);
}

std::optional<OneToOneDesktop::Mode> OneToOneDesktop::saved() const {
    return config::DisplayModes::parse(settings_.string_value(K::kDesktopRestore));
}

QString OneToOneDesktop::modes(const std::optional<Mode>& output) {
    output_ = output;
    if (target_) {
        return config::DisplayModes::property({target_->text()});
    }
    return display_modes_from_settings(settings_, output_);
}

void OneToOneDesktop::opened() {
    open_ = true;
    room_.reset();
}

void OneToOneDesktop::resized(const QSize& pixels) {
    if (!open_ || pixels.isEmpty()) {
        return;
    }
    room_ = Mode{pixels.width(), pixels.height()};
    if (enabled()) {
        settle_.start();
    }
}

void OneToOneDesktop::closed() {
    open_ = false;
    room_.reset();
    settle_.stop();
    restore();
}

void OneToOneDesktop::guest_ready() {
    if (target_) {
        return;
    }
    if (saved()) {
        LSW_INFO("1:1 desktop: the guest was left at a desktop window's size; putting {} back",
                 saved()->text().toStdString());
        restore();
    }
    follow();
}

void OneToOneDesktop::follow() {
    if (!open_ || !room_ || !enabled() || asking_) {
        return;
    }
    const Mode wanted = config::DisplayModes::clamped(*room_);
    if (target_ == wanted) {
        return;
    }
    if (saved()) {
        apply(wanted);
        return;
    }
    asking_ = true;
    ask(std::nullopt, [this](const std::optional<Mode>& now) {
        asking_ = false;
        if (!now) {
            LSW_WARN("1:1 desktop: the guest did not say its resolution; it keeps it");
            return;
        }
        if (!open_ || !room_ || !enabled()) {
            return;
        }
        if (!target_ && config::DisplayModes::clamped(*room_) == *now) {
            return;
        }
        if (!saved()) {
            settings_.set_value(K::kDesktopRestore, now->text());
        }
        follow();
    });
}

void OneToOneDesktop::apply(const Mode& wanted) {
    const std::optional<Mode> before = std::exchange(target_, wanted);
    if (!guest_.offer(modes(output_))) {
        target_ = before;
        return;
    }
    LSW_INFO("1:1 desktop: the guest's desktop is now {}, the desktop window's {}x{} pixels",
             wanted.text().toStdString(), room_->width, room_->height);
    ask(wanted, [wanted](const std::optional<Mode>& now) {
        if (now != wanted) {
            LSW_WARN("1:1 desktop: Windows did not switch to {}", wanted.text().toStdString());
        }
    });
}

void OneToOneDesktop::restore() {
    const std::optional<Mode> before = saved();
    if (!target_ && !before) {
        return;
    }
    target_.reset();
    if (!guest_.offer(modes(output_))) {
        return;
    }
    if (!before) {
        return;
    }
    LSW_INFO("1:1 desktop: the configured resolutions are back; Windows returns to {}",
             before->text().toStdString());
    ask(before, [this](const std::optional<Mode>& now) {
        if (now && !target_) {
            forget();
        }
    });
}

void OneToOneDesktop::ask(const std::optional<Mode>& wanted, Answer done) {
    if (busy_) {
        if (const std::optional<Switch> dropped = std::exchange(waiting_, Switch{wanted, std::move(done)})) {
            dropped->done(std::nullopt);
        }
        return;
    }
    busy_ = true;
    guest_.resolution(wanted, [this, done = std::move(done)](const std::optional<Mode>& now) {
        busy_ = false;
        if (const std::optional<Switch> next = std::exchange(waiting_, std::nullopt)) {
            ask(next->wanted, next->done);
        }
        done(now);
    });
}

void OneToOneDesktop::shutdown() {
    open_ = false;
    room_.reset();
    settle_.stop();
    const std::optional<Mode> before = saved();
    if (!target_ && !before) {
        return;
    }
    target_.reset();
    if (guest_.offer(modes(output_)) && before && guest_.resolution_now(before)) {
        forget();
    }
}

void OneToOneDesktop::forget() {
    settings_.set_value(K::kDesktopRestore, QString());
}

}
