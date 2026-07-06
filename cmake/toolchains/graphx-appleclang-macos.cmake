# GraphX AppleClang toolchain for macOS hosts.

include("${CMAKE_CURRENT_LIST_DIR}/graphx-host.cmake")

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR
        "graphx-appleclang-macos.cmake targets Darwin only. "
        "Detected target system: ${CMAKE_SYSTEM_NAME}")
endif()

set(CMAKE_C_COMPILER "clang" CACHE FILEPATH
    "GraphX AppleClang C compiler" FORCE)
set(CMAKE_CXX_COMPILER "clang++" CACHE FILEPATH
    "GraphX AppleClang C++ compiler" FORCE)
set(GRAPHX_CXX_STANDARD_FLAG "-std=c++2c" CACHE STRING
    "GraphX compiler flag used to enable C++26 mode" FORCE)
