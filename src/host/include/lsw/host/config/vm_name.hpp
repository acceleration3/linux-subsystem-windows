#pragma once

namespace lsw::host::config {

// LSW is a single-VM system (the "Linux Subsystem for Windows" guest),
// so the VM and all its on-disk artifacts use a fixed name. Stable
// across runs and across re-runs of the wizard — re-running the wizard
// re-uses the same disk image, NVRAM, and payload ISO paths.
inline constexpr auto kVmName = "lsw";

}  // namespace lsw::host::config
