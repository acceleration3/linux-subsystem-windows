"""Generate ``vk_command_names.{h,cpp}``: ``cmd_id -> name`` lookup.

The runtime trace logs print the command name alongside its wire id so
the developer doesn't have to hand-translate hex through
``protocol_ids.toml``. This emitter is also the right place to bake in
the LSW-internal control commands (kCmdHello, kCmdCreateXcbSurface,
kCmdMemoryWrite, ...) so they show up legibly in the same trace.
"""

from __future__ import annotations

from pathlib import Path

from ..ir import Registry
from .common import GENERATED_BANNER, header_guard, write


# LSW-internal control commands. Keep the numeric values in sync with
# ``src/protocol/include/lsw/protocol/frame.hpp``. They live there as the
# wire-format source of truth; this list lets cmd_id_to_string render
# them, and bumps in frame.hpp need a corresponding bump here. There's
# no chicken-and-egg: the header already uses these names too.
LSW_INTERNAL_CMDS: tuple[tuple[int, str], ...] = (
    (0x00000001, "kCmdHello"),
    (0x00000002, "kCmdHelloAck"),
    (0x00000003, "kCmdPing"),
    (0x00000004, "kCmdPong"),
    (0x00000005, "kCmdAsyncError"),
    (0x00000010, "kCmdCreateXcbSurface"),
    (0x00000011, "kCmdDestroySurface"),
    (0x00000020, "kCmdMemoryWrite"),
    (0x00000021, "kCmdMemoryRead"),
    (0xFFFFFFFF, "kCmdBye"),
)


def emit(reg: Registry, out_dir: Path) -> None:
    cmds = sorted(reg.commands.values(), key=lambda c: c.cmd_id)

    header = [
        GENERATED_BANNER,
        "",
        "#include <cstdint>",
        "",
        "namespace lsw::vkproto {",
        "",
        "// Returns a static C-string name for a wire command id. Returns",
        "// ``\"unknown\"`` for unknown ids — never null. Cheap (one switch).",
        "[[nodiscard]] const char* cmd_id_to_string(std::uint32_t cmd_id) noexcept;",
        "",
        "}  // namespace lsw::vkproto",
        "",
    ]
    write(out_dir / "vk_command_names.h", header_guard("\n".join(header[1:])))

    source = [
        GENERATED_BANNER,
        "",
        '#include "vk_command_names.h"',
        "",
        "namespace lsw::vkproto {",
        "",
        "const char* cmd_id_to_string(std::uint32_t cmd_id) noexcept {",
        "    switch (cmd_id) {",
    ]
    for cid, name in LSW_INTERNAL_CMDS:
        source.append(f'        case 0x{cid:08X}u: return "{name}";')
    for cmd in cmds:
        source.append(
            f'        case 0x{cmd.cmd_id:08X}u: return "{cmd.name}";'
        )
    source.append('        default: return "unknown";')
    source.append("    }")
    source.append("}")
    source.append("")
    source.append("}  // namespace lsw::vkproto")
    source.append("")
    write(out_dir / "vk_command_names.cpp", "\n".join(source))
