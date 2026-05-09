"""Parameter classification + len-expression utilities.

Both ``client_dispatch`` and ``server_dispatch`` need to bucket each
parameter into one of a fixed set of shapes (in_pod, in_struct_ptr,
out_handle_array, etc.). They also need to evaluate vk.xml ``len=`` /
``altlen=`` expressions in the right context (caller side vs server
side, where in-struct-pointer params have been decoded into local
by-value structs).

Keeping all of that in one file lets future emitters (a tracing emitter,
a validating emitter, an alternate wire format) reuse the same
classification without dragging in client/server-specific code.
"""

from __future__ import annotations

from ..ir import Command, CType, Param, Registry


# ---- Shared signature helpers ---------------------------------------------


def param_cpp(t: CType) -> str:
    """Render a CType for a function-parameter slot.

    Fixed-array params (``const float blendConstants[4]``) decay to
    pointers in C function signatures, so we emit them as
    ``const float* blendConstants``. This keeps the dispatch signature
    callable from any code that has either a pointer or an array.
    """
    s = t.base
    eff_ptr = t.pointer_depth + (1 if t.fixed_dim is not None else 0)
    if eff_ptr:
        s += "*" * eff_ptr
    if t.is_const:
        s = "const " + s
    return s


# ---- Param shape predicates ----------------------------------------------


def is_alloc_callbacks(p: Param) -> bool:
    return p.type.base == "VkAllocationCallbacks"


def is_count_array_pair(params: list[Param], i: int) -> bool:
    """The two-call enumeration idiom: a ``uint32_t*`` count followed by an
    array param whose ``len=`` references the count."""
    if i + 1 >= len(params):
        return False
    a, b = params[i], params[i + 1]
    if a.type.base not in ("uint32_t", "size_t"):
        return False
    if a.type.pointer_depth != 1 or b.type.pointer_depth != 1:
        return False
    return b.len_expr == a.name


def stype_value_for_struct(reg: Registry, type_base: str) -> str | None:
    """Return the canonical ``sType`` value for an sType-bearing struct.

    Used to pre-fill ``sType`` on freshly-allocated output array elements
    server-side so the real Vulkan driver accepts the call.
    """
    s = reg.structs.get(type_base)
    if s is None or not s.has_stype:
        return None
    stype_member = s.members[0]
    if stype_member.values:
        return stype_member.values[0]
    return None


def len_is_sibling(params: list[Param], len_expr: str | None) -> bool:
    """True if ``len_expr`` resolves to a sibling param in the same command.

    Two patterns count:
      * ``len="X"`` where ``X`` is a by-value sibling -- e.g.
        ``const T* pX`` with ``len="COUNT"`` and a sibling
        ``uint32_t COUNT``.
      * ``len="X->Y"`` where ``X`` is an in-struct-ptr sibling -- e.g.
        ``VkCommandBuffer* pCommandBuffers`` with
        ``len="pAllocateInfo->commandBufferCount"`` where
        ``pAllocateInfo`` is a sibling
        ``const VkCommandBufferAllocateInfo*``.

    Output two-call enumeration (``uint32_t*`` count) is a separate
    pattern and goes through ``is_count_array_pair`` instead.
    """
    if not len_expr:
        return False
    for q in params:
        if q.name == len_expr and q.type.pointer_depth == 0:
            return True
    if "->" in len_expr:
        head = len_expr.split("->", 1)[0].strip()
        for q in params:
            if q.name == head and q.type.pointer_depth == 1:
                return True
    return False


def server_len_expr(params: list[Param], len_expr: str | None) -> str:
    """Rewrite a vk.xml ``len`` expression for server-side use.

    Server-side, in-struct-ptr params get decoded into ``X_local`` (a
    by-value struct), so ``X->Y`` must become ``X_local.Y``. Sibling
    by-value names need no rewrite.
    """
    if not len_expr:
        return ""
    if "->" not in len_expr:
        return len_expr
    head, _, rest = len_expr.partition("->")
    head = head.strip()
    rest = rest.strip()
    for q in params:
        if q.name == head and q.type.pointer_depth == 1:
            return f"{head}_local.{rest}"
    return len_expr


# ---- Classification --------------------------------------------------------


def classify(reg: Registry, p: Param, params: list[Param] | None = None) -> str:
    if is_alloc_callbacks(p):
        return "in_null"
    if p.direction == "in" and p.type.base in reg.handles and p.type.pointer_depth == 0:
        return "in_handle"
    # Fixed-size input array: ``const float blendConstants[4]``.
    if p.direction == "in" and p.type.fixed_dim is not None and p.type.pointer_depth == 0:
        return "in_fixed_array"
    if p.direction == "in" and p.type.pointer_depth == 0:
        return "in_pod"
    if p.direction == "in" and p.type.base == "char" and p.type.pointer_depth == 1:
        return "in_string"
    # Input arrays (count is a sibling by-value param).
    if (
        p.direction == "in"
        and p.type.pointer_depth == 1
        and params is not None
        and len_is_sibling(params, p.len_expr)
    ):
        if p.type.base == "void":
            return "in_byte_array"
        if p.type.base in reg.handles:
            return "in_handle_array"
        if p.type.base in reg.structs:
            return "in_struct_array"
        return "in_pod_array"
    if p.direction == "in" and p.type.base in reg.structs and p.type.pointer_depth == 1:
        return "in_struct_ptr"
    # Output array with a sibling-by-value count.
    if (
        p.direction == "out"
        and p.type.pointer_depth == 1
        and params is not None
        and len_is_sibling(params, p.len_expr)
    ):
        if p.type.base in reg.handles:
            return "out_handle_array"
        if p.type.base in reg.structs:
            return "out_struct_array"
        return "out_pod_array"
    if p.direction == "out" and p.type.base in reg.handles and p.type.pointer_depth == 1:
        return "out_handle"
    if p.direction == "out" and p.type.base in reg.structs and p.type.pointer_depth == 1:
        return "out_struct"
    # Bare POD output: ``uint32_t* pImageIndex`` in vkAcquireNextImageKHR,
    # ``VkBool32* pSupported`` in vkGetPhysicalDeviceSurfaceSupportKHR.
    if p.direction == "out" and p.type.pointer_depth == 1:
        return "out_pod"
    raise ValueError(
        f"unhandled parameter shape for codegen: name={p.name!r} type={p.type.cpp()!r}"
    )


def signature(cmd: Command, *, prefix: str) -> str:
    """Render a function-prototype-style signature for ``cmd``.

    ``prefix`` is what comes before the parameter list, e.g.
    ``"VkResult lsw_vkCreateInstance"``. The caller decides whether to
    include a transport parameter, an ``override`` keyword, etc.
    """
    args = ", ".join(f"{param_cpp(p.type)} {p.name}" for p in cmd.params)
    return f"{prefix}({args})"


def trace_format_and_args(cmd: Command, reg: Registry,
                           *, value_prefix: str = "") -> tuple[str, list[str]]:
    """Build a (format_string, runtime_args) pair for tracing ``cmd``.

    The format string looks like ``"(device={}, ...)"`` with one ``{}``
    per scalar / handle param. Pointer and fixed-array params are
    rendered as ``=ptr`` literally so they don't need a runtime arg.

    ``value_prefix`` is prepended to each runtime arg name — useful on
    the server side where deserialized locals live behind ``...`` (no
    prefix) but in/out struct ptrs use ``..._local`` etc.; pass empty
    on the client side where param names are the function's own args.
    """
    parts: list[str] = []
    args: list[str] = []
    for p in cmd.params:
        if p.type.pointer_depth > 0 or p.type.fixed_dim is not None:
            parts.append(f"{p.name}=ptr")
        elif p.type.base in reg.handles:
            parts.append(f"{p.name}={{}}")
            args.append(f"static_cast<const void*>({value_prefix}{p.name})")
        elif p.type.base in ("float", "double"):
            parts.append(f"{p.name}={{}}")
            args.append(f"{value_prefix}{p.name}")
        else:
            # Catches uint32_t, VkFormat, VkSampleCountFlagBits, etc.
            # Cast to long long to avoid fmt's enum-formatter issues.
            parts.append(f"{p.name}={{}}")
            args.append(f"static_cast<long long>({value_prefix}{p.name})")
    return "(" + ", ".join(parts) + ")", args
