# Guest cross-build: next steps on Ubuntu / Mint

The host build works fine on Ubuntu 26.04+ and Linux Mint (Qt 6.7+, GCC 14+ host,
libsdl3-dev all available). The only unresolved issue is the **Windows guest
cross-build** (`lsw-guest-agent.exe`, `remotewin-guest.exe`, Vulkan loaders).

## Root cause

Ubuntu and Mint ship **GCC 13 for `x86_64-w64-mingw32`**, configured for MSVCRT
(the legacy Windows C runtime). The project's source calls `_set_abort_behavior`,
which lives in UCRT (the modern Windows CRT), not in Ubuntu's `libmsvcrt.a`. All
272 source files compile cleanly; only the final link step fails.

The official build uses Arch Linux (GCC 14 + mingw-w64 14, UCRT by default).

---

## Option A — Build GCC 14 + mingw-w64 14 from source

Install the same toolchain Arch and CI use, locally in `~/.local/opt/`.

```sh
# 1. Prerequisites (Ubuntu 26.04 / Mint)
sudo apt-get install -y gcc g++ binutils make flex bison texinfo \
  libgmp-dev libmpfr-dev libmpc-dev libisl-dev

# 2. Build binutils for the target
wget https://ftp.gnu.org/gnu/binutils/binutils-2.43.tar.xz
tar xJf binutils-2.43.tar.xz && mkdir binutils-build && cd binutils-build
../binutils-2.43/configure \
  --target=x86_64-w64-mingw32 --prefix=$HOME/.local/opt/gcc14-mingw \
  --with-sysroot=$HOME/.local/opt/gcc14-mingw --disable-nls
make -j$(nproc) && make install && cd ..

# 3. mingw-w64 14 headers (no compiler needed yet)
wget https://github.com/mingw-w64/mingw-w64/archive/refs/tags/v14.0.0.tar.gz
tar xzf v14.0.0.tar.gz && cd mingw-w64-14.0.0/mingw-w64-headers
./configure --host=x86_64-w64-mingw32 \
  --prefix=$HOME/.local/opt/gcc14-mingw/x86_64-w64-mingw32
make install && cd ../..

# 4. GCC 14 (C/C++ only, first pass — no libc yet)
wget https://ftp.gnu.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.xz
tar xJf gcc-14.2.0.tar.xz && mkdir gcc-build && cd gcc-build
../gcc-14.2.0/configure \
  --target=x86_64-w64-mingw32 --prefix=$HOME/.local/opt/gcc14-mingw \
  --with-sysroot=$HOME/.local/opt/gcc14-mingw \
  --enable-languages=c,c++ --disable-nls \
  --enable-threads=posix --disable-multilib
make -j$(nproc) all-gcc && make install-gcc && cd ..

# 5. mingw-w64 CRT (UCRT)
cd mingw-w64-14.0.0/mingw-w64-crt
./configure --host=x86_64-w64-mingw32 \
  --prefix=$HOME/.local/opt/gcc14-mingw/x86_64-w64-mingw32 \
  --with-default-msvcrt=ucrt
make -j$(nproc) && make install && cd ../..

# 6. winpthreads
cd mingw-w64-14.0.0/mingw-w64-libraries/winpthreads
./configure --host=x86_64-w64-mingw32 \
  --prefix=$HOME/.local/opt/gcc14-mingw/x86_64-w64-mingw32
make -j$(nproc) && make install && cd ../../..

# 7. GCC 14 full (libstdc++ etc.)
cd gcc-build
make -j$(nproc) && make install && cd ..
```

Then set the toolchain prefix in cmake:

```sh
export PATH="$HOME/.local/opt/gcc14-mingw/bin:$PATH"
./scripts/build.sh
```

The cmake toolchain (`cmake/toolchain-mingw-w64.cmake`) already uses
`x86_64-w64-mingw32-g++`, which will resolve to the GCC 14 build when it is
first on `PATH`.

**Effort**: ~2 hours build time (mostly GCC). One-time setup.

---

## Option B — Fix the llvm-mingw path

llvm-mingw (Clang/LLVM cross-compiler) is already installed at
`~/.local/opt/llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64/`. It is UCRT-based
and has no MSVCRT conflict. It stalled because `libc++` is stricter than
`libstdc++` about transitive includes: some files in `external/remotewin/` use
`std::size_t` without explicitly including `<cstddef>`.

**To try it:**

```sh
PATH=~/.local/opt/llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64/bin:$PATH \
  ./scripts/build.sh 2>&1 | grep "error:" | sort -u
```

Each `error: 'size_t' is not a member of 'std'` requires adding
`#include <cstddef>` near the top of the offending file.  If the fix is a
handful of files in `external/remotewin/`, it is worth patching the submodule
(or contributing upstream). If the errors are widespread, prefer Option A.

**Effort**: low if few files need fixing; moderate if many.

---

## Option C — Host-only build + guest binaries from release (current)

Build only the Linux host side. Pull the pre-built Windows guest binaries
(`lsw-guest-agent.exe`, `remotewin-guest.exe`, Vulkan loaders) from the
latest GitHub release.

```sh
# Build the host side only
./scripts/build.sh --host-only

# Download and extract the release
mkdir -p /tmp/lsw-release
curl -L https://github.com/acceleration3/linux-subsystem-windows/releases/download/v0.2.3/lsw-0.2.3-x86_64.tar.zst \
  | tar --zstd -xf - -C /tmp/lsw-release

# Install host build, then overlay guest files
cmake --install build --prefix ~/.local --component lsw
cp -a /tmp/lsw-release/usr/share/lsw/guest ~/.local/share/lsw/
```

**When to use**: iterating on the manager, MCP server, or GPU stack where the
guest binaries are not what is changing. The manager auto-discovers the guest
files from the installed prefix.

**Limitation**: guest binaries are pinned to v0.2.3; re-sync after each
release if you need newer agent features.
