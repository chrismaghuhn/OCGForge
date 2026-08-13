# Optional local Windows fallback used when a native MSVC/LLVM toolchain is
# unavailable. The Zig archive is downloaded into the ignored repository-local
# cache.
set(CMAKE_C_COMPILER "${CMAKE_CURRENT_LIST_DIR}/../tools/zig-cc.cmd" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${CMAKE_CURRENT_LIST_DIR}/../tools/zig-cxx.cmd" CACHE FILEPATH "" FORCE)
set(CMAKE_AR "${CMAKE_CURRENT_LIST_DIR}/../tools/zig-ar.cmd" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${CMAKE_CURRENT_LIST_DIR}/../tools/zig-ranlib.cmd" CACHE FILEPATH "" FORCE)
set(CMAKE_C_COMPILER_AR "${CMAKE_CURRENT_LIST_DIR}/../tools/zig-ar.cmd" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER_AR "${CMAKE_CURRENT_LIST_DIR}/../tools/zig-ar.cmd" CACHE FILEPATH "" FORCE)
set(CMAKE_C_COMPILER_RANLIB "${CMAKE_CURRENT_LIST_DIR}/../tools/zig-ranlib.cmd" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER_RANLIB "${CMAKE_CURRENT_LIST_DIR}/../tools/zig-ranlib.cmd" CACHE FILEPATH "" FORCE)
