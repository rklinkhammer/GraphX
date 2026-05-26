cmake_minimum_required(VERSION 3.23)

# Include the plugin template
include(${CMAKE_SOURCE_DIR}/cmake/PluginTemplate.cmake)

# ============================================================================
# Plugin Directory Setup
# ============================================================================

add_graphx_plugins_directory()

# ============================================================================
# Define Plugins
# ============================================================================

# TestNode Plugin - Simple test node
add_graphx_plugin(
    TARGET_NAME test_node
    SOURCE_FILE test_node_plugin.cpp 
    DESCRIPTION "dynamic loading test node "
)

# Advanced Test Nodes - Stage 5.5a plugins (each in its own .so)
add_graphx_plugin(
    TARGET_NAME source_test_node
    SOURCE_FILE source_test_node_plugin.cpp 
    DESCRIPTION "source test node with ICompletionCallback support"
)

add_graphx_plugin(
    TARGET_NAME sink_test_node
    SOURCE_FILE sink_test_node_plugin.cpp 
    DESCRIPTION "sink test node with ICompletionCallback support"
)

add_graphx_plugin(
    TARGET_NAME interior_test_node
    SOURCE_FILE interior_test_node_plugin.cpp 
    DESCRIPTION "interior test node for transformation testing"
)

add_graphx_plugin(
    TARGET_NAME merge_test_node
    SOURCE_FILE merge_test_node_plugin.cpp 
    DESCRIPTION "merge test node for multi-input testing"
)

add_graphx_plugin(
    TARGET_NAME split_test_node
    SOURCE_FILE split_test_node_plugin.cpp 
    DESCRIPTION "split test node for multi-output testing"
)
