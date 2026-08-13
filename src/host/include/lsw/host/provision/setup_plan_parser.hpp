#pragma once

#include "lsw/host/provision/setup_stage.hpp"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace lsw::host::provision {

// Turns the embedded ``setup-steps.json`` into a stage list, and
// resolves the script references it points at.
//
// Separate from the Pipeline that executes the plan: parsing is a pure
// function of its input and is the part worth testing on its own.
class SetupPlanParser {
public:
    SetupPlanParser() = delete;

    // Parse a setup-steps.json blob. Returns an empty list on parse
    // error; ``error_out`` (if non-null) receives the message.
    [[nodiscard]] static QVector<SetupStage> parse(const QByteArray& json,
                                                   QString* error_out = nullptr);

    // Resolve ``script_ref`` (e.g. "tune-windows") to the embedded .cmd
    // body. Returns an empty string for unknown refs.
    [[nodiscard]] static QString resolve_script_ref(const QString& ref);
};

}  // namespace lsw::host::provision
