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

    struct Bridge {
        QString name;
        QString state;
        int members = 0;
    };

    struct Facts {
        QStringList bridges;
        QVector<Bridge> details;
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

    struct Requirement {
        enum class State { Met, Unmet, Unknown };

        State state = State::Unknown;
        QString text;
        QString detail;
        QString fix;
    };

    [[nodiscard]] static QVector<Requirement> readiness(const Facts& facts, const QString& bridge);

private:
    static bool read_rules(const QString& path, QVector<Rule>& rules, int depth);
};

}  // namespace lsw::host::platform
