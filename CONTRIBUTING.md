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

## Where to find what

- [STATE.md](STATE.md) — current implementation phase and next step.
- [docs/](docs/) — design intent.
