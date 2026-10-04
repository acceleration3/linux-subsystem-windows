#pragma once

namespace lsw::agent::system {

// Grows C: into whatever free space follows it on its disk.
//
// The manager's Disk setting grows the VM's disk image while the VM is
// off; the new space arrives in the guest as unallocated space after the
// Windows partition (the install's layout is EFI, MSR, Windows — C: is
// last, see the autounattend's DiskConfiguration). This takes it. With
// nothing to take it does nothing, so it simply runs at every start.
//
// The Storage module's own cmdlets do the work (Get-PartitionSupportedSize
// then Resize-Partition), in a hidden PowerShell with no window: they
// already handle what doing this by hand would have to — re-reading a
// disk whose size changed underneath a GPT whose backup header sits at
// the old end — and are what anyone would type to check it.
class VolumeGrower {
public:
    VolumeGrower() = delete;

    // Run once, on a thread of its own; returns immediately. What it did
    // (or that there was nothing to do) goes to the agent log.
    static void start();
};

}  // namespace lsw::agent::system
