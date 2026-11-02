#pragma once

#include "lsw/host/config/display_modes.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/config/vm_sizing.hpp"
#include "lsw/host/vm/domain_xml_inputs.hpp"

#include <optional>

namespace lsw::host::services {
[[nodiscard]] vm::DomainXmlInputs
domain_inputs_from_settings(const config::Settings& settings,
                            const config::ManagerSettings& manager_settings,
                            std::optional<quint32> monitor_refresh_millihz = std::nullopt);

[[nodiscard]] std::optional<quint32>
display_refresh_from_settings(const config::ManagerSettings& manager_settings,
                              std::optional<quint32> monitor = std::nullopt);

[[nodiscard]] config::VmSizing
vm_sizing_from_settings(const config::ManagerSettings& manager_settings);

[[nodiscard]] QString
display_modes_from_settings(const config::ManagerSettings& manager_settings,
                            const std::optional<config::DisplayMode>& output = std::nullopt);

}
