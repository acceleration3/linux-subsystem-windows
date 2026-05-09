"""Intermediate representation for the codegen pipeline.

vk.xml is large and quirky. We don't want every emitter touching the raw
DOM — `lower.py` boils it down to these dataclasses, and emitters consume
them. The IR is intentionally typed and frozen where possible.
"""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass(frozen=True)
class CType:
    """A C-level type reference with optional pointer indirection, const, fixed-array dim."""

    base: str
    pointer_depth: int = 0
    is_const: bool = False
    fixed_dim: int | None = None
    # If fixed_dim came from an API constant (e.g. ``VK_UUID_SIZE``), the
    # original symbol — useful for emitting readable code. ``None`` for
    # numeric literals.
    api_constant: str | None = None

    @property
    def is_fixed_array(self) -> bool:
        return self.fixed_dim is not None

    def cpp(self) -> str:
        s = self.base
        if self.pointer_depth:
            s += "*" * self.pointer_depth
        if self.is_const:
            s = "const " + s
        return s


@dataclass(frozen=True)
class Handle:
    name: str
    dispatchable: bool = True
    parent: str | None = None


@dataclass
class Param:
    name: str
    type: CType
    # Per-pointer-level optional flags from vk.xml `optional="false,true"`.
    # E.g. for a uint32_t* with optional="false,true": optional_levels = (False, True)
    # meaning the pointer is required (False) but the pointee is optional (True).
    # For a non-pointer with no optional attr: ().
    optional_levels: tuple[bool, ...] = ()
    len_expr: str | None = None
    direction: str = "in"  # "in" | "out" | "inout"


@dataclass
class Command:
    name: str
    return_type: CType
    params: list[Param]
    cmd_id: int
    sync_mode: str = "sync"  # "sync" | "async"
    success_codes: tuple[str, ...] = ()
    error_codes: tuple[str, ...] = ()


@dataclass
class StructMember:
    name: str
    type: CType
    optional_levels: tuple[bool, ...] = ()
    len_expr: str | None = None
    values: tuple[str, ...] = ()  # from ``values="VK_STRUCTURE_TYPE_..."``


@dataclass
class Struct:
    name: str
    members: list[StructMember]
    returned_only: bool = False
    extends: tuple[str, ...] = ()  # struct names this one can extend via pNext
    # Vulkan distinguishes ``struct`` and ``union`` types via the ``category``
    # attribute in vk.xml. Unions (``VkClearColorValue``, ``VkClearValue``)
    # are encoded by the codegen as raw bytes — every member overlaps the
    # same storage, so memcpy of the whole struct is the simplest correct
    # encoding. The discriminator is context-dependent (e.g. the format of
    # the image being cleared), so we don't try to be clever.
    is_union: bool = False

    @property
    def has_stype(self) -> bool:
        """Whether the struct's first member is the standard ``sType`` discriminator.

        Vulkan v1 structs (e.g. ``VkPhysicalDeviceFeatures``) have no sType;
        v2-and-later structs (e.g. ``VkPhysicalDeviceFeatures2``) do. The
        codegen treats the two differently for ``out_struct`` parameters:
        the sType-bearing form forwards sType + an empty pNext chain on the
        wire, while the non-sType form sends nothing.
        """
        return bool(self.members) and self.members[0].name == "sType"


@dataclass
class Registry:
    """Everything the emitters need."""

    handles: dict[str, Handle] = field(default_factory=dict)
    commands: dict[str, Command] = field(default_factory=dict)
    structs: dict[str, Struct] = field(default_factory=dict)
    # Struct names lowered in topological order (dependencies first), so the
    # emitter can place serializer definitions in a sane order. Only set after
    # ``lower.lower``.
    struct_order: list[str] = field(default_factory=list)
