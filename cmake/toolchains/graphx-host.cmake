# GraphX native host toolchain
#
# This file enforces the project invariant that host builds are supported on
# Linux and macOS. It also selects sane default compilers per host when the
# caller did not already set CMAKE_C_COMPILER/CMAKE_CXX_COMPILER.

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
    set(_graphx_c_candidates gcc-14 gcc clang)
    set(_graphx_cxx_candidates g++-14 g++ clang++)
    set(_graphx_cxx_standard_flag "-std=gnu++26")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_graphx_c_candidates clang gcc)
    set(_graphx_cxx_candidates clang++ g++)
    # c++2c is broadly accepted across modern AppleClang/Clang versions.
    set(_graphx_cxx_standard_flag "-std=c++2c")
else()
    message(FATAL_ERROR
        "GraphX toolchain only supports Linux and macOS targets. "
        "Detected target system: ${CMAKE_SYSTEM_NAME}")
endif()

if(NOT DEFINED CMAKE_C_COMPILER)
    find_program(_graphx_default_c_compiler NAMES ${_graphx_c_candidates})
    if(_graphx_default_c_compiler)
        set(CMAKE_C_COMPILER "${_graphx_default_c_compiler}" CACHE FILEPATH
            "GraphX default C compiler" FORCE)
    endif()
endif()

if(NOT DEFINED CMAKE_CXX_COMPILER)
    find_program(_graphx_default_cxx_compiler NAMES ${_graphx_cxx_candidates})
    if(_graphx_default_cxx_compiler)
        set(CMAKE_CXX_COMPILER "${_graphx_default_cxx_compiler}" CACHE FILEPATH
            "GraphX default C++ compiler" FORCE)
    endif()
endif()

set(GRAPHX_CXX_STANDARD_FLAG "${_graphx_cxx_standard_flag}" CACHE STRING
    "GraphX compiler flag used to enable C++26 mode")
