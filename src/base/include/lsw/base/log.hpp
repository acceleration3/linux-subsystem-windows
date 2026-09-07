#pragma once

// Logging macros. Configuration of the underlying sinks lives in
// ``lsw::base::Logger`` (lsw/base/logger.hpp) — include that only where
// the process is actually set up; everything else needs the macros only.

// Compile every level in: ``LSW_TRACE`` and ``LSW_DEBUG`` would otherwise
// expand to nothing because spdlog's default ``SPDLOG_ACTIVE_LEVEL`` is
// ``SPDLOG_LEVEL_INFO``. Setting it here means the runtime level
// (``$LSW_LOG`` env var) actually controls what's printed.
#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif

#include <spdlog/spdlog.h>

#include <cstdint>

namespace lsw::base {

// One spelling for "put this Vulkan handle in a log line" that compiles
// on both pointer widths.
//
// A 64-bit build gets a pointer type for every handle, dispatchable or
// not, so ``static_cast<const void*>`` worked and that is what the
// codegen emitted. On 32-bit, ``VK_DEFINE_NON_DISPATCHABLE_HANDLE``
// expands to ``uint64_t`` instead — wider than a pointer, not castable
// to one, and the trace lines stopped compiling. Normalising to an
// integer is the only form both agree on.
inline unsigned long long trace_handle(const void* h) {
    return static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(h));
}
inline unsigned long long trace_handle(std::uint64_t h) {
    return static_cast<unsigned long long>(h);
}

}  // namespace lsw::base

// Convenience: drop the spdlog macros into the lsw namespace under
// shorter names. They expand to no-ops below the configured level.
//
//   LSW_TRACE("vkCreateBuffer device={} size={}", (void*)device, info.size);
//   LSW_DEBUG("connection {} accepted", peer.fd);
//   LSW_INFO("listening on {}", listener.description());
//   LSW_WARN("extension {} disabled in vk.xml", name);
//   LSW_ERROR("dispatch failed: cmd_id=0x{:08x}", cmd_id);
#define LSW_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define LSW_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LSW_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define LSW_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define LSW_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
