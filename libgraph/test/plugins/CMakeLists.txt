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

# Producer Test Nodes - Producer-based topology plugins
add_graphx_plugin(
    TARGET_NAME int_producer
    SOURCE_FILE int_producer_plugin.cpp 
    DESCRIPTION "integer producer test node"
)

add_graphx_plugin(
    TARGET_NAME double_producer
    SOURCE_FILE double_producer_plugin.cpp 
    DESCRIPTION "double producer test node"
)

add_graphx_plugin(
    TARGET_NAME int_sink
    SOURCE_FILE int_sink_plugin.cpp 
    DESCRIPTION "integer sink test node"
)

add_graphx_plugin(
    TARGET_NAME double_sink
    SOURCE_FILE double_sink_plugin.cpp 
    DESCRIPTION "double sink test node"
)

add_graphx_plugin(
    TARGET_NAME completion_node
    SOURCE_FILE completion_node_plugin.cpp 
    DESCRIPTION "completion signal sink node"
)

# SDR Test Nodes - dynamically loadable signal-processing pipeline
add_graphx_plugin(
    TARGET_NAME sine_iq_source
    SOURCE_FILE sine_iq_source_plugin.cpp
    DESCRIPTION "SDR sine wave IQ packet generator"
)

add_graphx_plugin(
    TARGET_NAME fft_power_spectrum
    SOURCE_FILE fft_power_spectrum_plugin.cpp
    DESCRIPTION "SDR FFT processor producing power spectra"
)

add_graphx_plugin(
    TARGET_NAME analyzer_sink
    SOURCE_FILE analyzer_sink_plugin.cpp
    DESCRIPTION "SDR analyzer sink node"
)
