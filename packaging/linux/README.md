# Linux packaging

## ICD manifest

The Vulkan loader on Linux discovers ICDs from JSON manifests under one
of these search paths (highest priority first):

1. `$VK_DRIVER_FILES` — colon-separated list of explicit JSON paths
   (preferred for development; takes precedence over everything below).
2. `$VK_ICD_FILENAMES` — deprecated equivalent.
3. `$XDG_CONFIG_HOME/vulkan/icd.d/*.json` (default `~/.config`)
4. `$XDG_CONFIG_DIRS/vulkan/icd.d/*.json`
5. `/etc/vulkan/icd.d/*.json`
6. `/usr/local/share/vulkan/icd.d/*.json`
7. `/usr/share/vulkan/icd.d/*.json`

`lsw_icd.json.in` is a CMake template; the build emits a concrete
`lsw_icd.json` next to the generated `liblsw_icd.so` so you can point
`VK_DRIVER_FILES` straight at it during development.

## Smoke-running the ICD against the system loader

Once the host has the Vulkan loader (`libvulkan.so.1`) installed:

```sh
# Terminal 1 — start the server:
./build/src/server/lsw-render-server --socket /tmp/lsw.sock

# Terminal 2 — run a Vulkan app under the LSW ICD only:
export VK_DRIVER_FILES=$PWD/build/lsw_icd.json
export LSW_SERVER_SOCKET=/tmp/lsw.sock
vulkaninfo --summary    # or any vk app
```

The Phase 2 ICD only exposes the seven-command set; tools that demand
e.g. `vkGetPhysicalDeviceFeatures` will report missing functions until
the codegen profile is expanded.
