"""Generate ``vk_dispatch_client.{h,cpp}``: ``lsw_vkXxx`` wrappers that
serialize their args, ship them over a ``Transport``, and decode the
reply.

Each wrapper takes a ``Transport&`` so the caller chooses the wire path
(real ICD connection, in-process loopback, recording transport, etc.).
"""

from __future__ import annotations

from pathlib import Path

from ..ir import Command, Registry
from . import classify
from .common import GENERATED_BANNER, header_guard, write


def _signature(cmd: Command) -> str:
    args = ["::lsw::protocol::Transport& t"]
    for p in cmd.params:
        args.append(f"{classify.param_cpp(p.type)} {p.name}")
    return f"{cmd.return_type.cpp()} lsw_{cmd.name}({', '.join(args)})"


def _emit_body(reg: Registry, cmd: Command) -> list[str]:  # noqa: PLR0915
    is_void = cmd.return_type.base == "void"
    is_async = cmd.sync_mode == "async"
    on_fail = "    return;" if is_void else "    return VK_ERROR_DEVICE_LOST;"

    fmt, fmt_args = classify.trace_format_and_args(cmd, reg)
    fmt_args_str = ("," + ", ".join(fmt_args)) if fmt_args else ""
    lines: list[str] = [
        f'LSW_TRACE("client {cmd.name}{fmt}"{fmt_args_str});',
        "::lsw::protocol::Buffer payload;",
    ]

    # ---- Encode inputs ----
    i = 0
    has_array_pair = False
    while i < len(cmd.params):
        p = cmd.params[i]
        if classify.is_count_array_pair(cmd.params, i):
            count = cmd.params[i]
            arr = cmd.params[i + 1]
            lines.append(
                f"const std::uint8_t array_present = {arr.name} != nullptr ? 1 : 0;"
            )
            lines.append("payload.write_pod(array_present);")
            lines.append("if (array_present) {")
            lines.append(f"    payload.write_pod<std::uint32_t>(*{count.name});")
            lines.append("}")
            has_array_pair = True
            i += 2
            continue

        kind = classify.classify(reg, p, cmd.params)
        if kind == "in_null":
            lines.append(f"(void){p.name};  // {p.type.base} not crossed over the wire")
        elif kind == "in_handle":
            lines.append(f"serialize_{p.type.base}(payload, {p.name});")
        elif kind == "in_pod":
            lines.append(f"payload.write_pod({p.name});")
        elif kind == "out_handle":
            pass  # output, no payload encoding
        elif kind == "in_string":
            lines.append("{")
            lines.append(f"    const char* s = {p.name};")
            lines.append("    if (s == nullptr) {")
            lines.append("        payload.write_varint(0);")
            lines.append("    } else {")
            lines.append("        const std::size_t n = std::strlen(s) + 1u;")
            lines.append("        payload.write_varint(n);")
            lines.append("        payload.write(s, n);")
            lines.append("    }")
            lines.append("}")
        elif kind == "in_struct_ptr":
            lines.append(f"serialize_{p.type.base}(payload, *{p.name});")
        elif kind in ("in_handle_array", "in_struct_array"):
            lines.append(
                f"for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>({p.len_expr}); ++__i) {{"
            )
            lines.append(f"    serialize_{p.type.base}(payload, {p.name}[__i]);")
            lines.append("}")
        elif kind == "in_pod_array":
            lines.append(
                f"for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>({p.len_expr}); ++__i) {{"
            )
            lines.append(f"    payload.write_pod({p.name}[__i]);")
            lines.append("}")
        elif kind == "in_byte_array":
            lines.append(f"if ({p.name} != nullptr && {p.len_expr} > 0) {{")
            lines.append(
                f"    payload.write({p.name}, static_cast<std::size_t>({p.len_expr}));"
            )
            lines.append("}")
        elif kind == "in_fixed_array":
            lines.append(
                f"for (std::uint32_t __i = 0; __i < {p.type.fixed_dim}u; ++__i) {{"
            )
            lines.append(f"    payload.write_pod({p.name}[__i]);")
            lines.append("}")
        elif kind == "out_struct":
            # sType-bearing structs need their discriminator (and an empty
            # pNext chain) on the wire so the server reconstructs the right
            # variant; sType-less v1 structs send nothing.
            if reg.structs[p.type.base].has_stype:
                lines.append(f"payload.write_pod({p.name}->sType);")
                lines.append("payload.write_varint(0);  // pNext: empty chain (Phase 1)")
        i += 1

    # ---- Send / receive ----
    if is_async:
        lines.append(
            f"(void)t.send_async(::lsw::vkproto::kCmd_{cmd.name}, payload.bytes());"
        )
        lines.append(
            f'LSW_TRACE("client {cmd.name} -> (async, no reply expected)");'
        )
        # Async-with-result (e.g. vkQueueSubmit when sync_override = "async")
        # is optimistic: errors come back out-of-band on the control stream.
        # Return VK_SUCCESS so the caller's success path runs.
        if not is_void:
            lines.append("return VK_SUCCESS;")
        return lines

    lines.append("::lsw::protocol::Buffer reply;")
    lines.append(
        f"if (t.send_sync(::lsw::vkproto::kCmd_{cmd.name}, payload.bytes(), &reply) "
        "!= ::lsw::protocol::TransportResult::Ok) {"
    )
    lines.append(on_fail)
    lines.append("}")
    lines.append("::lsw::protocol::BufferReader r{reply.bytes()};")
    lines.append("std::int32_t result_code = 0;")
    lines.append("if (!r.read_pod(result_code)) {")
    lines.append(on_fail)
    lines.append("}")

    # ---- Decode out-params ----
    i = 0
    while i < len(cmd.params):
        p = cmd.params[i]
        if classify.is_count_array_pair(cmd.params, i):
            count = cmd.params[i]
            arr = cmd.params[i + 1]
            lines.append(f"if (!r.read_pod(*{count.name})) {{")
            lines.append(on_fail)
            lines.append("}")
            element_is_struct = arr.type.base in reg.structs
            element_is_handle = arr.type.base in reg.handles
            if element_is_struct:
                lines.append("::lsw::protocol::Arena arena_arr;")
            lines.append("if (array_present) {")
            lines.append(f"    for (std::uint32_t k = 0; k < *{count.name}; ++k) {{")
            if element_is_struct:
                lines.append(
                    f"        if (!deserialize_{arr.type.base}(r, &{arr.name}[k], arena_arr)) {{"
                )
            elif element_is_handle:
                lines.append(
                    f"        if (!deserialize_{arr.type.base}(r, {arr.name}[k])) {{"
                )
            else:  # POD / enum
                lines.append(f"        if (!r.read_pod({arr.name}[k])) {{")
            lines.append("        " + on_fail.lstrip())
            lines.append("        }")
            lines.append("    }")
            lines.append("}")
            i += 2
            continue

        kind = classify.classify(reg, p, cmd.params)
        if kind == "out_handle":
            lines.append(f"if (!deserialize_{p.type.base}(r, *{p.name})) {{")
            lines.append(on_fail)
            lines.append("}")
        elif kind == "out_struct":
            lines.append("::lsw::protocol::Arena arena_out;")
            lines.append(f"if (!deserialize_{p.type.base}(r, {p.name}, arena_out)) {{")
            lines.append(on_fail)
            lines.append("}")
        elif kind == "out_pod":
            lines.append(f"if (!r.read_pod(*{p.name})) {{")
            lines.append(on_fail)
            lines.append("}")
        elif kind in ("out_handle_array", "out_struct_array", "out_pod_array"):
            elem = p.type.base
            if kind == "out_struct_array":
                lines.append("::lsw::protocol::Arena arena_out_arr;")
            lines.append(
                f"for (std::uint32_t __k = 0; __k < static_cast<std::uint32_t>({p.len_expr}); ++__k) {{"
            )
            if kind == "out_handle_array":
                lines.append(f"    if (!deserialize_{elem}(r, {p.name}[__k])) {{")
                lines.append("    " + on_fail.lstrip())
                lines.append("    }")
            elif kind == "out_struct_array":
                lines.append(
                    f"    if (!deserialize_{elem}(r, &{p.name}[__k], arena_out_arr)) {{"
                )
                lines.append("    " + on_fail.lstrip())
                lines.append("    }")
            else:  # out_pod_array
                lines.append(f"    if (!r.read_pod({p.name}[__k])) {{")
                lines.append("    " + on_fail.lstrip())
                lines.append("    }")
            lines.append("}")
        i += 1

    if has_array_pair:
        # Nothing — variable was consumed already above; suppresses the
        # "unused variable" pylint warning that this block used to need.
        pass

    lines.append(
        f'LSW_TRACE("client {cmd.name} -> result_code={{}}", result_code);'
    )
    if not is_void:
        lines.append(f"return static_cast<{cmd.return_type.cpp()}>(result_code);")
    return lines


def emit(reg: Registry, out_dir: Path) -> None:
    cmds = sorted(reg.commands.values(), key=lambda c: c.cmd_id)

    # ---- Header ----
    header = [
        GENERATED_BANNER,
        "",
        "#include <cstdint>",
        "",
        "#include <vulkan/vulkan_core.h>",
        "",
        '#include "lsw/protocol/transport.hpp"',
        "",
        "namespace lsw::vkproto {",
        "",
    ]
    for cmd in cmds:
        header.append(_signature(cmd) + ";")
    header.append("")
    header.append("}  // namespace lsw::vkproto")
    header.append("")
    write(out_dir / "vk_dispatch_client.h", header_guard("\n".join(header[1:])))

    # ---- Source ----
    source = [
        GENERATED_BANNER,
        "",
        '#include "vk_dispatch_client.h"',
        "",
        '#include "lsw/log.hpp"',
        '#include "lsw/protocol/buffer.hpp"',
        '#include "vk_protocol_ids.h"',
        '#include "vk_serialize.h"',
        "",
        "namespace lsw::vkproto {",
        "",
    ]
    for cmd in cmds:
        source.append(_signature(cmd) + " {")
        for line in _emit_body(reg, cmd):
            source.append("    " + line)
        source.append("}")
        source.append("")
    source.append("}  // namespace lsw::vkproto")
    source.append("")
    write(out_dir / "vk_dispatch_client.cpp", "\n".join(source))
