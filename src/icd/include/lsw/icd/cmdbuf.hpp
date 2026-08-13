#pragma once

#include "lsw/icd/entry_table.hpp"

#include <span>

namespace lsw::icd::cmdbuf {

// Command-buffer lifecycle entries: the ones that start, ship and
// discard a recorded batch.
//
// Everything between them — every ``vkCmd*`` — is generated and goes
// straight into the buffer's own recorder without touching the
// transport. These four are the boundary, and they are hand-written
// because each has to do something before or after the generated call
// rather than instead of it:
//
//   vkBeginCommandBuffer   drop whatever was recorded before
//   vkEndCommandBuffer     ship the batch, THEN end (order matters)
//   vkResetCommandBuffer   drop the batch
//   vkFreeCommandBuffers   destroy the wrappers, and with them the
//                          recorders
//
// ``vkResetCommandPool`` is deliberately absent. It resets every buffer
// in the pool, so a pending batch does become stale — but a reset buffer
// is in the initial state, and the only legal way back to recording is
// ``vkBeginCommandBuffer``, which drops the batch on the way in. The
// stale bytes cannot reach the wire, because the only thing that ships
// them is ``vkEndCommandBuffer`` and reaching one without an
// intervening Begin is not valid usage.
std::span<const Entry> entries() noexcept;

}  // namespace lsw::icd::cmdbuf
