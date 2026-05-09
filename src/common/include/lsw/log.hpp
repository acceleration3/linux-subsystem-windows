#pragma once

// Compile every level in: ``LSW_TRACE`` and ``LSW_DEBUG`` would otherwise
// expand to nothing because spdlog's default ``SPDLOG_ACTIVE_LEVEL`` is
// ``SPDLOG_LEVEL_INFO``. Setting it here means the runtime level
// (``$LSW_LOG`` env var) actually controls what's printed.
#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif

#include <spdlog/spdlog.h>

namespace lsw::log {

// One-shot setup. ``component`` is a short tag (``icd``, ``server``,
// ``codegen``) that prefixes every line so combined logs are still
// readable. Reads ``$LSW_LOG`` for the level (``trace``, ``debug``,
// ``info``, ``warn``, ``error``); default is ``info``. Reads
// ``$LSW_LOG_FILE`` for an optional file sink (in addition to stderr).
// Safe to call repeatedly — only the first call has effect.
void init(const char* component) noexcept;

}  // namespace lsw::log

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
