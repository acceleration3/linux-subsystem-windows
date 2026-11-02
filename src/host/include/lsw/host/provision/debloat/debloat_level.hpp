#pragma once

#include <QString>
#include <QStringList>

namespace lsw::host::provision::debloat {
enum class DebloatLevel {
    None,
    Sane,
    Maximum,
};

class DebloatPlan {
public:
    explicit DebloatPlan(DebloatLevel level);

    [[nodiscard]] DebloatLevel level() const noexcept { return level_; }

    [[nodiscard]] const QStringList& appx_prefixes() const noexcept { return appx_prefixes_; }

    [[nodiscard]] const QStringList& paths() const noexcept { return paths_; }

    [[nodiscard]] const QStringList& component_prefixes() const noexcept {
        return component_prefixes_;
    }

    [[nodiscard]] const QStringList& winsxs_keep() const noexcept { return winsxs_keep_; }

    [[nodiscard]] const QStringList& driver_remove() const noexcept { return driver_remove_; }

    [[nodiscard]] QString summary() const;

    [[nodiscard]] QString warning() const;
    [[nodiscard]] QString warning_detail() const;

    [[nodiscard]] static QString to_string(DebloatLevel level);
    [[nodiscard]] static DebloatLevel from_string(const QString& text);

private:
    DebloatLevel level_;
    QStringList appx_prefixes_;
    QStringList paths_;
    QStringList component_prefixes_;
    QStringList winsxs_keep_;
    QStringList driver_remove_;
};

}
