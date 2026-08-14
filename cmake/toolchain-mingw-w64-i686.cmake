# CMake toolchain for cross-compiling the Windows guest agent from
# Linux using mingw-w64. Used by scripts/build-agent.sh.
#
# Requires Arch packages: mingw-w64-gcc mingw-w64-headers mingw-w64-crt
# mingw-w64-winpthreads mingw-w64-binutils. Equivalent on Debian:
# gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(MINGW_PREFIX i686-w64-mingw32)
set(CMAKE_C_COMPILER   ${MINGW_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${MINGW_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${MINGW_PREFIX}-windres)
set(CMAKE_AR           ${MINGW_PREFIX}-ar)
set(CMAKE_RANLIB       ${MINGW_PREFIX}-ranlib)
set(CMAKE_STRIP        ${MINGW_PREFIX}-strip)

# Search libraries / headers in the mingw root, host system files
# only for the cmake modules / programs we explicitly fetch.
set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Static-link the C++ runtime + libgcc + winpthreads so the resulting
# .exe drops onto a stock Windows install with no extra DLLs needed.
# ``-mconsole`` forces the Windows console subsystem so the linker
# pulls the ``main`` startup object instead of expecting WinMain
# (mingw-w64 sometimes defaults the other way otherwise).
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-static -static-libgcc -static-libstdc++ -mconsole -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")

# Same treatment for shared libraries, and for the same reason: the
# Vulkan ICD is a DLL that has to load inside an arbitrary application
# on a stock Windows install. Left to the defaults it links against
# libstdc++-6.dll, libgcc_s_seh-1.dll and libwinpthread-1.dll, none of
# which are there — the driver would simply fail to load, with the
# loader reporting nothing more useful than "not found".
#
# ``-static`` is deliberately absent here: it is meaningless for a DLL
# and mingw warns about it.
set(CMAKE_SHARED_LINKER_FLAGS_INIT
    "-static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")
