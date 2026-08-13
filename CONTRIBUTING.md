# Contributing

## Development setup

```sh
git clone <repo> linux-subsystem-windows
cd linux-subsystem-windows
git submodule update --init --recursive
```

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Default options:

- `LSW_BUILD_SERVER` (Linux only)
- `LSW_BUILD_CLIENT`
- `LSW_BUILD_MANAGER` (Linux only)
- `LSW_BUILD_AGENT` (Windows only)

Disable any with `-DLSW_BUILD_X=OFF`.

## Updating Vulkan-Headers

```sh
cd third_party/Vulkan-Headers
git fetch --tags
git checkout vX.Y.Z
cd ../..
git add third_party/Vulkan-Headers
```

After bumping, regenerate codegen output:

```sh
cmake --build build --target regenerate-vulkan
```

CI will fail on a regen-vs-committed mismatch.

## Codegen development

```sh
cd codegen
python -m venv .venv
source .venv/bin/activate
pip install -e .
lsw-codegen --xml ../third_party/Vulkan-Headers/registry/vk.xml --out /tmp/out
```

## Style

- C++: `.clang-format` and `.clang-tidy` at repo root. Run `clang-format -i` before committing.
- Python: `ruff check codegen/`.
- Commit messages: imperative mood, ~70 char subject.

## Changing the host↔guest link

Anything the Linux manager and the Windows guest agent must agree on —
the envelope framing, the RPC command names and event topics, the
IVSHMEM frame-ring layout — lives in `src/linkproto/`, which BOTH
toolchains compile, and is specified in
[docs/link-protocol.md](docs/link-protocol.md). Change it there, once;
do not restate a constant on either side. The offsets are pinned by
`static_assert` and covered by `lsw_linkproto_test`, so a divergence is
a build failure rather than a garbled frame at runtime.

Adding a guest RPC: one new TU under
`src/agent/src/rpc/handlers/<group>/`, its command name added to
`lsw/linkproto/commands.hpp`, and the TU listed in
`src/agent/CMakeLists.txt`. The transport is not involved — including
for log verbosity and reply summaries, which are handler overrides
(`RpcHandler::silent`, `RpcHandler::summarize`).

Adding an agent-pushed event: implement `rpc::EventSource` and register
it with the connection's `EventPump` in `host_endpoint.cpp`.

## Where to find what

- [README.md](README.md#layout) — the source tree, one line per module.
- Each module's `CMakeLists.txt` header comment states what belongs in it
  and what does not.
- [docs/link-protocol.md](docs/link-protocol.md) — the normative
  host↔guest spec.
