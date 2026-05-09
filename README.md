# Linux Subsystem for Windows (LSW)

A "reverse WSL" — run Windows applications on a Linux host with per-application native windows, via a Vulkan command-stream proxy from the guest to the host.

## Status

Greenfield. Phase 0 scaffolding.

- **Design:** [docs/](docs/) — architecture, components, protocol, transport, roadmap.
- **Implementation state and next step:** [STATE.md](STATE.md).
- **Component overviews:**
  [render-server](docs/render-server.md) ·
  [client-icd](docs/client-icd.md) ·
  [vm-manager](docs/vm-manager.md) ·
  [serialization-codegen](docs/serialization-codegen.md) ·
  [protocol](docs/protocol.md) ·
  [transport](docs/transport.md)

## Building

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja
cmake --build build
```

Requires CMake 3.25+, a C++20 compiler, and (later phases) Vulkan + Wayland headers on the host.

## License

Apache-2.0. See [LICENSE](LICENSE).
