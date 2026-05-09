# Serialization Codegen

A build-time tool that ingests Khronos's `vk.xml` and emits all the boilerplate needed to ship a Vulkan call from the client ICD to the render-server and back. This is the linchpin that lets us keep up with new Vulkan versions and extensions without hand-writing serialization for ~1500+ functions and ~3000+ structs.

## Inputs

- `third_party/Vulkan-Headers/registry/vk.xml` — pinned to a specific Vulkan-Headers tag.
- `third_party/Vulkan-Headers/registry/video.xml` — for video extensions (deferred; ignore in v1).
- `codegen/profile.toml` — our profile: which extensions to include, which to omit, which functions to mark sync vs async.

## Output

```
generated/
├── vk_protocol_ids.h          # enum of every command we serialize
├── vk_types.h                 # POD wire structs for any non-trivial Vulkan struct
├── vk_serialize.cpp           # serialize_<TypeName>(buf, &val) for every type
├── vk_serialize.h
├── vk_deserialize.cpp         # deserialize_<TypeName>(buf, &val_out)
├── vk_deserialize.h
├── vk_dispatch_client.cpp     # vkXxx wrappers — serialize, send, optionally wait reply
├── vk_dispatch_server.cpp     # cmd_id → unpack args → real vkXxx → reply
├── vk_handles.h               # which handle types remap, which pass through
└── vk_features.cpp            # generated feature enable/disable plumbing
```

## Tooling

Python 3.11+ is the right call for the generator. `vk.xml` parsing is well-understood (Khronos itself ships a Python parser at `registry/generator.py`); reusing it saves significant work.

```
codegen/
├── pyproject.toml
├── lsw_codegen/
│   ├── __init__.py
│   ├── __main__.py             # python -m lsw_codegen --xml ... --out ...
│   ├── parse.py                # wraps Khronos generator.Registry to load vk.xml
│   ├── ir.py                   # our intermediate representation (typed dataclasses)
│   ├── lower.py                # vk.xml DOM → IR; resolves <len>, <values>, sType, pNext
│   ├── annotate.py             # applies profile.toml: sync/async, included extensions
│   ├── emit/
│   │   ├── ids.py              # vk_protocol_ids.h
│   │   ├── types.py            # vk_types.h
│   │   ├── serialize.py        # vk_serialize.{h,cpp}
│   │   ├── deserialize.py      # vk_deserialize.{h,cpp}
│   │   ├── client.py           # vk_dispatch_client.cpp
│   │   ├── server.py           # vk_dispatch_server.cpp
│   │   └── helpers.py          # shared C++ formatting
│   └── templates/              # jinja2 templates
└── tests/
    ├── test_lower.py
    └── snapshot/               # golden generated files for diffing
```

The output is checked in (under `generated/`) so consumers don't need Python at build time. CI re-runs the generator and fails if outputs differ from the committed copy.

## IR shape (sketch)

```python
@dataclass
class TypeRef:
    name: str
    is_pointer: bool
    is_const: bool
    array_len: Optional[ArrayLen]   # static [N], or "len=" expr, or None

@dataclass
class StructMember:
    name: str
    type: TypeRef
    optional: bool                  # from <member optional="true">
    len_expr: Optional[str]         # from <member len="...">
    values: list[str]               # from <member values="..."> for sType
    extension_struct: bool          # part of pNext chain?

@dataclass
class Struct:
    name: str
    members: list[StructMember]
    is_returned_only: bool          # <type returnedonly="true">
    extends: list[str]              # <type structextends="...">

@dataclass
class Param:
    name: str
    type: TypeRef
    direction: Literal["in", "out", "inout"]
    optional: bool
    len_expr: Optional[str]

@dataclass
class Command:
    name: str
    return_type: TypeRef
    params: list[Param]
    success_codes: list[str]
    error_codes: list[str]
    queue_constraints: list[str]    # render, compute, ...
    sync_mode: Literal["sync", "async", "auto"]   # from profile.toml
```

## What's tricky

Vulkan's XML schema is rich. The generator must handle all of these correctly or the wire protocol breaks.

### 1. `<len>` attribute

Defines the length of a pointer parameter. Examples:

- `len="null-terminated"` → strlen + 1 bytes.
- `len="dataSize"` → another parameter named `dataSize`.
- `len="pAllocateInfo->descriptorSetCount"` → field of another parameter.
- `len="latexmath:[\lceil{\mathit{rasterizationSamples} \over 32}\rceil]"` → ignore the LaTeX, parse the alternate form `altlen=`.
- Multi-dim: `len="codeSize / 4"` for SPIR-V → expression evaluation.

We need a tiny expression evaluator that resolves these at serialize time against actual parameter values.

### 2. `pNext` chains

`pNext` is a linked list of extension structs. Each has its own `sType`. We serialize:

```
[uint32 sType] [uint32 pNext-present?] [struct payload] (recurse)
```

The serializer dispatches by `sType` at each link. The codegen must know the full set of extension structs for each `pNext` chain root (from `structextends="..."`). New extensions just add to this set; the dispatch is data-driven.

### 3. Unions

`VkClearValue`, `VkClearColorValue`, `VkPerformanceCounterResultKHR`, etc. The discriminator is context-dependent (e.g., `VkClearValue` discriminator is the attached attachment's format). The codegen treats unions as "send the largest member, deal with it server-side" — this wastes a few bytes but eliminates a whole class of bugs.

### 4. Handle types

Every handle is 64-bit. Codegen emits a per-type `Handle<T>` wrapper with a packed 64-bit value. On the wire it's just 8 bytes. Whether it's remapped or pass-through is per-type metadata in `vk_handles.h`.

### 5. Variable-output sizes

```c
vkEnumeratePhysicalDevices(instance, &count, NULL);  // count fills in
vkEnumeratePhysicalDevices(instance, &count, devices); // returns devices
```

The "two-call idiom" needs special handling: first call passes `NULL` for the array — the codegen recognizes this from `optional="false,true"` on the array param. The wire frame indicates "size query" and the reply contains only the count. The second call sends the count and gets a count-prefixed array back.

### 6. Function pointers / callbacks

`pfnUserCallback` in debug-utils, `pfnAllocation` in allocators, etc. Function pointers don't cross machine boundaries:
- Allocator callbacks (`VkAllocationCallbacks*`): always pass NULL to the server. The host driver allocates with its default allocator. The client's allocator, if any, is honored only for client-side bookkeeping.
- Debug callbacks: server logs are ferried back to the client as messages, then dispatched into the registered callback in the guest.

### 7. Platform-specific types

`HWND`, `HINSTANCE`, `HANDLE`, `xcb_connection_t*`, `wl_display*`, etc. The codegen emits per-platform serializers but our **server only ever sees Linux platform types and our synthetic surface ID type**. Win32 surface creation is intercepted in handwritten code, not generated.

A profile flag `[platforms] include = ["common", "linux", "synthetic"]` keeps Win32-specific structs out of the generated server code.

### 8. Aliases and promotions

`VK_KHR_descriptor_indexing` was promoted to core 1.2. The XML lists both names with `<command alias="...">`. Codegen emits a single underlying implementation and forwarders for the aliases.

### 9. SPIR-V

`pCode` in `VkShaderModuleCreateInfo` is a `uint32_t*` of length `codeSize / 4`. Codegen handles via the `len` expression. We may also want to **validate** SPIR-V on the server side before passing to the driver — that's a runtime concern, not codegen, but the generator could emit hooks.

### 10. Versioning

`vk.xml` is keyed by Vulkan version. We pin one. Bumping requires:
- Update `Vulkan-Headers` submodule.
- Re-run codegen.
- Diff `vk_protocol_ids.h` — any IDs that changed mean the protocol version must bump.

To make the protocol stable across regenerations, we **don't auto-assign command IDs from the XML order**. Instead, `codegen/protocol_ids.toml` is a hand-curated map of `command name → stable ID`, and the generator errors on any new command without an assigned ID. New commands are appended at the end of that file with the next free ID. Removed commands keep their slots (mark them deprecated).

## Profile file

```toml
# codegen/profile.toml
api_version = "1.3.296"

[extensions]
include = [
  "VK_KHR_swapchain",
  "VK_KHR_surface",
  "VK_KHR_external_memory_fd",
  "VK_EXT_descriptor_indexing",
  # ... DXVK / VKD3D required set
]
exclude = [
  "VK_KHR_external_memory_win32",     # makes no sense host-side
  "VK_KHR_external_semaphore_win32",
  "VK_NV_acquire_winrt_display",
  "VK_GGP_*",
  "VK_FUCHSIA_*",
  "VK_QNX_*",
]

[platforms]
include = ["common", "linux"]

[sync_overrides]
# Override default sync detection for these commands
"vkQueueSubmit" = "async"   # default sync; override to async optimistic
"vkQueueSubmit2" = "async"
```

## Running

```sh
python -m lsw_codegen \
    --xml third_party/Vulkan-Headers/registry/vk.xml \
    --profile codegen/profile.toml \
    --out generated/
```

CMake target `regenerate-vulkan` runs this. `--check` mode runs it to a tempdir and diffs against `generated/`, used in CI.

## Testing the generator

- **Snapshot tests**: golden generated files for a small fake `vk.xml` stub. Easier to debug than a full Vulkan registry.
- **Round-trip tests**: pick a representative struct (e.g., `VkGraphicsPipelineCreateInfo`), populate it, serialize, deserialize, check field-by-field equality.
- **Fuzz tests**: random valid arg packs through the generated dispatch tables. Catches alignment / bounds bugs.

## Why Python and not C++

- `vk.xml` parsing is the slow part conceptually; Python's overhead is irrelevant against the file's size.
- jinja2 makes templated emission readable.
- Khronos's own generator is Python — we can lift its `Registry` parser instead of reimplementing.
- The output is C++; the generator's language doesn't infect runtime.

## Why hand-rolled and not Mesa's venus?

Mesa's `venus` is the equivalent for virtio-gpu. We're not using virtio-gpu; we're using vsock with our own transport and per-app windows. The serialization machinery overlaps but the framing, handle model, and presentation paths differ enough that forking venus would create more complexity than it saves. Studying venus's codegen design is worth doing — borrow ideas, not code.
