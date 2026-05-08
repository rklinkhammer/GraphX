cmake_minimum_required(VERSION 3.23)
project(gdashboard-workspace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler flags
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Build options
option(BUILD_TESTS "Build test targets" ON)
option(BUILD_DOCS "Build Doxygen documentation" OFF)

# Add projects
add_subdirectory(libgraph)
add_subdirectory(libnodes)
add_subdirectory(libdashboard)
add_subdirectory(gdashboard-app)

# Testing
if(BUILD_TESTS)
    enable_testing()
    
    # Add per-project tests
    add_subdirectory(libgraph/test)
    add_subdirectory(libnodes/test)
    add_subdirectory(libdashboard/test)
    
    # Summary target
    add_custom_target(test-summary
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMENT "Running all tests with summary"
    )
endif()

# Installation
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install")

# Add cmake directory to search path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
