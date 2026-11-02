#pragma once

#include "lsw/host/provision/setup_stage.hpp"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace lsw::host::provision {
class SetupPlanParser {
public:
    SetupPlanParser() = delete;

    [[nodiscard]] static QVector<SetupStage> parse(const QByteArray& json,
                                                   QString* error_out = nullptr);

    [[nodiscard]] static QString resolve_script_ref(const QString& ref);
};

}
