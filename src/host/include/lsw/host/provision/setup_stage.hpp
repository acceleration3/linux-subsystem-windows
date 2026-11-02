#pragma once

#include "lsw/host/provision/setup_step.hpp"

#include <QVector>

namespace lsw::host::provision {
struct SetupStage {
    QVector<SetupStep> steps;
};

}
