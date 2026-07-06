# GraphX native host toolchain
#
# This file enforces GraphX native-host policy only. Compiler selection is
# intentionally delegated to per-compiler toolchain files.

include_guard(GLOBAL)

if(NOT (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin"))
    message(FATAL_ERROR
        "GraphX host builds are supported only on Linux and macOS. "
        "Detected host: ${CMAKE_HOST_SYSTEM_NAME}")
endif()

set(GRAPHX_HOST_PLATFORM "${CMAKE_HOST_SYSTEM_NAME}" CACHE STRING
    "GraphX host platform detected by the GraphX toolchain")
set_property(CACHE GRAPHX_HOST_PLATFORM PROPERTY STRINGS Linux Darwin)

# Native host-only policy for GraphX build profiles.
if(NOT DEFINED CMAKE_SYSTEM_NAME)
    set(CMAKE_SYSTEM_NAME "${CMAKE_HOST_SYSTEM_NAME}" CACHE STRING
        "Target system name for native GraphX host builds")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GRAPHX_HOST_DEFAULT_CXX_STANDARD_FLAG "-std=gnu++26" CACHE STRING
        "Default GraphX C++ language mode flag for Linux hosts")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # c++2c is broadly accepted across modern AppleClang/Clang versions.
    set(GRAPHX_HOST_DEFAULT_CXX_STANDARD_FLAG "-std=c++2c" CACHE STRING
        "Default GraphX C++ language mode flag for macOS hosts")
else()
    message(FATAL_ERROR
        "GraphX toolchain only supports Linux and macOS targets. "
        "Detected target system: ${CMAKE_SYSTEM_NAME}")
endif()
