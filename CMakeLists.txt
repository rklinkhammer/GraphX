cmake_minimum_required(VERSION 4.0.0)

project(gdashboard-workspace VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CheckIncludeFileCXX)

if(NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
    message(FATAL_ERROR
        "GraphX host builds are supported only on Linux and macOS. "
        "Detected target system: ${CMAKE_SYSTEM_NAME}")
endif()

if(NOT DEFINED GRAPHX_CXX_STANDARD_FLAG OR GRAPHX_CXX_STANDARD_FLAG STREQUAL "")
    if(DEFINED GRAPHX_HOST_DEFAULT_CXX_STANDARD_FLAG AND
       NOT GRAPHX_HOST_DEFAULT_CXX_STANDARD_FLAG STREQUAL "")
        message(WARNING
            "GRAPHX_CXX_STANDARD_FLAG was not set by a compiler toolchain preset. "
            "Using host default language mode '${GRAPHX_HOST_DEFAULT_CXX_STANDARD_FLAG}'.")
        set(GRAPHX_CXX_STANDARD_FLAG "${GRAPHX_HOST_DEFAULT_CXX_STANDARD_FLAG}")
    else()
        message(WARNING
            "GRAPHX_CXX_STANDARD_FLAG was not set by a toolchain preset. "
            "Falling back to compiler-id inference for ad-hoc/manual configure.")
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set(GRAPHX_CXX_STANDARD_FLAG "-std=gnu++26")
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
            set(GRAPHX_CXX_STANDARD_FLAG "-std=c++2c")
        else()
            message(FATAL_ERROR
                "Unable to infer a C++26 compiler flag for compiler '${CMAKE_CXX_COMPILER_ID}'. "
                "Configure with the GraphX toolchain file or set GRAPHX_CXX_STANDARD_FLAG manually.")
        endif()
    endif()
endif()

if(NOT GRAPHX_CXX_STANDARD_FLAG MATCHES "^-std=")
    message(FATAL_ERROR
        "GRAPHX_CXX_STANDARD_FLAG='${GRAPHX_CXX_STANDARD_FLAG}' is invalid. "
        "Expected a compiler language mode flag like -std=gnu++26 or -std=c++2c.")
endif()

# Set C++26 flag after project() to ensure it's applied to all targets.
add_compile_options("${GRAPHX_CXX_STANDARD_FLAG}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${GRAPHX_CXX_STANDARD_FLAG}")

# Don't set CMAKE_CXX_STANDARD since CMake 4.3 doesn't support C++26 mapping
# The flag is already in CMAKE_CXX_FLAGS_INIT
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(GRAPHX_BUILD_PROFILE "default" CACHE STRING
    "Logical build profile label (for example: default, linux-ninja-host)")
set_property(CACHE GRAPHX_BUILD_PROFILE PROPERTY STRINGS
    default
    linux-ninja-host)

if(NOT (GRAPHX_BUILD_PROFILE STREQUAL "default" OR
        GRAPHX_BUILD_PROFILE STREQUAL "linux-ninja-host"))
    message(FATAL_ERROR
        "Unsupported GRAPHX_BUILD_PROFILE='${GRAPHX_BUILD_PROFILE}'. "
        "Supported values: default, linux-ninja-host")
endif()

option(GRAPHX_REQUIRE_NINJA "Require Ninja generator for all GraphX configurations" ON)

if(GRAPHX_REQUIRE_NINJA)
    if(NOT CMAKE_GENERATOR MATCHES "^Ninja")
        message(FATAL_ERROR
            "GraphX requires the Ninja generator. "
            "Current generator: ${CMAKE_GENERATOR}. "
            "Reconfigure with: cmake -S . -B <build-dir> -G Ninja")
    endif()
endif()


# Build options
option(BUILD_TESTS "Build test targets" ON)
option(BUILD_DOCS "Build Doxygen documentation" OFF)
option(GRAPHX_BUILD_WEB_DASHBOARD "Build FHSS embedded web dashboard support" OFF)
if(WIN32 AND GRAPHX_BUILD_WEB_DASHBOARD)
    message(FATAL_ERROR
        "GRAPHX_BUILD_WEB_DASHBOARD is unsupported on Windows: secure reparse-point-safe "
        "static asset opening has not been implemented. Configure with "
        "-DGRAPHX_BUILD_WEB_DASHBOARD=OFF.")
endif()
option(GRAPHX_BUILD_EXAMPLES_SAR "Build the SAR example package under examples/SAR" ON)
option(GRAPHX_BUILD_EXAMPLES_DSP "Build the DSP example package under examples/DSP" ON)
option(ENABLE_TSAN "Build with ThreadSanitizer instrumentation" OFF)
option(ENABLE_ASAN_UBSAN
    "Build with AddressSanitizer and UndefinedBehaviorSanitizer instrumentation"
    OFF)
option(GRAPHX_ENABLE_EXTENDED_VALIDATION
    "Register scheduled statistical, stress, and extended validation lanes"
    OFF)
option(ENABLE_CUDA_GRAPH_NODES "Enable CUDA GPU graph nodes in libgpu" OFF)
option(ENABLE_METAL_GRAPH_NODES "Enable Metal GPU graph nodes in libgpu" ON)
option(ENABLE_METAL_NATIVE_RUNTIME "Enable native Metal runtime implementation on Apple" ON)
option(GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME
    "Fail configure/startup when native Metal runtime is requested but unavailable"
    OFF)
set(GRAPHX_METAL_CPP_INCLUDE_DIR "" CACHE PATH
    "Path to metal-cpp headers root containing Metal/Metal.hpp")
option(MULTI_GPU_TESTS "Enable multi-GPU test lanes (when supported)" OFF)

set(GRAPHX_GPU_TEST_HOOKS_ENABLED OFF)
if(BUILD_TESTS)
    set(GRAPHX_GPU_TEST_HOOKS_ENABLED ON)
endif()

find_package(CUDAToolkit QUIET)
set(GRAPHX_HAS_CUDA_TOOLKIT OFF)
if(CUDAToolkit_FOUND)
    set(GRAPHX_HAS_CUDA_TOOLKIT ON)
endif()

set(GRAPHX_HAS_METAL_PLATFORM OFF)
if(APPLE)
    set(GRAPHX_HAS_METAL_PLATFORM ON)
endif()

set(GRAPHX_HAS_METAL_FRAMEWORKS OFF)
set(GRAPHX_HAS_METAL_CPP_HEADERS OFF)
if(APPLE)
    find_library(GRAPHX_METAL_FRAMEWORK Metal)
    find_library(GRAPHX_FOUNDATION_FRAMEWORK Foundation)
    find_library(GRAPHX_QUARTZCORE_FRAMEWORK QuartzCore)

    set(_graphx_metal_cpp_hints "")
    if(GRAPHX_METAL_CPP_INCLUDE_DIR)
        list(APPEND _graphx_metal_cpp_hints "${GRAPHX_METAL_CPP_INCLUDE_DIR}")
    endif()
    list(APPEND _graphx_metal_cpp_hints
        "${CMAKE_SOURCE_DIR}/libgpu/include/metal-cpp"
        "${CMAKE_SOURCE_DIR}/third_party/metal-cpp"
        "${CMAKE_SOURCE_DIR}/third_party/metal-cpp/include"
        "/opt/homebrew/include"
        "/usr/local/include")

    unset(GRAPHX_METAL_CPP_INCLUDE_DIR_RESOLVED CACHE)
    unset(GRAPHX_METAL_CPP_INCLUDE_DIR_RESOLVED)

    find_path(GRAPHX_METAL_CPP_INCLUDE_DIR_RESOLVED
        NAMES Metal/Metal.hpp
        HINTS ${_graphx_metal_cpp_hints})

    if(GRAPHX_METAL_CPP_INCLUDE_DIR_RESOLVED)
        unset(GRAPHX_HAS_METAL_HEADER CACHE)
        unset(GRAPHX_HAS_FOUNDATION_HEADER CACHE)
        unset(GRAPHX_HAS_QUARTZCORE_HEADER CACHE)
        set(_graphx_prev_required_includes "${CMAKE_REQUIRED_INCLUDES}")
        set(CMAKE_REQUIRED_INCLUDES "${GRAPHX_METAL_CPP_INCLUDE_DIR_RESOLVED}")
        check_include_file_cxx("Metal/Metal.hpp" GRAPHX_HAS_METAL_HEADER)
        check_include_file_cxx("Foundation/Foundation.hpp" GRAPHX_HAS_FOUNDATION_HEADER)
        check_include_file_cxx("QuartzCore/QuartzCore.hpp" GRAPHX_HAS_QUARTZCORE_HEADER)
        set(CMAKE_REQUIRED_INCLUDES "${_graphx_prev_required_includes}")
    else()
        set(GRAPHX_HAS_METAL_HEADER OFF)
        set(GRAPHX_HAS_FOUNDATION_HEADER OFF)
        set(GRAPHX_HAS_QUARTZCORE_HEADER OFF)
    endif()

    if(GRAPHX_METAL_FRAMEWORK AND GRAPHX_FOUNDATION_FRAMEWORK AND GRAPHX_QUARTZCORE_FRAMEWORK)
        set(GRAPHX_HAS_METAL_FRAMEWORKS ON)
    endif()

    if(GRAPHX_HAS_METAL_HEADER AND GRAPHX_HAS_FOUNDATION_HEADER AND GRAPHX_HAS_QUARTZCORE_HEADER)
        set(GRAPHX_HAS_METAL_CPP_HEADERS ON)
    endif()
endif()

set(GRAPHX_CUDA_GRAPH_NODES_ENABLED OFF)
if(ENABLE_CUDA_GRAPH_NODES)
    if(GRAPHX_HAS_CUDA_TOOLKIT)
        set(GRAPHX_CUDA_GRAPH_NODES_ENABLED ON)
    else()
        message(WARNING
            "ENABLE_CUDA_GRAPH_NODES=ON but CUDAToolkit was not found. "
            "Disabling CUDA graph nodes for this configure.")
    endif()
endif()

set(GRAPHX_METAL_GRAPH_NODES_ENABLED OFF)
if(ENABLE_METAL_GRAPH_NODES)
    if(GRAPHX_HAS_METAL_PLATFORM)
        set(GRAPHX_METAL_GRAPH_NODES_ENABLED ON)
    else()
        message(WARNING
            "ENABLE_METAL_GRAPH_NODES=ON but this platform is not Apple/macOS. "
            "Disabling Metal graph nodes for this configure.")
    endif()
endif()

set(GRAPHX_METAL_NATIVE_RUNTIME_ENABLED OFF)
if(ENABLE_METAL_NATIVE_RUNTIME)
    if(GRAPHX_METAL_GRAPH_NODES_ENABLED AND GRAPHX_HAS_METAL_FRAMEWORKS AND GRAPHX_HAS_METAL_CPP_HEADERS)
        set(GRAPHX_METAL_NATIVE_RUNTIME_ENABLED ON)
    elseif(GRAPHX_METAL_GRAPH_NODES_ENABLED)
        message(WARNING
            "ENABLE_METAL_NATIVE_RUNTIME=ON but required metal-cpp headers and/or Apple frameworks were not found. "
            "Set GRAPHX_METAL_CPP_INCLUDE_DIR to your metal-cpp include root if needed. "
            "Falling back to simulated Metal capabilities.")
    endif()
endif()

if(GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME)
    if(NOT ENABLE_METAL_NATIVE_RUNTIME)
        message(FATAL_ERROR
            "GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME=ON requires ENABLE_METAL_NATIVE_RUNTIME=ON.")
    endif()

    if(NOT GRAPHX_METAL_GRAPH_NODES_ENABLED)
        message(FATAL_ERROR
            "GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME=ON requires Metal graph nodes to be enabled on Apple.")
    endif()

    if(NOT GRAPHX_METAL_NATIVE_RUNTIME_ENABLED)
        message(FATAL_ERROR
            "GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME=ON but native Metal runtime prerequisites are missing "
            "(frameworks and/or metal-cpp headers).")
    endif()
endif()

if(MULTI_GPU_TESTS AND NOT GRAPHX_CUDA_GRAPH_NODES_ENABLED)
    message(WARNING
        "MULTI_GPU_TESTS=ON but CUDA graph nodes are disabled. "
        "Tests may be skipped until a multi-device CUDA backend is enabled.")
endif()

if(ENABLE_TSAN)
    if(MSVC)
        message(FATAL_ERROR "ENABLE_TSAN is not supported with MSVC")
    endif()
endif()
if(ENABLE_TSAN AND ENABLE_ASAN_UBSAN)
    message(FATAL_ERROR
        "ENABLE_TSAN and ENABLE_ASAN_UBSAN are mutually exclusive")
endif()

# Shared project options target to avoid leaking global flags into all targets.
add_library(graphx_project_options INTERFACE)
target_compile_definitions(graphx_project_options INTERFACE
    GRAPHX_ENABLE_CUDA_GRAPH_NODES=$<BOOL:${GRAPHX_CUDA_GRAPH_NODES_ENABLED}>
    GRAPHX_ENABLE_METAL_GRAPH_NODES=$<BOOL:${GRAPHX_METAL_GRAPH_NODES_ENABLED}>
    GRAPHX_ENABLE_METAL_NATIVE_RUNTIME=$<BOOL:${GRAPHX_METAL_NATIVE_RUNTIME_ENABLED}>
    GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME=$<BOOL:${GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME}>
    GRAPHX_ENABLE_GPU_TEST_HOOKS=$<BOOL:${GRAPHX_GPU_TEST_HOOKS_ENABLED}>
    GRAPHX_MULTI_GPU_TESTS=$<BOOL:${MULTI_GPU_TESTS}>
)

if(MSVC)
    target_compile_options(graphx_project_options INTERFACE /W4)
else()
    target_compile_options(graphx_project_options INTERFACE -Wall -Wextra -Wpedantic)
endif()

if(ENABLE_TSAN)
    target_compile_options(graphx_project_options INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(graphx_project_options INTERFACE -fsanitize=thread)
endif()
if(ENABLE_ASAN_UBSAN)
    target_compile_options(graphx_project_options INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(graphx_project_options INTERFACE
        -fsanitize=address,undefined)
endif()

message(STATUS "GraphX CUDA graph nodes: ${GRAPHX_CUDA_GRAPH_NODES_ENABLED}")
message(STATUS "GraphX Metal graph nodes: ${GRAPHX_METAL_GRAPH_NODES_ENABLED}")
message(STATUS "GraphX Metal native runtime: ${GRAPHX_METAL_NATIVE_RUNTIME_ENABLED}")
message(STATUS "GraphX strict native Metal requirement: ${GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME}")
message(STATUS "GraphX target system: ${CMAKE_SYSTEM_NAME}")
message(STATUS "GraphX C++ standard flag: ${GRAPHX_CXX_STANDARD_FLAG}")
message(STATUS "GraphX build profile: ${GRAPHX_BUILD_PROFILE}")
message(STATUS "GraphX GPU test hooks: ${GRAPHX_GPU_TEST_HOOKS_ENABLED}")
if(GRAPHX_METAL_CPP_INCLUDE_DIR_RESOLVED)
    message(STATUS "GraphX metal-cpp include dir: ${GRAPHX_METAL_CPP_INCLUDE_DIR_RESOLVED}")
endif()
message(STATUS "GraphX multi-GPU tests: ${MULTI_GPU_TESTS}")

# Add projects
add_subdirectory(libgraph)
add_subdirectory(libaccelgraph)
add_subdirectory(libsensor)
add_subdirectory(libgpu)
add_subdirectory(libdsp)

add_executable(graphx_graph_cli tools/graph-cli.cpp)
set_target_properties(graphx_graph_cli PROPERTIES OUTPUT_NAME graph-cli)
target_link_libraries(graphx_graph_cli PRIVATE graph gpu graphx_project_options)

add_executable(graphx_graph_dashboard tools/graph-dashboard.cpp)
set_target_properties(graphx_graph_dashboard PROPERTIES OUTPUT_NAME graphx-dashboard)
target_link_libraries(graphx_graph_dashboard PRIVATE graph gpu graphx_project_options)
if(NOT MSVC)
    target_compile_options(graphx_graph_cli PRIVATE -Werror)
    target_compile_options(graphx_graph_dashboard PRIVATE -Werror)
endif()
install(TARGETS graphx_graph_cli graphx_graph_dashboard
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(GRAPHX_BUILD_EXAMPLES_SAR)
    add_subdirectory(examples/SAR)
endif()
if(GRAPHX_BUILD_EXAMPLES_DSP)
    add_subdirectory(examples/DSP)
endif()

# Testing
if(BUILD_TESTS)
    # Ensure CTest dashboard metadata files are generated for clean ctest runs.
    include(CTest)
    enable_testing()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    
    # Add per-project tests
    add_subdirectory(libgraph/test)
    add_subdirectory(libaccelgraph/test)
    add_subdirectory(libgpu/test)
    add_subdirectory(libdsp/test)
    add_subdirectory(libsensor/test)
    add_test(NAME graph_cli_help COMMAND graphx_graph_cli --help)
    add_test(NAME graph_dashboard_help COMMAND graphx_graph_dashboard --help)
    add_test(
        NAME graph_cli_node_count
        COMMAND graphx_graph_cli --graph
            ${CMAKE_SOURCE_DIR}/libgraph/test/config/topologies/minimal_graph.json
            node-count)
    set_tests_properties(
        graph_cli_help graph_cli_node_count graph_dashboard_help PROPERTIES
        LABELS "core;cli;phase2b;ci-safe;host-any")
    add_test(
        NAME graph_tools_executable_contract
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/tests/test_graph_tools.py
            --dashboard $<TARGET_FILE:graphx_graph_dashboard>
            --cli $<TARGET_FILE:graphx_graph_cli>
            --graph
                ${CMAKE_SOURCE_DIR}/libgraph/test/config/topologies/minimal_graph.json
            --work-dir ${CMAKE_BINARY_DIR}/test-output/graph-tools)
    set_tests_properties(graph_tools_executable_contract PROPERTIES
        LABELS "core;cli;dashboard;integration;ci-safe;host-any"
        TIMEOUT 30
        RUN_SERIAL TRUE)
    if(GRAPHX_BUILD_EXAMPLES_SAR)
        add_subdirectory(examples/SAR/plugins)
        add_subdirectory(examples/SAR/test)
    endif()
    if(GRAPHX_BUILD_EXAMPLES_DSP)
        add_subdirectory(examples/DSP/test)
    endif()

    # Summary target
    add_custom_target(test-summary
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMENT "Running all tests with summary"
    )
endif()

# Installation
# Keep a convenient default install path without overriding explicit user/CI choices.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install path prefix" FORCE)
endif()

# Add cmake directory to search path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Top-level package config for downstream consumers.
set(GRAPHX_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/GraphX")

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GraphXConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/GraphXConfig.cmake
    INSTALL_DESTINATION ${GRAPHX_CMAKE_INSTALL_DIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/GraphXConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/GraphXConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/GraphXConfigVersion.cmake
    DESTINATION ${GRAPHX_CMAKE_INSTALL_DIR}
)
