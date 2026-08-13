"""Emit serialize_/deserialize_ helpers for handles and structs.

Phase 1.6 expands struct support beyond pure-POD: optional pointer-to-struct,
single null-terminated string, string array, variable-length POD array, and
variable-length struct array. Server-side deserialization allocates pointed-to
storage in an ``Arena`` provided by the caller. Client-side serialization is
direct.

What's still not handled (would need pNext support, opaque blobs, unions, etc.):
  * non-empty pNext chains
  * function pointers
  * unions (VkClearValue family)
  * external memory handle types (Win32, fd, etc.)
"""

from __future__ import annotations

from pathlib import Path

from ..ir import Registry, Struct, StructMember
from .common import GENERATED_BANNER, header_guard, write


def _referenced_handle_names(reg: Registry) -> list[str]:
    seen: set[str] = set()
    for cmd in reg.commands.values():
        if cmd.return_type.base in reg.handles:
            seen.add(cmd.return_type.base)
        for p in cmd.params:
            if p.type.base in reg.handles:
                seen.add(p.type.base)
    return sorted(seen)


DISPATCHABLE_HANDLE_TYPES = (
    "VkInstance",
    "VkPhysicalDevice",
    "VkDevice",
    "VkQueue",
    "VkCommandBuffer",
)


def _emit_handle_helpers(name: str) -> list[str]:
    is_dispatchable = name in DISPATCHABLE_HANDLE_TYPES
    if is_dispatchable:
        # Dispatchable handles route through ``handle_to_wire`` so the
        # client side can override and unwrap its loader-magic-marked
        # ``LswWrappedHandle`` to the server's real driver handle. The
        # default linkage (used by the server and by loopback tests) is
        # identity. See ``vk_handle_default.cpp``.
        return [
            f"inline void serialize_{name}(::lsw::protocol::Buffer& buf, {name} v) noexcept {{",
            f"    buf.write_pod(handle_to_wire(v));",
            "}",
            "",
            "[[nodiscard]] inline bool "
            f"deserialize_{name}(::lsw::protocol::BufferReader& r, {name}& out) noexcept {{",
            "    std::uint64_t raw = 0;",
            "    if (!r.read_pod(raw)) {",
            "        return false;",
            "    }",
            "    std::memcpy(&out, &raw, sizeof(out));",
            "    return true;",
            "}",
            "",
        ]
    return [
        f"inline void serialize_{name}(::lsw::protocol::Buffer& buf, {name} v) noexcept {{",
        "    std::uint64_t raw = 0;",
        f"    static_assert(sizeof({name}) == sizeof(raw));",
        "    std::memcpy(&raw, &v, sizeof(raw));",
        "    buf.write_pod(raw);",
        "}",
        "",
        "[[nodiscard]] inline bool "
        f"deserialize_{name}(::lsw::protocol::BufferReader& r, {name}& out) noexcept {{",
        "    std::uint64_t raw = 0;",
        "    if (!r.read_pod(raw)) {",
        "        return false;",
        "    }",
        "    std::memcpy(&out, &raw, sizeof(out));",
        "    return true;",
        "}",
        "",
    ]


# ---- Member classification ------------------------------------------------


def _len_components(m: StructMember) -> list[str]:
    if not m.len_expr:
        return []
    return [s.strip() for s in m.len_expr.split(",") if s.strip()]


def _expr_to_cpp(expr: str, prefix: str) -> str:
    """Rewrite a vk.xml ``len`` / ``altlen`` expression into a C++ expression.

    Bare C-identifier tokens are member references — they get ``prefix``
    prepended (``v.`` for serializer, ``out->`` for deserializer). Numeric
    literals, parens, and operators are left as-is. So
    ``"(rasterizationSamples + 31) / 32"`` becomes
    ``"(v.rasterizationSamples + 31) / 32"`` for serializer-side use.
    """
    import re

    return re.sub(r"\b([a-zA-Z_][a-zA-Z_0-9]*)\b", lambda m: prefix + m.group(0), expr)


def _classify_member(reg: Registry, m: StructMember) -> str:
    if m.name == "pNext":
        return "pnext"
    if m.type.is_fixed_array:
        return "fixed_array"

    if m.type.pointer_depth == 0:
        if m.type.base in reg.structs:
            return "nested_struct"
        if m.type.base in reg.handles:
            return "nested_handle"
        return "pod"

    parts = _len_components(m)

    if m.type.pointer_depth == 1:
        if m.type.base == "char":
            if parts == ["null-terminated"]:
                return "string"
            return "unknown_ptr"
        if m.type.base == "void":
            # Bare ``void*`` outside ``pNext`` — opaque handles, function
            # pointers; not encoded for Phase 1.6.
            return "unknown_ptr"
        if m.type.base in reg.structs:
            if not parts:
                return "optional_struct_ptr"
            return "struct_array"
        if m.type.base in reg.handles and parts:
            # Arrays of Vulkan handles MUST go through ``serialize_<H>``
            # so the ``handle_to_wire`` hook can unwrap dispatchable
            # handles (VkCommandBuffer in VkSubmitInfo.pCommandBuffers,
            # VkSemaphore in pWaitSemaphores, etc.). Without this they'd
            # write the raw client-side wrapper pointer.
            return "handle_array"
        if parts:
            return "pod_array"
        return "unknown_ptr"

    if m.type.pointer_depth == 2:
        if m.type.base == "char" and parts and parts[-1] == "null-terminated":
            return "string_array"
        return "unknown_ptr"

    return "unknown_ptr"


# ---- Per-member emission --------------------------------------------------


def _serialize_member(reg: Registry, m: StructMember) -> list[str]:
    kind = _classify_member(reg, m)
    if kind == "pnext":
        return [
            "    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.",
            "    buf.write_varint(0);",
        ]
    if kind == "fixed_array":
        return [f"    buf.write(&v.{m.name}[0], sizeof(v.{m.name}));"]
    if kind == "nested_struct":
        return [f"    serialize_{m.type.base}(buf, v.{m.name});"]
    if kind == "nested_handle":
        return [f"    serialize_{m.type.base}(buf, v.{m.name});"]
    if kind == "pod":
        return [f"    buf.write_pod(v.{m.name});"]
    if kind == "string":
        return [
            "    {",
            f"        const char* s = v.{m.name};",
            "        if (s == nullptr) {",
            "            buf.write_varint(0);",
            "        } else {",
            "            const std::size_t n = std::strlen(s) + 1u;",
            "            buf.write_varint(n);",
            "            buf.write(s, n);",
            "        }",
            "    }",
        ]
    if kind == "optional_struct_ptr":
        return [
            "    {",
            f"        const auto* p = v.{m.name};",
            "        if (p == nullptr) {",
            "            buf.write_pod<std::uint8_t>(0);",
            "        } else {",
            "            buf.write_pod<std::uint8_t>(1);",
            f"            serialize_{m.type.base}(buf, *p);",
            "        }",
            "    }",
        ]
    if kind == "struct_array":
        count = _len_components(m)[0]
        return [
            "    {",
            f"        const std::uint8_t present = v.{m.name} != nullptr ? 1 : 0;",
            "        buf.write_pod(present);",
            "        if (present) {",
            f"            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>({_expr_to_cpp(count, "v.")}); ++i) {{",
            f"                serialize_{m.type.base}(buf, v.{m.name}[i]);",
            "            }",
            "        }",
            "    }",
        ]
    if kind == "pod_array":
        count = _len_components(m)[0]
        return [
            "    {",
            f"        const std::uint8_t present = v.{m.name} != nullptr ? 1 : 0;",
            "        buf.write_pod(present);",
            "        if (present) {",
            f"            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>({_expr_to_cpp(count, "v.")}); ++i) {{",
            f"                buf.write_pod(v.{m.name}[i]);",
            "            }",
            "        }",
            "    }",
        ]
    if kind == "handle_array":
        count = _len_components(m)[0]
        return [
            "    {",
            f"        const std::uint8_t present = v.{m.name} != nullptr ? 1 : 0;",
            "        buf.write_pod(present);",
            "        if (present) {",
            f"            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>({_expr_to_cpp(count, "v.")}); ++i) {{",
            f"                serialize_{m.type.base}(buf, v.{m.name}[i]);",
            "            }",
            "        }",
            "    }",
        ]
    if kind == "string_array":
        count = _len_components(m)[0]
        return [
            "    {",
            f"        const std::uint8_t present = v.{m.name} != nullptr ? 1 : 0;",
            "        buf.write_pod(present);",
            "        if (present) {",
            f"            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>({_expr_to_cpp(count, "v.")}); ++i) {{",
            f"                const char* s = v.{m.name}[i];",
            "                if (s == nullptr) {",
            "                    buf.write_varint(0);",
            "                } else {",
            "                    const std::size_t n = std::strlen(s) + 1u;",
            "                    buf.write_varint(n);",
            "                    buf.write(s, n);",
            "                }",
            "            }",
            "        }",
            "    }",
        ]
    return [
        f"    // TODO: unhandled member shape {kind!r} for {m.name!r} ({m.type.cpp()})",
        f"    (void)v.{m.name};",
    ]


def _deserialize_member(reg: Registry, m: StructMember) -> list[str]:
    kind = _classify_member(reg, m)
    if kind == "pnext":
        # Read the chain length but DON'T touch ``out->pNext``. On the
        # server side ``out`` was just zero-initialized so pNext is already
        # nullptr; on the client side the application set ``out->pNext``
        # to its own chain head before the call, and overwriting it with
        # nullptr would orphan the chain (and crash the application when it
        # later walks pNext for chained queries it expected to be filled).
        # Phase 1 still rejects non-empty chains because the chained data
        # isn't on the wire — but the pointer is preserved.
        return [
            "    {",
            "        std::uint64_t pnext_count = 0;",
            "        if (!r.read_varint(pnext_count)) { return false; }",
            "        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions",
            "    }",
        ]
    if kind == "fixed_array":
        return [
            f"    if (!r.read(&out->{m.name}[0], sizeof(out->{m.name}))) {{ return false; }}"
        ]
    if kind == "nested_struct":
        return [
            f"    if (!deserialize_{m.type.base}(r, &out->{m.name}, arena)) {{ return false; }}"
        ]
    if kind == "nested_handle":
        return [
            f"    if (!deserialize_{m.type.base}(r, out->{m.name})) {{ return false; }}"
        ]
    if kind == "pod":
        return [f"    if (!r.read_pod(out->{m.name})) {{ return false; }}"]
    if kind == "string":
        return [
            "    {",
            "        std::uint64_t n = 0;",
            "        if (!r.read_varint(n)) { return false; }",
            "        if (n == 0) {",
            f"            out->{m.name} = nullptr;",
            "        } else {",
            "            char* s = arena.alloc_chars(static_cast<std::size_t>(n));",
            "            if (!r.read(s, static_cast<std::size_t>(n))) { return false; }",
            f"            out->{m.name} = s;",
            "        }",
            "    }",
        ]
    if kind == "optional_struct_ptr":
        return [
            "    {",
            "        std::uint8_t present = 0;",
            "        if (!r.read_pod(present)) { return false; }",
            "        if (present == 0) {",
            f"            out->{m.name} = nullptr;",
            "        } else {",
            f"            auto* p = arena.alloc<{m.type.base}>();",
            f"            if (!deserialize_{m.type.base}(r, p, arena)) {{ return false; }}",
            f"            out->{m.name} = p;",
            "        }",
            "    }",
        ]
    if kind == "struct_array":
        count = _len_components(m)[0]
        return [
            "    {",
            "        std::uint8_t present = 0;",
            "        if (!r.read_pod(present)) { return false; }",
            f"        if (!present || {_expr_to_cpp(count, "out->")} == 0) {{",
            f"            out->{m.name} = nullptr;",
            "        } else {",
            f"            auto* arr = arena.alloc<{m.type.base}>(static_cast<std::size_t>({_expr_to_cpp(count, "out->")}));",
            f"            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>({_expr_to_cpp(count, "out->")}); ++i) {{",
            f"                if (!deserialize_{m.type.base}(r, &arr[i], arena)) {{ return false; }}",
            "            }",
            f"            out->{m.name} = arr;",
            "        }",
            "    }",
        ]
    if kind == "pod_array":
        count = _len_components(m)[0]
        # Use the underlying pointee type for storage, peeled off the pointer.
        elem = m.type.base
        return [
            "    {",
            "        std::uint8_t present = 0;",
            "        if (!r.read_pod(present)) { return false; }",
            f"        if (!present || {_expr_to_cpp(count, "out->")} == 0) {{",
            f"            out->{m.name} = nullptr;",
            "        } else {",
            f"            auto* arr = arena.alloc<{elem}>(static_cast<std::size_t>({_expr_to_cpp(count, "out->")}));",
            f"            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>({_expr_to_cpp(count, "out->")}); ++i) {{",
            "                if (!r.read_pod(arr[i])) { return false; }",
            "            }",
            f"            out->{m.name} = arr;",
            "        }",
            "    }",
        ]
    if kind == "handle_array":
        count = _len_components(m)[0]
        elem = m.type.base
        return [
            "    {",
            "        std::uint8_t present = 0;",
            "        if (!r.read_pod(present)) { return false; }",
            f"        if (!present || {_expr_to_cpp(count, "out->")} == 0) {{",
            f"            out->{m.name} = nullptr;",
            "        } else {",
            f"            auto* arr = arena.alloc<{elem}>(static_cast<std::size_t>({_expr_to_cpp(count, "out->")}));",
            f"            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>({_expr_to_cpp(count, "out->")}); ++i) {{",
            f"                if (!deserialize_{elem}(r, arr[i])) {{ return false; }}",
            "            }",
            f"            out->{m.name} = arr;",
            "        }",
            "    }",
        ]
    if kind == "string_array":
        count = _len_components(m)[0]
        return [
            "    {",
            "        std::uint8_t present = 0;",
            "        if (!r.read_pod(present)) { return false; }",
            f"        if (!present || {_expr_to_cpp(count, "out->")} == 0) {{",
            f"            out->{m.name} = nullptr;",
            "        } else {",
            f"            auto** arr = arena.alloc<const char*>(static_cast<std::size_t>({_expr_to_cpp(count, "out->")}));",
            f"            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>({_expr_to_cpp(count, "out->")}); ++i) {{",
            "                std::uint64_t n = 0;",
            "                if (!r.read_varint(n)) { return false; }",
            "                if (n == 0) {",
            "                    arr[i] = nullptr;",
            "                } else {",
            "                    char* s = arena.alloc_chars(static_cast<std::size_t>(n));",
            "                    if (!r.read(s, static_cast<std::size_t>(n))) { return false; }",
            "                    arr[i] = s;",
            "                }",
            "            }",
            f"            out->{m.name} = arr;",
            "        }",
            "    }",
        ]
    return [
        f"    // TODO: unhandled member shape {kind!r} for {m.name!r} ({m.type.cpp()})",
        f"    (void)out->{m.name};",
    ]


def _emit_struct_helpers(reg: Registry, s: Struct) -> list[str]:
    if s.is_union:
        # Unions are encoded as raw bytes — the discriminator is
        # context-dependent (e.g. VkClearColorValue's active member is
        # determined by the format of the image being cleared). The
        # alternative would be to walk every member every time, which
        # reads invalid memory for inactive members.
        return [
            f"inline void serialize_{s.name}(::lsw::protocol::Buffer& buf, const {s.name}& v) {{",
            "    buf.write(&v, sizeof(v));",
            "}",
            "",
            "[[nodiscard]] inline bool "
            f"deserialize_{s.name}(::lsw::protocol::BufferReader& r, {s.name}* out, "
            "::lsw::protocol::Arena& arena) {",
            "    (void)arena;",
            "    return r.read(out, sizeof(*out));",
            "}",
            "",
        ]
    out: list[str] = []
    out.append(
        f"inline void serialize_{s.name}(::lsw::protocol::Buffer& buf, const {s.name}& v) {{"
    )
    for m in s.members:
        out.extend(_serialize_member(reg, m))
    out.append("}")
    out.append("")
    out.append(
        "[[nodiscard]] inline bool "
        f"deserialize_{s.name}(::lsw::protocol::BufferReader& r, {s.name}* out, "
        "::lsw::protocol::Arena& arena) {"
    )
    out.append("    (void)arena;")
    for m in s.members:
        out.extend(_deserialize_member(reg, m))
    out.append("    return true;")
    out.append("}")
    out.append("")
    return out


def emit(reg: Registry, out_dir: Path) -> None:
    header = [
        GENERATED_BANNER,
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <cstring>",
        "",
        "#include <vulkan/vulkan_core.h>",
        "",
        '#include "lsw/protocol/arena.hpp"',
        '#include "lsw/protocol/buffer.hpp"',
        # Every deserialize_* takes a BufferReader&, so this header is
        # not optional. It was missing from the emitter and present in
        # the committed output, which is one of the ways generated/ had
        # drifted out of reach of check-codegen.
        '#include "lsw/protocol/buffer_reader.hpp"',
        "",
        "namespace lsw::vkproto {",
        "",
        "// All Vulkan handles cross the wire as 64-bit values. See docs/protocol.md.",
        "// LSW currently requires 64-bit hosts; revisit handle width if 32-bit support",
        "// becomes a goal.",
        'static_assert(sizeof(void*) == 8, "LSW assumes 64-bit pointers");',
        "",
        "// Hook for translating dispatchable handles to their wire form.",
        "// The default implementations (linked from vk_handle_default.cpp,",
        "// weakly bound) are identity — correct for the server replay path",
        "// and for loopback tests. The ICD links a strong override that",
        "// unwraps its loader-magic-marked LswWrappedHandle to the server's",
        "// real driver handle.",
        "[[nodiscard]] std::uint64_t handle_to_wire(VkInstance v) noexcept;",
        "[[nodiscard]] std::uint64_t handle_to_wire(VkPhysicalDevice v) noexcept;",
        "[[nodiscard]] std::uint64_t handle_to_wire(VkDevice v) noexcept;",
        "[[nodiscard]] std::uint64_t handle_to_wire(VkQueue v) noexcept;",
        "[[nodiscard]] std::uint64_t handle_to_wire(VkCommandBuffer v) noexcept;",
        "",
    ]

    for h in _referenced_handle_names(reg):
        header.extend(_emit_handle_helpers(h))

    if reg.struct_order:
        header.append("// Forward declarations.")
        for n in reg.struct_order:
            header.append(
                f"inline void serialize_{n}(::lsw::protocol::Buffer&, const {n}&);"
            )
            header.append(
                "[[nodiscard]] inline bool "
                f"deserialize_{n}(::lsw::protocol::BufferReader&, {n}*, "
                "::lsw::protocol::Arena&);"
            )
        header.append("")

    for n in reg.struct_order:
        header.extend(_emit_struct_helpers(reg, reg.structs[n]))

    header.append("}  // namespace lsw::vkproto")
    header.append("")

    write(out_dir / "vk_serialize.h", header_guard("\n".join(header[1:])))

    # vk_handle_default.cpp: weak identity definitions of handle_to_wire.
    # Anyone who wants a different translation (the ICD unwrap, mainly)
    # links a strong definition; the linker picks the strong one.
    handle_default = [
        GENERATED_BANNER,
        "",
        '#include "vk_serialize.h"',
        "",
        "#include <cstdint>",
        "#include <cstring>",
        "",
        "namespace lsw::vkproto {",
        "",
        "namespace {",
        "template<typename H>",
        "[[nodiscard]] std::uint64_t identity(H v) noexcept {",
        "    std::uint64_t raw = 0;",
        "    static_assert(sizeof(H) == sizeof(raw));",
        "    std::memcpy(&raw, &v, sizeof(raw));",
        "    return raw;",
        "}",
        "}  // namespace",
        "",
    ]
    for h in DISPATCHABLE_HANDLE_TYPES:
        handle_default.append(
            f"[[gnu::weak]] std::uint64_t handle_to_wire({h} v) noexcept "
            f"{{ return identity(v); }}"
        )
    handle_default.append("")
    handle_default.append("}  // namespace lsw::vkproto")
    handle_default.append("")
    write(out_dir / "vk_handle_default.cpp", "\n".join(handle_default))
