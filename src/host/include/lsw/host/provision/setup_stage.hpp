#pragma once

#include "lsw/host/provision/setup_step.hpp"

#include <QVector>

namespace lsw::host::provision {

// A barrier in the setup plan: stages run sequentially, and every step
// inside one stage is dispatched in parallel.
struct SetupStage {
    QVector<SetupStep> steps;
};

}  // namespace lsw::host::provision
