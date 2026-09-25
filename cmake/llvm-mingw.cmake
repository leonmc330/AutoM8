# Cross-compile for Windows from Linux with llvm-mingw (https://github.com/mstorsjo/llvm-mingw).
#
#   cmake -B build-win-x64   -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake -DMINGW_ARCH=x86_64
#   cmake -B build-win-arm64 -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake -DMINGW_ARCH=aarch64
#
# The llvm-mingw bin/ folder must be in PATH (or set LLVM_MINGW to its root folder).

set(MINGW_ARCH "x86_64" CACHE STRING "x86_64 or aarch64")
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR ${MINGW_ARCH})
set(triple ${MINGW_ARCH}-w64-mingw32)

if(DEFINED ENV{LLVM_MINGW})
	set(prefix "$ENV{LLVM_MINGW}/bin/")
else()
	set(prefix "")
endif()
set(CMAKE_C_COMPILER ${prefix}${triple}-clang)
set(CMAKE_CXX_COMPILER ${prefix}${triple}-clang++)
set(CMAKE_RC_COMPILER ${prefix}${triple}-windres)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
