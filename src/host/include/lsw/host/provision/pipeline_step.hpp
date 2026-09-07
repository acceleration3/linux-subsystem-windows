#pragma once

#include <QObject>

namespace lsw::host::provision {

Q_NAMESPACE

// Coarse-grained phases of a provisioning run, in the order they occur.
//
// Per-step detail inside RunningSetup is reported separately, because a
// setup stage has its own rows, names and outcomes that the UI shows
// independently of the phase it belongs to.
enum class PipelineStep {
    DownloadingIso,            // skipped when a local ISO was supplied
    TrimmingImage,             // pulls install.wim out and debloats it
    BuildingPayloadIso,        // also fetches the latest virtio-win.iso
    CreatingDisk,
    StartingVm,
    WaitingForAgent,           // the press-any-key prompter runs alongside
    RunningSetup,
    ShuttingDown,
    StartingVmVirtio,
    WaitingForAgentAfterSwap,
};
Q_ENUM_NS(PipelineStep)

}  // namespace lsw::host::provision
