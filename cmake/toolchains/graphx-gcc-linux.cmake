# GraphX GCC toolchain for Linux hosts.

include("${CMAKE_CURRENT_LIST_DIR}/graphx-host.cmake")

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "graphx-gcc-linux.cmake targets Linux only. "
        "Detected target system: ${CMAKE_SYSTEM_NAME}")
endif()

set(CMAKE_C_COMPILER "gcc-14" CACHE FILEPATH
    "GraphX GCC C compiler" FORCE)
set(CMAKE_CXX_COMPILER "g++-14" CACHE FILEPATH
    "GraphX GCC C++ compiler" FORCE)
set(GRAPHX_CXX_STANDARD_FLAG "-std=gnu++26" CACHE STRING
    "GraphX compiler flag used to enable C++26 mode" FORCE)
