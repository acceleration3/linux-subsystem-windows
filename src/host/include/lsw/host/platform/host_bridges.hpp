#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace lsw::host::platform {
class HostBridges {
public:
    HostBridges() = delete;

    struct Rule {
        bool allow = false;
        QString bridge;
    };

    struct Facts {
        QStringList bridges;
        QStringList helper_places;
        QString helper;
        bool helper_privileged = false;
        QString rules_file;
        bool rules_readable = false;
        QVector<Rule> rules;
    };

    struct Places {
        QString interfaces;
        QStringList helpers;
        QString rules_file;
    };

    [[nodiscard]] static Places system_places();
    [[nodiscard]] static Facts probe(const Places& places = system_places());

    [[nodiscard]] static QString missing(const Facts& facts, const QString& bridge);

private:
    static bool read_rules(const QString& path, QVector<Rule>& rules, int depth);
};

}
