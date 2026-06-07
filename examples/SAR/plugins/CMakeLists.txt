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
