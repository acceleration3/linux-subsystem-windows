"""Generate Linux Vulkan ICD wrappers + name -> fnptr table.

The ICD layer is mechanically driven by each command's parameter list:

  * dispatchable handle inputs (VkInstance, VkPhysicalDevice, VkDevice,
    VkQueue, VkCommandBuffer) get unwrapped from the loader-magic-marked
    LswWrappedHandle to expose the server-side handle on the wire;
  * dispatchable handle outputs get wrapped on the way back up, inheriting
    the parent ``Connection`` from the first input dispatchable handle;
  * arrays of dispatchable handles (the count_array_pair shape) get
    wrapped element-by-element into the caller's array;
  * VkAllocationCallbacks pass through; the wire side discards them;
  * the rest pass through unchanged.

Per-VkInstance Connection: the Connection is acquired in
``vkCreateInstance``, attached to the wrapped VkInstance, inherited by
every child handle, and released in ``vkDestroyInstance``. There is no
process-wide transport singleton anymore.

Hand-writing this for every newly added command is tedious and exactly
the kind of thing the codegen should own. With this module each new
command in ``profile.toml`` produces an ICD wrapper for free; the only
remaining manual ICD edit is for entry points like vkGetInstanceProcAddr
that don't go on the wire.
"""

from __future__ import annotations

from pathlib import Path

from ..ir import Command, Param, Registry
from .common import GENERATED_BANNER, header_guard, write

# Dispatchable handle types we wrap with loader-magic. Non-dispatchable
# handles (VkBuffer, VkImage, etc.) cross the wire as raw 64-bit values
# and don't need this treatment.
DISPATCHABLE_HANDLES = frozenset(
    {"VkInstance", "VkPhysicalDevice", "VkDevice", "VkQueue", "VkCommandBuffer"}
)


def _is_recordable(cmd: Command, parent: Param) -> bool:
    """Does this command belong in a per-command-buffer batch?

    ``vkCmd*`` taking a VkCommandBuffer, plus Begin and End. Those are
    the commands that record into a buffer rather than acting
    immediately, and the buffer they record into is externally
    synchronised — so accumulating them per buffer needs no lock and
    cannot reorder anything.

    Begin and End earn their place: they bracket the recording rather
    than being global state, so putting them IN the batch removes two
    blocking round trips per frame and costs no extra message. The
    wrappers in ``src/icd/src/cmdbuf.cpp`` still exist — Begin drops the
    previous batch before recording into it, and End ships the batch
    once the End itself has been recorded — but the serialization goes
    through the recorder either way.

    Reset is NOT here. It has to reach the server even if the
    application never records into that buffer again, so it goes
    straight out on the transport (asynchronously) rather than sitting
    in a batch that might never be flushed.
    """
    if parent.type.base != "VkCommandBuffer":
        return False
    return cmd.name.startswith("vkCmd") or cmd.name in (
        "vkBeginCommandBuffer",
        "vkEndCommandBuffer",
    )


# ---- Helpers ---------------------------------------------------------------


def _is_count_array_pair(params: list[Param], i: int) -> bool:
    if i + 1 >= len(params):
        return False
    a, b = params[i], params[i + 1]
    if a.type.base not in ("uint32_t", "size_t"):
        return False
    if a.type.pointer_depth != 1 or b.type.pointer_depth != 1:
        return False
    return b.len_expr == a.name


def _param_cpp(t) -> str:
    """Decay fixed-array params to pointers in the wrapper signatures."""
    s = t.base
    eff_ptr = t.pointer_depth + (1 if t.fixed_dim is not None else 0)
    if eff_ptr:
        s += "*" * eff_ptr
    if t.is_const:
        s = "const " + s
    return s


def _signature(cmd: Command) -> str:
    args = ", ".join(f"{_param_cpp(p.type)} {p.name}" for p in cmd.params)
    return f"VKAPI_ATTR {cmd.return_type.cpp()} VKAPI_CALL Wrap_{cmd.name}({args})"


def _first_input_dispatchable(cmd: Command) -> Param | None:
    """Find the first input dispatchable handle param.

    Used to find the ``Connection`` for each wire call. ``vkCreateInstance``
    has none and is special-cased upstream.
    """
    for p in cmd.params:
        if p.type.base in DISPATCHABLE_HANDLES and p.type.pointer_depth == 0:
            return p
    return None


# ---- Per-shape body builders ----------------------------------------------


def _emit_create_instance(cmd: Command) -> list[str]:
    """vkCreateInstance: open a fresh Connection, wrap the returned handle.

    Releases the connection if the wire call fails. The wrapping pattern
    inherits via the same connection pointer for every child handle.
    """
    out_inst = next(
        (p for p in cmd.params
         if p.type.base == "VkInstance" and p.type.pointer_depth == 1),
        None,
    )
    assert out_inst is not None, "vkCreateInstance must have VkInstance* out param"

    forwarded: list[str] = []
    for p in cmd.params:
        if p.name == out_inst.name:
            forwarded.append("&server_instance")
        else:
            forwarded.append(p.name)

    return [
        "    auto* conn = ::lsw::icd::acquire_default_connection();",
        "    if (conn == nullptr) {",
        "        return VK_ERROR_INITIALIZATION_FAILED;",
        "    }",
        "    VkInstance server_instance = VK_NULL_HANDLE;",
        f"    const VkResult __rc = ::lsw::vkproto::lsw_{cmd.name}("
        + "conn->transport(), "
        + ", ".join(forwarded)
        + ");",
        "    if (__rc != VK_SUCCESS) {",
        "        ::lsw::icd::release_connection(conn);",
        "        return __rc;",
        "    }",
        f"    *{out_inst.name} = ::lsw::icd::wrap_server_handle(server_instance, conn);",
        "    return __rc;",
    ]


def _emit_destroy_instance(cmd: Command) -> list[str]:
    """vkDestroyInstance: extract Connection from instance, do call, free both."""
    inst_param = next(
        (p for p in cmd.params
         if p.type.base == "VkInstance" and p.type.pointer_depth == 0),
        None,
    )
    assert inst_param is not None, "vkDestroyInstance must take VkInstance"

    forwarded: list[str] = []
    for p in cmd.params:
        # Pass the wrapped handle through; serialize_<H> in the wire
        # layer routes through ``handle_to_wire`` which the ICD overrides
        # to do the unwrap. Avoiding it here prevents double-unwrap.
        forwarded.append(p.name)

    return [
        f"    if ({inst_param.name} == VK_NULL_HANDLE) {{",
        "        return;",
        "    }",
        f"    auto* conn = ::lsw::icd::connection_of({inst_param.name});",
        f"    ::lsw::vkproto::lsw_{cmd.name}(conn->transport(), "
        + ", ".join(forwarded) + ");",
        f"    ::lsw::icd::destroy_wrapper({inst_param.name});",
        "    ::lsw::icd::release_connection(conn);",
    ]


def _emit_default_body(cmd: Command) -> list[str]:
    """Generic wrapper: unwrap input handles, call, wrap output handles."""
    is_void = cmd.return_type.base == "void"
    pre_call: list[str] = []
    post_call: list[str] = []
    call_args: list[str] = []
    has_handle_array_out = False

    parent = _first_input_dispatchable(cmd)
    if parent is None:
        # No input dispatchable handle and not vkCreateInstance — should not
        # happen in our profile, but assert loudly if a future command lands
        # here. We could fall back to a default connection, but anything in
        # this position is suspicious.
        raise ValueError(
            f"command {cmd.name} has no input dispatchable handle; "
            "either special-case it (like vkCreateInstance) or audit the profile"
        )

    # A recorded command never touches the Connection: its bytes go into
    # the command buffer's own batch, and it has no handle outputs to
    # wrap. Emitting the lookup anyway would be an unused variable in
    # every one of the hundred-odd vkCmd* wrappers.
    if not _is_recordable(cmd, parent):
        pre_call.append(
            f"    auto* conn = ::lsw::icd::connection_of({parent.name});"
        )

    # Where this command's bytes go. For a recorded command that is the
    # command buffer's own batch, which is a Transport that appends
    # instead of sending; for everything else it is the connection's
    # real transport. The generated body is otherwise identical, which
    # is the point — nothing below here knows batching exists.
    if _is_recordable(cmd, parent):
        transport = f"::lsw::icd::record_target({parent.name})"
    else:
        transport = "conn->transport()"

    i = 0
    while i < len(cmd.params):
        p = cmd.params[i]

        # count_array_pair: dispatchable handle output is a special case.
        if _is_count_array_pair(cmd.params, i):
            count = cmd.params[i]
            arr = cmd.params[i + 1]
            if arr.type.base in DISPATCHABLE_HANDLES:
                vec_name = f"server_{arr.name}"
                # Query-only mode forwards directly. Dispatchable handle
                # args pass through unwrapped — ``serialize_<H>`` does the
                # unwrap via ``handle_to_wire`` so double-unwrap doesn't
                # happen.
                pre_call.append(f"    if ({arr.name} == nullptr) {{")
                forward_args: list[str] = []
                for q in cmd.params:
                    if q.name == arr.name:
                        forward_args.append("nullptr")
                    else:
                        forward_args.append(q.name)
                pre_call.append(
                    f"        return ::lsw::vkproto::lsw_{cmd.name}("
                    + transport + ", " + ", ".join(forward_args) + ");"
                )
                pre_call.append("    }")
                pre_call.append(
                    f"    std::vector<{arr.type.base}> {vec_name}(*{count.name}, VK_NULL_HANDLE);"
                )
                call_args.append(count.name)
                call_args.append(f"{vec_name}.data()")
                post_call.append(
                    f"    for (std::uint32_t k = 0; k < *{count.name}; ++k) {{"
                )
                post_call.append(
                    f"        {arr.name}[k] = "
                    f"::lsw::icd::wrap_server_handle({vec_name}[k], conn);"
                )
                post_call.append("    }")
                has_handle_array_out = True
            else:
                # Plain count_array_pair, no special handling.
                call_args.append(count.name)
                call_args.append(arr.name)
            i += 2
            continue

        if p.type.base in DISPATCHABLE_HANDLES and p.type.pointer_depth == 0:
            # Pass the wrapped handle through; serialize_<H> on the wire
            # layer unwraps via ``handle_to_wire`` (the ICD's strong
            # override). Avoids double-unwrap.
            call_args.append(p.name)
        elif (
            p.type.base in DISPATCHABLE_HANDLES
            and p.type.pointer_depth == 1
            and p.direction == "out"
            and p.len_expr is None
        ):
            # SINGLE dispatchable handle output (e.g. vkCreateDevice's pDevice).
            local = f"server_{p.name}"
            pre_call.append(f"    {p.type.base} {local} = VK_NULL_HANDLE;")
            call_args.append(f"&{local}")
            post_call.append(
                f"    *{p.name} = ::lsw::icd::wrap_server_handle({local}, conn);"
            )
        elif (
            p.type.base in DISPATCHABLE_HANDLES
            and p.type.pointer_depth == 1
            and p.direction == "out"
            and p.len_expr is not None
        ):
            # Dispatchable handle ARRAY output where the length lives in
            # a sibling struct member (e.g. vkAllocateCommandBuffers's
            # pCommandBuffers, len="pAllocateInfo->commandBufferCount";
            # vkAllocateDescriptorSets's pDescriptorSets too). The wire
            # layer fills a temporary vector with raw server handles,
            # then we wrap each into the caller's array so subsequent
            # unwrap calls find an LswWrappedHandle and not a raw value.
            vec = f"server_{p.name}"
            # Rewrite ``X->Y`` as ``X->Y`` for the count expression — it
            # references the caller's actual struct pointer in scope.
            count_expr = p.len_expr
            pre_call.append(
                f"    std::vector<{p.type.base}> {vec}({count_expr}, VK_NULL_HANDLE);"
            )
            call_args.append(f"{vec}.data()")
            post_call.append(
                f"    for (std::uint32_t k = 0; k < static_cast<std::uint32_t>({count_expr}); ++k) {{"
            )
            post_call.append(
                f"        {p.name}[k] = ::lsw::icd::wrap_server_handle({vec}[k], conn);"
            )
            post_call.append("    }")
            has_handle_array_out = True
        else:
            call_args.append(p.name)
        i += 1

    body: list[str] = []
    body.extend(pre_call)
    call_str = (
        f"::lsw::vkproto::lsw_{cmd.name}({transport}, "
        + ", ".join(call_args) + ")"
    )
    if is_void:
        body.append(f"    {call_str};")
        body.extend(post_call)
    elif post_call or has_handle_array_out:
        body.append(f"    const {cmd.return_type.cpp()} __rc = {call_str};")
        body.append("    if (__rc != VK_SUCCESS && __rc != VK_INCOMPLETE) {")
        body.append("        return __rc;")
        body.append("    }")
        body.extend(post_call)
        body.append("    return __rc;")
    else:
        body.append(f"    return {call_str};")
    return body


def _emit_wrapper_body(cmd: Command) -> list[str]:
    if cmd.name == "vkCreateInstance":
        return _emit_create_instance(cmd)
    if cmd.name == "vkDestroyInstance":
        return _emit_destroy_instance(cmd)
    return _emit_default_body(cmd)


# ---- Output ---------------------------------------------------------------


def emit(reg: Registry, out_dir: Path) -> None:
    cmds = sorted(reg.commands.values(), key=lambda c: c.cmd_id)

    # ---- Header ----
    h = [
        GENERATED_BANNER,
        "",
        "#include <cstdint>",
        "#include <span>",
        "",
        "#include <vulkan/vulkan_core.h>",
        "",
        "namespace lsw::icd::generated {",
        "",
        "// Wrapper functions matching the application-facing Vulkan signatures.",
        "// Each one looks up the per-VkInstance Connection (or opens a fresh",
        "// one in vkCreateInstance), unwraps dispatchable handles, calls the",
        "// generated lsw_vk* dispatch over that Connection's transport, and",
        "// wraps any returned dispatchable handles back into LswWrappedHandle",
        "// pointers carrying the same Connection.",
        "",
    ]
    for cmd in cmds:
        h.append(_signature(cmd) + ";")
    h.append("")
    h.append("// Name -> function-pointer entries for vk_icdGetInstanceProcAddr.")
    h.append("struct Entry {")
    h.append("    const char* name;")
    h.append("    PFN_vkVoidFunction fn;")
    h.append("};")
    h.append("")
    h.append("// Span of generated wrapper entries. The ICD layer prepends its own")
    h.append("// non-wire entries (loader negotiation, GetInstanceProcAddr, etc.).")
    h.append("std::span<const Entry> wrapper_entries() noexcept;")
    h.append("")
    h.append("}  // namespace lsw::icd::generated")
    h.append("")
    write(out_dir / "vk_icd_entries.h", header_guard("\n".join(h[1:])))

    # ---- Source ----
    s = [
        GENERATED_BANNER,
        "",
        '#include "vk_icd_entries.h"',
        "",
        "#include <array>",
        "#include <vector>",
        "",
        '#include "lsw/icd/connection.hpp"',
        '#include "lsw/icd/handle_wrap.hpp"',
        '#include "vk_dispatch_client.h"',
        "",
        "namespace lsw::icd::generated {",
        "",
    ]
    for cmd in cmds:
        s.append(_signature(cmd) + " {")
        s.extend(_emit_wrapper_body(cmd))
        s.append("}")
        s.append("")

    # Note: not ``constexpr`` -- reinterpret_cast to function pointer isn't a
    # constant expression. Plain runtime-initialised globals are fine here.
    s.append("namespace {")
    s.append(f"const std::array<Entry, {len(cmds)}> kWrapperTable = {{{{")
    for cmd in cmds:
        s.append(
            f'    Entry{{ "{cmd.name}", '
            f"reinterpret_cast<PFN_vkVoidFunction>(&Wrap_{cmd.name}) }},"
        )
    s.append("}};")
    s.append("}  // namespace")
    s.append("")
    s.append(
        "std::span<const Entry> wrapper_entries() noexcept { "
        "return {kWrapperTable.data(), kWrapperTable.size()}; }"
    )
    s.append("")
    s.append("}  // namespace lsw::icd::generated")
    s.append("")
    write(out_dir / "vk_icd_entries.cpp", "\n".join(s[1:]))
