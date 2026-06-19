# MIT License
#
# Copyright (c) 2025 graphlib contributors
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# ============================================================================
# PluginTemplate.cmake
# ============================================================================
#
# Provides helper functions for creating GraphX plugin shared libraries.
# Simplifies the boilerplate required to add new plugins.
#
# Usage:
#   include(${CMAKE_SOURCE_DIR}/cmake/PluginTemplate.cmake)
#
#   # Define a plugin with default settings
#   add_graphx_plugin(
#       TARGET_NAME my_node_plugin
#       SOURCE_FILE my_node_plugin.cpp
#       DESCRIPTION "My node implementation"
#   )
#
#   # Define a plugin with custom dependencies
#   add_graphx_plugin(
#       TARGET_NAME custom_plugin
#       SOURCE_FILES custom_plugin.cpp helper.cpp
#       DESCRIPTION "Custom plugin with helpers"
#       DEPENDENCIES custom_lib another_lib
#   )

# ============================================================================
# Ensure required packages are found
# ============================================================================

# Find log4cxx if not already found
if(NOT TARGET log4cxx)
    find_package(log4cxx REQUIRED)
endif()

find_package(Threads REQUIRED)

# ============================================================================
# Function: add_graphx_plugin
# ============================================================================
#
# Creates a GraphX plugin shared library with standard configuration.
#
# Arguments:
#   TARGET_NAME (required)
#       - Name of the CMake target to create
#       - Will be used as the library name
#       - Example: flight_fsm_node
#
#   SOURCE_FILE / SOURCE_FILES (required)
#       - Single source file or list of source files
#       - Can use either SOURCE_FILE (singular) or SOURCE_FILES (plural)
#       - Example: flight_fsm_node_plugin.cpp
#
#   DESCRIPTION (optional)
#       - Human-readable description of the plugin
#       - Used for CMake status messages
#       - Default: "GraphX plugin"
#
#   DEPENDENCIES (optional)
#       - Additional libraries to link against (beyond defaults)
#       - Default dependencies: gdashboard_lib, log4cxx, pthread, sensor
#       - Example: DEPENDENCIES custom_lib another_lib
#
#   OUTPUT_DIRECTORY (optional)
#       - Override the default plugin output directory
#       - Default: GRAPHX_PLUGIN_OUTPUT_DIRECTORY when set, otherwise ${CMAKE_BINARY_DIR}/plugins
#
#   CXX_STANDARD (optional)
#       - C++ standard to use (cxx_std_XX)
#       - Default: cxx_std_26
#
# Example 1 - Simple plugin:
#   add_graphx_plugin(
#       TARGET_NAME flight_fsm_node
#       SOURCE_FILE flight_fsm_node_plugin.cpp
#       DESCRIPTION "Flight state machine node plugin"
#   )
#
# Example 2 - Plugin with multiple sources:
#   add_graphx_plugin(
#       TARGET_NAME data_processor
#       SOURCE_FILES
#           processor_plugin.cpp
#           processor_impl.cpp
#           helpers.cpp
#       DESCRIPTION "Data processing node plugin"
#       DEPENDENCIES data_lib
#   )
#
function(add_graphx_plugin)
    # Parse arguments
    set(one_value_args TARGET_NAME DESCRIPTION OUTPUT_DIRECTORY CXX_STANDARD)
    set(multi_value_args SOURCE_FILE SOURCE_FILES DEPENDENCIES)
    cmake_parse_arguments(PLUGIN "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    # Validate required arguments
    if(NOT PLUGIN_TARGET_NAME)
        message(FATAL_ERROR "add_graphx_plugin: TARGET_NAME is required")
    endif()

    # Handle SOURCE_FILE or SOURCE_FILES
    if(PLUGIN_SOURCE_FILE AND PLUGIN_SOURCE_FILES)
        message(FATAL_ERROR "add_graphx_plugin: Cannot specify both SOURCE_FILE and SOURCE_FILES")
    endif()

    if(PLUGIN_SOURCE_FILE)
        set(sources ${PLUGIN_SOURCE_FILE})
    elseif(PLUGIN_SOURCE_FILES)
        set(sources ${PLUGIN_SOURCE_FILES})
    else()
        message(FATAL_ERROR "add_graphx_plugin: Either SOURCE_FILE or SOURCE_FILES is required")
    endif()

    # Set defaults
    if(NOT PLUGIN_DESCRIPTION)
        set(PLUGIN_DESCRIPTION "GraphX plugin")
    endif()

    if(NOT PLUGIN_OUTPUT_DIRECTORY AND GRAPHX_PLUGIN_OUTPUT_DIRECTORY)
        set(PLUGIN_OUTPUT_DIRECTORY ${GRAPHX_PLUGIN_OUTPUT_DIRECTORY})
    endif()

    if(NOT PLUGIN_OUTPUT_DIRECTORY)
        set(PLUGIN_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
    endif()

    if(NOT PLUGIN_CXX_STANDARD)
        set(PLUGIN_CXX_STANDARD cxx_std_26)
    endif()

    # Ensure plugin output directory exists
    file(MAKE_DIRECTORY ${PLUGIN_OUTPUT_DIRECTORY})

    # ========================================================================
    # Create the shared library
    # ========================================================================
    add_library(${PLUGIN_TARGET_NAME} SHARED)

    target_sources(${PLUGIN_TARGET_NAME} PRIVATE ${sources})

    # ========================================================================
    # Include directories
    # ========================================================================
    target_include_directories(${PLUGIN_TARGET_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/core
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/libsensor/include
        ${CMAKE_SOURCE_DIR}/libdsp/include
        ${CMAKE_SOURCE_DIR}/libgraph/test/include
    )

    # ========================================================================
    # Compilation flags
    # ========================================================================
    target_compile_features(${PLUGIN_TARGET_NAME} PRIVATE ${PLUGIN_CXX_STANDARD})

    # ========================================================================
    # Shared library properties
    # ========================================================================
    set_target_properties(${PLUGIN_TARGET_NAME} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        PREFIX "lib"
        LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_OUTPUT_DIRECTORY}
    )

    # ========================================================================
    # Link libraries
    # ========================================================================
    set(default_plugin_dependencies log4cxx Threads::Threads)
    if(NOT "dsp" IN_LIST PLUGIN_DEPENDENCIES AND NOT "gpu" IN_LIST PLUGIN_DEPENDENCIES)
        list(APPEND default_plugin_dependencies sensor)
    endif()

    # Always link the default dependencies. DSP plugins receive sensor/graph
    # transitively through dsp, so avoid adding duplicate static libraries.
    target_link_libraries(${PLUGIN_TARGET_NAME} PRIVATE
        ${default_plugin_dependencies}
    )

    # Link any additional user-specified dependencies
    if(PLUGIN_DEPENDENCIES)
        target_link_libraries(${PLUGIN_TARGET_NAME} PRIVATE
            ${PLUGIN_DEPENDENCIES}
        )
    endif()

    if(TARGET graphx_project_options)
        target_link_libraries(${PLUGIN_TARGET_NAME} PRIVATE graphx_project_options)
    endif()

    # ========================================================================
    # Status message
    # ========================================================================
    message(STATUS "Plugin: ${PLUGIN_TARGET_NAME}")
    message(STATUS "  Description: ${PLUGIN_DESCRIPTION}")
    message(STATUS "  Sources: ${sources}")
    if(PLUGIN_DEPENDENCIES)
        message(STATUS "  Additional Dependencies: ${PLUGIN_DEPENDENCIES}")
    endif()
    message(STATUS "  Output directory: ${PLUGIN_OUTPUT_DIRECTORY}")
    message(STATUS "")

    if(NOT TARGET graphx_dynamic_nodes)
        add_custom_target(graphx_dynamic_nodes ALL
            COMMENT "Building all GraphX dynamic node plugins")
    endif()
    add_dependencies(graphx_dynamic_nodes ${PLUGIN_TARGET_NAME})

endfunction()

# ============================================================================
# Function: add_graphx_plugins_directory
# ============================================================================
#
# Sets up the plugin output directory and configuration.
# Should be called once at the beginning of the plugins CMakeLists.txt.
#
# Arguments:
#   OUTPUT_DIR (optional)
#       - Directory where plugins will be built
#       - Default: ${CMAKE_BINARY_DIR}/plugins
#
# Example:
#   add_graphx_plugins_directory()
#   # Now add your plugins with add_graphx_plugin()
#
function(add_graphx_plugins_directory)
    set(one_value_args OUTPUT_DIR)
    cmake_parse_arguments(PLUGINDIR "" "${one_value_args}" "" ${ARGN})

    if(NOT PLUGINDIR_OUTPUT_DIR)
        set(PLUGINDIR_OUTPUT_DIR ${CMAKE_BINARY_DIR}/plugins)
    endif()

    # Create directory
    file(MAKE_DIRECTORY ${PLUGINDIR_OUTPUT_DIR})

    if(NOT TARGET graphx_dynamic_nodes)
        add_custom_target(graphx_dynamic_nodes ALL
            COMMENT "Building all GraphX dynamic node plugins")
    endif()

    # Set in parent scope for use in add_graphx_plugin
    set(GRAPHX_PLUGIN_OUTPUT_DIRECTORY ${PLUGINDIR_OUTPUT_DIR} PARENT_SCOPE)

    message(STATUS "")
    message(STATUS "GraphX Plugin Configuration")
    message(STATUS "===========================")
    message(STATUS "Plugin Output Directory: ${PLUGINDIR_OUTPUT_DIR}")
    message(STATUS "")

endfunction()
