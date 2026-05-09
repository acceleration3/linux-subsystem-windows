"""Lower a parsed vk.xml DOM into our IR.

Driven by ``profile.toml``: the command set is computed by walking
``<feature>`` blocks (every core API version up to and including
``profile.api_version``) plus the requested ``<extension>`` blocks
(transitively, via ``depends="..."``). Anything in
``[commands].exclude`` is dropped — those are typically platform-tied
entry points hand-written in ``src/client``.

If a command's parameter list contains shapes the codegen doesn't know
how to serialize yet (function pointers, untagged unions, certain
external-handle types), it's logged and skipped instead of failing the
whole build. The summary at the end tells you what got dropped.
"""

from __future__ import annotations

import sys
import xml.etree.ElementTree as ET
from typing import Iterable

from . import parse
from .ir import Command, Handle, Param, Registry, Struct, StructMember
from .profile import Profile, assign_missing_ids


def lower(root: ET.Element, profile: Profile) -> Registry:
    reg = Registry()

    api_consts = parse.parse_api_constants(root)

    # Pull in handles up front; we need them for parameter typing.
    raw_handles = parse.find_handles(root)
    for name, info in raw_handles.items():
        reg.handles[name] = Handle(
            name=name,
            dispatchable=bool(info["dispatchable"]),
            parent=info["parent"],
        )

    # ---- Resolve which commands are in scope ------------------------------
    selected = _select_commands(root, profile)

    # Validate / assign protocol IDs.
    sorted_selected = sorted(selected)
    new_ids = assign_missing_ids(profile, sorted_selected)
    if new_ids:
        print(
            f"codegen: assigned new wire IDs to {len(new_ids)} commands",
            file=sys.stderr,
        )

    # ---- Lower each command (skip on unsupported shape) -------------------
    skipped: list[tuple[str, str]] = []
    for cmd_name in sorted_selected:
        cmd_el = parse.find_command(root, cmd_name)
        if cmd_el is None:
            skipped.append((cmd_name, "not in vk.xml"))
            continue
        try:
            reg.commands[cmd_name] = _lower_command(cmd_el, cmd_name, api_consts, profile)
        except _UnsupportedShape as e:
            skipped.append((cmd_name, str(e)))

    if skipped:
        print(
            f"codegen: skipped {len(skipped)} commands due to unsupported "
            "param shapes:",
            file=sys.stderr,
        )
        for name, why in skipped:
            print(f"  {name}: {why}", file=sys.stderr)

    # ---- Walk the struct closure ------------------------------------------
    raw_structs = parse.find_structs(root)
    needed: set[str] = set()
    queue: list[str] = []
    omit = {"VkAllocationCallbacks"}

    def maybe_enqueue(type_name: str) -> None:
        if type_name in raw_structs and type_name not in needed:
            queue.append(type_name)

    for cmd in reg.commands.values():
        maybe_enqueue(cmd.return_type.base)
        for p in cmd.params:
            maybe_enqueue(p.type.base)

    while queue:
        name = queue.pop()
        if name in needed or name in omit:
            continue
        if name not in raw_structs:
            continue
        needed.add(name)
        for m_el in raw_structs[name].findall("member"):
            mtype, _ = parse.parse_param(m_el, api_consts)
            if mtype.base in raw_structs and mtype.base not in needed:
                queue.append(mtype.base)

    for name in needed:
        s_el = raw_structs[name]
        members: list[StructMember] = []
        for m_el in s_el.findall("member"):
            if m_el.get("api") == "vulkansc":
                continue
            mtype, mname = parse.parse_param(m_el, api_consts)
            optional = parse.parse_optional(m_el.get("optional"))
            len_expr = m_el.get("altlen") or m_el.get("len")
            values_attr = m_el.get("values") or ""
            values = tuple(s.strip() for s in values_attr.split(",") if s.strip())
            members.append(
                StructMember(
                    name=mname,
                    type=mtype,
                    optional_levels=optional,
                    len_expr=len_expr,
                    values=values,
                )
            )
        reg.structs[name] = Struct(
            name=name,
            members=members,
            returned_only=s_el.get("returnedonly") == "true",
            is_union=(s_el.get("category") == "union"),
        )

    reg.struct_order = _topo_sort(reg.structs)

    # Final validation pass: emitters classify each parameter into one of
    # a fixed set of shapes. Anything that fails to classify (function
    # pointers, untagged unions, exotic pointer-of-pointer-of-X) gets
    # dropped here so codegen emit stages can assume every Command is
    # emit-friendly. Done AFTER struct closure so the classifier sees
    # the full struct registry.
    from .emit.classify import classify, is_count_array_pair

    classify_failed: list[tuple[str, str]] = []
    for cmd_name in list(reg.commands):
        cmd = reg.commands[cmd_name]
        reason = _validation_reason(reg, cmd, classify, is_count_array_pair)
        if reason is not None:
            classify_failed.append((cmd_name, reason))
            del reg.commands[cmd_name]

    if classify_failed:
        print(
            f"codegen: skipped {len(classify_failed)} commands due to "
            "unsupported param shapes:",
            file=sys.stderr,
        )
        for name, why in classify_failed:
            print(f"  {name}: {why}", file=sys.stderr)

    return reg


def _validation_reason(reg, cmd, classify, is_count_array_pair) -> str | None:
    """Return a string explaining why ``cmd`` can't be emitted, or None
    if every param is in a shape the codegen handles."""
    # count_array_pair edge cases the emitters can't yet express:
    #   * size_t counts (only uint32_t handled)
    #   * void* element arrays (vkGetPipelineCacheData et al.)
    i = 0
    while i < len(cmd.params):
        if is_count_array_pair(cmd.params, i):
            count = cmd.params[i]
            arr = cmd.params[i + 1]
            if count.type.base != "uint32_t":
                return (
                    f"count_array_pair count {count.name!r} is "
                    f"{count.type.base}; only uint32_t handled"
                )
            if arr.type.base == "void":
                return (
                    f"count_array_pair element {arr.name!r} is void* "
                    "(opaque blob); needs bespoke serialization"
                )
            i += 2
        else:
            i += 1

    # Output ``void*`` byte buffers (e.g. vkGetQueryPoolResults pData)
    # would be an out_byte_array shape we don't emit yet.
    for p in cmd.params:
        if p.direction == "out" and p.type.pointer_depth == 1 and p.type.base == "void":
            return f"output void* buffer {p.name!r}; needs bespoke serialization"

    # Per-param classification.
    try:
        for p in cmd.params:
            classify(reg, p, cmd.params)
    except ValueError as e:
        return str(e)
    return None


# ---- Command selection -----------------------------------------------------


def _select_commands(root: ET.Element, profile: Profile) -> set[str]:
    features = parse.parse_features(root)
    extensions = parse.parse_extensions(root)

    selected: set[str] = set()

    # Core features: include every <feature> with version ≤ profile.api_version.
    for feat_name, cmds in features.items():
        version = parse.parse_feature_number(feat_name)
        if version is None:
            continue
        if version <= profile.api_version:
            selected.update(cmds)

    # Extensions: requested + transitive via ``depends``.
    visited: set[str] = set()
    queue: list[str] = list(profile.extensions)
    while queue:
        ext_name = queue.pop()
        if ext_name in visited:
            continue
        visited.add(ext_name)
        info = extensions.get(ext_name)
        if info is None:
            print(f"warning: extension {ext_name!r} not found in vk.xml", file=sys.stderr)
            continue
        if info["supported"] == "disabled":
            print(f"warning: extension {ext_name!r} is disabled in vk.xml", file=sys.stderr)
            continue
        if "vulkan" not in info["supported"].split(","):
            print(
                f"warning: extension {ext_name!r} doesn't support the vulkan API",
                file=sys.stderr,
            )
            continue
        selected.update(info["commands"])
        for dep in _split_depends(info["depends"]):
            queue.append(dep)

    # Apply the explicit exclude list (hand-written entry points).
    selected -= profile.excluded_commands
    return selected


def _split_depends(depends: str) -> Iterable[str]:
    """Split a ``depends="A+B,C"`` attribute into individual extension names.

    The vk.xml grammar uses ``,`` for OR and ``+`` for AND; for our
    selection purpose we just want the union of mentioned extensions, so
    we treat both delimiters the same.
    """
    if not depends:
        return ()
    out: list[str] = []
    for part in depends.replace(",", "+").split("+"):
        p = part.strip()
        if p and not parse.parse_feature_number(p):  # ignore version deps
            out.append(p)
    return out


# ---- Per-command lowering --------------------------------------------------


class _UnsupportedShape(Exception):
    """Raised by ``_lower_command`` when a parameter cannot be lowered."""


def _lower_command(cmd_el: ET.Element, cmd_name: str,
                   api_consts: dict[str, int], profile: Profile) -> Command:
    proto = cmd_el.find("proto")
    if proto is None:
        raise _UnsupportedShape("command missing <proto>")

    return_type, _ = parse.parse_param(proto, api_consts)

    params: list[Param] = []
    for p_el in cmd_el.findall("param"):
        if p_el.get("api") == "vulkansc":
            continue
        ptype, pname = parse.parse_param(p_el, api_consts)
        optional = parse.parse_optional(p_el.get("optional"))
        len_expr = p_el.get("altlen") or p_el.get("len")
        direction = _infer_direction(ptype, pname)
        params.append(
            Param(
                name=pname,
                type=ptype,
                optional_levels=optional,
                len_expr=len_expr,
                direction=direction,
            )
        )

    sync_mode = profile.sync_overrides.get(
        cmd_name, _default_sync_mode(return_type, params)
    )

    return Command(
        name=cmd_name,
        return_type=return_type,
        params=params,
        cmd_id=profile.command_ids[cmd_name],
        sync_mode=sync_mode,
        success_codes=parse.parse_codes(cmd_el.get("successcodes")),
        error_codes=parse.parse_codes(cmd_el.get("errorcodes")),
    )


# ---- Helpers ---------------------------------------------------------------


def _topo_sort(structs: dict[str, Struct]) -> list[str]:
    order: list[str] = []
    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(name: str) -> None:
        if name in visited:
            return
        if name in visiting:
            raise ValueError(f"cycle in struct dependencies through {name}")
        visiting.add(name)
        s = structs.get(name)
        if s is not None:
            for m in s.members:
                if m.type.pointer_depth == 0 and m.type.base in structs:
                    visit(m.type.base)
        visiting.discard(name)
        visited.add(name)
        order.append(name)

    for n in sorted(structs):
        visit(n)
    return order


def _infer_direction(t, name: str) -> str:  # noqa: ARG001
    if t.pointer_depth == 0:
        return "in"
    if t.is_const:
        return "in"
    return "out"


def _default_sync_mode(ret_type, params) -> str:
    if ret_type.base != "void":
        return "sync"
    for p in params:
        if p.direction == "out":
            return "sync"
    return "async"
