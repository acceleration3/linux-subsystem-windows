#pragma once

#include "lsw/host/platform/command_runner.hpp"

#include <QString>
#include <QStringList>

namespace lsw::host::platform {
class IndexerExclusions {
public:
    IndexerExclusions(CommandRunner& runner, QString home);

    void apply(const QString& path);

    [[nodiscard]] static QStringList parse_baloo(const QString& value);

    [[nodiscard]] static QStringList parse_tracker(const QString& value);

    [[nodiscard]] static QString format_tracker(const QStringList& values);

    [[nodiscard]] static QStringList merge(const QStringList& existing, const QString& path,
                                           const QString& home, bool& changed);

private:
    void apply_baloo(const QString& path);
    void apply_tracker(const QString& path);

    CommandRunner& runner_;
    QString home_;
};

}  // namespace lsw::host::platform
