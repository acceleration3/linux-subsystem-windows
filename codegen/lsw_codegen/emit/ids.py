"""Emit ``vk_protocol_ids.h`` — the stable wire-protocol command ID enum."""

from __future__ import annotations

from pathlib import Path

from ..ir import Registry
from .common import GENERATED_BANNER, header_guard, write


def emit(reg: Registry, out_dir: Path) -> None:
    lines = [
        GENERATED_BANNER,
        "",
        "#include <cstdint>",
        "",
        "namespace lsw::vkproto {",
        "",
        "// Stable wire-protocol command IDs. See codegen/protocol_ids.toml.",
        "enum CmdId : std::uint32_t {",
    ]
    # Sort by ID so the file is stable.
    for cmd in sorted(reg.commands.values(), key=lambda c: c.cmd_id):
        lines.append(f"    kCmd_{cmd.name} = 0x{cmd.cmd_id:08X}u,")
    lines.append("};")
    lines.append("")
    lines.append("}  // namespace lsw::vkproto")
    lines.append("")

    write(out_dir / "vk_protocol_ids.h", header_guard("\n".join(lines[1:])))
