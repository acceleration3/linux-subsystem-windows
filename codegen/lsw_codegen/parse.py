"""Parse vk.xml into raw element-trees we can lower into our IR.

We parse with stdlib ``xml.etree.ElementTree`` — Khronos's own ``Registry``
parser is heavier and brings transitive dependencies; the subset we need is
straightforward.
"""

from __future__ import annotations

import xml.etree.ElementTree as ET
from pathlib import Path

from .ir import CType


def load_xml(path: Path) -> ET.Element:
    return ET.parse(path).getroot()


def parse_param(el: ET.Element, api_consts: dict[str, int] | None = None) -> tuple[CType, str]:
    """Parse a ``<param>`` or ``<member>`` element.

    Returns the C type and the parameter name. Walks the element's
    text/tail interleavings: vk.xml encodes ``const VkFoo* pBar`` as
    ``const <type>VkFoo</type>* <name>pBar</name>``. Struct members can
    have a fixed array dimension after the name, either as a literal
    (``[3]``) or as an API-constant reference (``[<enum>VK_UUID_SIZE</enum>]``).
    """
    if api_consts is None:
        api_consts = {}

    is_const = "const" in (el.text or "")
    base: str | None = None
    pointer_depth = 0
    name: str | None = None
    fixed_dim: int | None = None
    api_constant: str | None = None

    for child in el:
        if child.tag == "type":
            base = (child.text or "").strip()
            tail = child.tail or ""
            pointer_depth += tail.count("*")
        elif child.tag == "name":
            name = (child.text or "").strip()
            tail = (child.tail or "").strip()
            # Literal fixed-array dimension, e.g. "[3]".
            if tail.startswith("[") and tail.endswith("]") and tail != "[":
                inner = tail[1:-1]
                if inner.isdigit():
                    fixed_dim = int(inner)
        elif child.tag == "enum":
            # API-constant fixed-array dimension, between [ and ] siblings.
            sym = (child.text or "").strip()
            if sym in api_consts:
                fixed_dim = api_consts[sym]
                api_constant = sym

    if base is None or name is None:
        raise ValueError(f"could not parse param: {ET.tostring(el)!r}")

    return (
        CType(
            base=base,
            pointer_depth=pointer_depth,
            is_const=is_const,
            fixed_dim=fixed_dim,
            api_constant=api_constant,
        ),
        name,
    )


def parse_api_constants(root: ET.Element) -> dict[str, int]:
    """Extract integer constants from the ``API Constants`` enums block."""
    out: dict[str, int] = {}
    for enums in root.findall("enums"):
        if enums.get("name") != "API Constants":
            continue
        for e in enums.findall("enum"):
            v = e.get("value")
            if v is None:
                continue
            try:
                out[e.get("name", "")] = int(v, 0)
            except ValueError:
                # Non-integer constants (floats, expressions) — skip.
                pass
    return out


def find_structs(root: ET.Element) -> dict[str, ET.Element]:
    """Return struct + union type elements keyed by name.

    Vulkan distinguishes ``struct`` and ``union`` via the ``category``
    attribute in vk.xml. We pull both into the same registry; downstream
    code uses ``Struct.is_union`` to decide on memcpy vs member-walk
    encoding.
    """
    out: dict[str, ET.Element] = {}
    types_el = root.find("types")
    if types_el is None:
        return out
    for t in types_el.findall("type"):
        if t.get("category") not in ("struct", "union"):
            continue
        if t.get("alias"):
            continue
        nm = t.get("name")
        if nm:
            out[nm] = t
    return out


def parse_optional(attr: str | None) -> tuple[bool, ...]:
    """Parse vk.xml ``optional="false,true"`` into a tuple of bools."""
    if not attr:
        return ()
    parts = [p.strip().lower() for p in attr.split(",")]
    return tuple(p == "true" for p in parts)


def find_handles(root: ET.Element) -> dict[str, dict]:
    """Return raw handle info keyed by name.

    Each entry: ``{"dispatchable": bool, "parent": str | None}``.
    """
    out: dict[str, dict] = {}
    types_el = root.find("types")
    if types_el is None:
        return out

    for t in types_el.findall("type"):
        if t.get("category") != "handle":
            continue
        if t.get("alias"):
            continue
        name_el = t.find("name")
        type_el = t.find("type")
        if name_el is None or type_el is None:
            continue
        name = (name_el.text or "").strip()
        macro = (type_el.text or "").strip()
        out[name] = {
            "dispatchable": macro == "VK_DEFINE_HANDLE",
            "parent": t.get("parent"),
        }
    return out


def find_command(root: ET.Element, name: str) -> ET.Element | None:
    """Locate a ``<command>`` element by name, resolving aliases."""
    commands_el = root.find("commands")
    if commands_el is None:
        return None

    # First pass: direct match.
    for cmd in commands_el.findall("command"):
        if cmd.get("alias"):
            continue
        proto = cmd.find("proto")
        if proto is None:
            continue
        if proto.findtext("name") == name:
            return cmd

    # Second pass: resolve aliases.
    for cmd in commands_el.findall("command"):
        if cmd.get("name") == name and cmd.get("alias"):
            return find_command(root, cmd.get("alias"))

    return None


def parse_codes(attr: str | None) -> tuple[str, ...]:
    if not attr:
        return ()
    return tuple(s.strip() for s in attr.split(",") if s.strip())


# ---- <feature> + <extension> walking ---------------------------------------


def _collect_commands_from_requires(parent: ET.Element) -> list[str]:
    """Walk every nested ``<require>`` block and gather ``<command name="...">`` entries.

    ``<require>`` may have a ``feature=`` or ``extension=`` attribute that
    gates inclusion; for the LSW codegen we always expand the full set
    that the API contains and let downstream selection decide what to
    serialize. Conditional inclusion can land later.

    (Note: a ``<require>`` can also contain ``<feature name="..."/>``
    children. Those name DEVICE features — ``robustBufferAccess`` and
    friends — not other feature blocks, and are not commands.)
    """
    out: list[str] = []
    for req in parent.findall("require"):
        for cmd in req.findall("command"):
            name = cmd.get("name")
            if name:
                out.append(name)
    return out


def _wants_api(api_attr: str | None) -> bool:
    """Return whether an api="..." attribute applies to plain ``vulkan``.

    Missing attribute = applies to all APIs. ``vulkansc`` only = skip.
    """
    if not api_attr:
        return True
    apis = {a.strip() for a in api_attr.split(",")}
    return "vulkan" in apis


def parse_features(root: ET.Element) -> dict[str, list[str]]:
    """Return ``feature_name -> commands`` for all ``<feature>`` blocks
    that apply to the ``vulkan`` API.

    ``feature_name`` looks like ``"VK_VERSION_1_0"``; the corresponding
    ``number`` attribute is parsed by callers via ``parse_feature_number``.
    """
    out: dict[str, list[str]] = {}
    for feat in root.findall("feature"):
        if not _wants_api(feat.get("api")):
            continue
        name = feat.get("name", "")
        if not name:
            continue
        out[name] = _collect_commands_from_requires(feat)
    return out


# Core commands used to live directly under ``VK_VERSION_1_x``. Since the
# registry was split into base/compute/graphics interface sets (around
# header 1.4.32x) those blocks hold the commands and ``VK_VERSION_1_x``
# only ``depends`` on them, so a walker that recognises the old name
# alone finds an API with no commands in it — the codegen then emits a
# protocol containing nothing but the extension entry points, silently.
#
# All four spellings carry the same ``number``, so treating them as the
# same version is exactly right; no depends-chasing needed.
_FEATURE_PREFIXES = (
    "VK_VERSION_",
    "VK_BASE_VERSION_",
    "VK_COMPUTE_VERSION_",
    "VK_GRAPHICS_VERSION_",
)


def parse_feature_number(name: str) -> tuple[int, int] | None:
    """``"VK_VERSION_1_3"`` -> ``(1, 3)``. Returns None if the name doesn't fit.

    Also accepts the split-interface spellings
    (``VK_BASE_VERSION_1_3`` and friends), which are the same version.
    """
    for prefix in _FEATURE_PREFIXES:
        if not name.startswith(prefix):
            continue
        parts = name.removeprefix(prefix).split("_")
        if len(parts) != 2 or not all(p.isdigit() for p in parts):
            return None
        return (int(parts[0]), int(parts[1]))
    return None


def parse_extensions(root: ET.Element) -> dict[str, dict]:
    """Return ``extension_name -> info`` for every supported extension.

    ``info`` keys: ``commands`` (list[str]), ``depends`` (list[str]),
    ``supported`` (str), ``platform`` (str | None), ``type`` (str).
    Extensions whose ``supported`` is ``disabled`` are still returned
    so callers can warn explicitly when they're requested.
    """
    out: dict[str, dict] = {}
    container = root.find("extensions")
    if container is None:
        return out
    for ext in container.findall("extension"):
        name = ext.get("name")
        if not name:
            continue
        # Some extensions list ``supported="vulkansc"`` only. We keep
        # them in the dict but flagged so callers can complain on request.
        out[name] = {
            "commands": _collect_commands_from_requires(ext),
            "depends": ext.get("depends", ""),
            "supported": ext.get("supported", ""),
            "platform": ext.get("platform"),
            "type": ext.get("type"),
        }
    return out
