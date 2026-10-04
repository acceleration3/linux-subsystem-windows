#pragma once

#include "lsw/host/config/display_modes.hpp"
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
// or a GPU setting ends up in one definition and not the other.

// Everything ``DomainXmlInputs`` takes from settings: the shared
// folders, the GPU device (host GPU, render-only GPUs, host window,
// isolation, resolution) and the driver log's path. The host GPUs are enumerated here to check the
// chosen one still exists. ``install_display`` is left false — only the
// pipeline's first define sets it — and the accel-virt path fields
// are left empty; Lifecycle fills those in.
[[nodiscard]] vm::DomainXmlInputs
domain_inputs_from_settings(const config::Settings& settings,
                            const config::ManagerSettings& manager_settings);

// Cores, memory and disk as the settings have them.
[[nodiscard]] config::VmSizing
vm_sizing_from_settings(const config::ManagerSettings& manager_settings);

// The device ``modes`` text the display settings ask for: the stored
// list (empty is the host monitors' own modes), with ``output`` — the
// monitor the manager is on — first when "match the monitor" is on and
// the caller knows it. The same text goes into the definition (where no
// window, so no output, is known) and, on a running VM, through
// Lifecycle::set_display_modes.
[[nodiscard]] QString
display_modes_from_settings(const config::ManagerSettings& manager_settings,
                            const std::optional<config::DisplayMode>& output = std::nullopt);

}  // namespace lsw::host::services
