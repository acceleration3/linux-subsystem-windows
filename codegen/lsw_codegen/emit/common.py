"""Helpers shared by emitters."""

from __future__ import annotations

from pathlib import Path

GENERATED_BANNER = """// THIS FILE IS GENERATED. DO NOT EDIT.
// Regenerate with: cmake --build build --target regenerate-vulkan
//                  (or: python -m lsw_codegen --xml ... --out generated/)
"""


def write(path: Path, content: str) -> None:
    """Write atomically; only touches the file if content differs."""
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(content, encoding="utf-8")
    tmp.replace(path)


def header_guard(content: str) -> str:
    return "#pragma once\n\n" + content


def is_handle(reg, type_name: str) -> bool:
    return type_name in reg.handles


def wire_handle_type() -> str:
    """All Vulkan handles travel as 64-bit on the wire (see docs/protocol.md)."""
    return "std::uint64_t"
