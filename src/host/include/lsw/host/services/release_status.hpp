#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

namespace lsw::host::services {
struct ReleaseStatus {
    enum class State {
        Unchecked,
        Checking,
        Latest,
        Newer,
        Failed,
    };

    struct Release {
        QString version;
        QString published;
        QUrl page;
        QStringList headlines;
        [[nodiscard]] bool operator==(const Release&) const = default;
    };

    State state = State::Unchecked;
    QVector<Release> newer;
    QString error;
    QString checked;

    [[nodiscard]] bool operator==(const ReleaseStatus&) const = default;
};

}
