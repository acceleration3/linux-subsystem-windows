#pragma once

#include "lsw/host/manager/nav_rail.hpp"

#include <QWidget>

#include <array>

class QLabel;

namespace lsw::host::config { class PendingChanges; }

namespace lsw::host::manager {
class ApplyBar : public QWidget {
    Q_OBJECT
public:
    using Counts = std::array<int, static_cast<int>(NavRail::Page::Count)>;

    explicit ApplyBar(config::PendingChanges& pending, QWidget* parent = nullptr);

    [[nodiscard]] static NavRail::Page page_of(const QString& key);

    [[nodiscard]] Counts counts() const;

Q_SIGNALS:
    void restart_requested();
    void counts_changed();

private:
    void refresh();

    config::PendingChanges& pending_;
    QLabel* count_ = nullptr;
    QLabel* diffs_ = nullptr;
};

}
