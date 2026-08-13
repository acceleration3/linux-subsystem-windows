#pragma once

#include "lsw/icd/entry_table.hpp"

#include <span>

namespace lsw::icd::queue {

// Queue-submission entries that wrap the generated ones.
//
// Their whole job is the flush that Vulkan says is unnecessary: with
// HOST_COHERENT memory an application writes through a mapped pointer
// and submits, with no flush call anywhere, because on a real driver the
// GPU sees those writes. Across a process (and later a VM) boundary
// there is no such coherence — the mapped pointer is a local buffer —
// so anything written since the last submit has to be shipped before the
// commands that read it.
//
// Without this, a proxied app renders its clear colour and nothing else:
// every draw executes against whatever the buffers held when they were
// mapped.
std::span<const Entry> entries() noexcept;

}  // namespace lsw::icd::queue
