#pragma once

#include <QObject>

namespace lsw::host::provision {

Q_NAMESPACE

enum class PipelineStep {
    DownloadingIso,
    TrimmingImage,
    BuildingPayloadIso,
    CreatingDisk,
    StartingVm,
    WaitingForAgent,
    RunningSetup,
    ShuttingDown,
    StartingVmVirtio,
    WaitingForAgentAfterSwap,
};
Q_ENUM_NS(PipelineStep)

}
