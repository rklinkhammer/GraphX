cmake_minimum_required(VERSION 3.23)
project(gdashboard-workspace VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GRAPHX_ENABLE_MODULE_PILOT "Enable the experimental C++ module pilot" ON)
option(GRAPHX_REQUIRE_NINJA "Require Ninja generator for all GraphX configurations" ON)

if(GRAPHX_REQUIRE_NINJA)
    if(NOT CMAKE_GENERATOR MATCHES "^Ninja")
        message(FATAL_ERROR
            "GraphX requires the Ninja generator. "
            "Current generator: ${CMAKE_GENERATOR}. "
            "Reconfigure with: cmake -S . -B <build-dir> -G Ninja")
    endif()
endif()

if(NOT CMAKE_CXX_STANDARD EQUAL 26)
    message(FATAL_ERROR "GraphX requires C++26 as the only supported standard")
endif()

# Build options
option(BUILD_TESTS "Build test targets" ON)
option(BUILD_DOCS "Build Doxygen documentation" OFF)
option(ENABLE_TSAN "Build with ThreadSanitizer instrumentation" OFF)

if(ENABLE_TSAN)
    if(MSVC)
        message(FATAL_ERROR "ENABLE_TSAN is not supported with MSVC")
    endif()
endif()

# Shared project options target to avoid leaking global flags into all targets.
add_library(graphx_project_options INTERFACE)

if(MSVC)
    target_compile_options(graphx_project_options INTERFACE /W4)
else()
    target_compile_options(graphx_project_options INTERFACE -Wall -Wextra -Wpedantic)
endif()

if(ENABLE_TSAN)
    target_compile_options(graphx_project_options INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(graphx_project_options INTERFACE -fsanitize=thread)
endif()

# Add projects
add_subdirectory(libgraph)
add_subdirectory(libsensor)
add_subdirectory(libdsp)
# add_subdirectory(libnodes)
# add_subdirectory(libdashboard)
# add_subdirectory(gdashboard-app)

# Testing
if(BUILD_TESTS)
    enable_testing()
    
    # Add per-project tests
    add_subdirectory(libgraph/test)
    # add_subdirectory(libnodes/test)
    # add_subdirectory(libdashboard/test)
    
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
