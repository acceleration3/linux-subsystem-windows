#pragma once

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/config/vm_sizing.hpp"
#include "lsw/host/vm/domain_xml_inputs.hpp"

namespace lsw::host::services {

// The domain definition's half that comes from settings, built in one
// place.
//
// Whoever rewrites the domain — the manager applying a restart, the
// pipeline defining it the first time — has to arrive at the same XML
// for the same settings, or the next redefine quietly undoes the last
// one. Each of them assembling DomainXmlInputs by hand is how a share
// or a video memory size ends up in one definition and not the other.

// Everything ``DomainXmlInputs`` takes from settings: the virtio-win
// ISO where the pipeline caches it, the adapter's video memory for the
// configured resolution, and the shared folders. The shared-memory
// fields are left at their defaults; Lifecycle fills those in.
[[nodiscard]] vm::DomainXmlInputs
domain_inputs_from_settings(const config::Settings& settings,
                            const config::ManagerSettings& manager_settings);

// Cores, memory and disk as the settings have them.
[[nodiscard]] config::VmSizing
vm_sizing_from_settings(const config::ManagerSettings& manager_settings);

// Video memory for a ``width`` x ``height`` mode, in KiB. The adapter's
// memory bounds the modes the guest can set, so it follows the
// resolution rather than capping it: 32 bits per pixel, doubled for a
// back buffer, plus a MiB of slack, and never below the 16 MiB QEMU's
// VGA wants anyway.
[[nodiscard]] int video_vram_kib_for(int width, int height);

}  // namespace lsw::host::services
