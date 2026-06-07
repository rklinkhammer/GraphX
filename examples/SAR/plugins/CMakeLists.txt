cmake_minimum_required(VERSION 3.23)

include(${CMAKE_SOURCE_DIR}/cmake/PluginTemplate.cmake)

add_graphx_plugins_directory()

add_graphx_plugin(
    TARGET_NAME azimuth_tile_split_node
    SOURCE_FILES
        azimuth_tile_split_node_plugin.cpp
        ../src/AzimuthTileSplitNode.cpp
    DESCRIPTION "SAR azimuth tile split node"
    DEPENDENCIES graph
)

target_include_directories(azimuth_tile_split_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_graphx_plugin(
    TARGET_NAME synthetic_aperture_iq_source_node
    SOURCE_FILES
        synthetic_aperture_iq_source_node_plugin.cpp
        ../src/SyntheticApertureIqSourceNode.cpp
    DESCRIPTION "SAR synthetic aperture IQ source node"
    DEPENDENCIES graph
)

target_include_directories(synthetic_aperture_iq_source_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
