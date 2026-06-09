cmake_minimum_required(VERSION 3.23)

include(${CMAKE_SOURCE_DIR}/cmake/PluginTemplate.cmake)

set(SAR_PLUGIN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/SAR/plugins")

add_graphx_plugins_directory(OUTPUT_DIR ${SAR_PLUGIN_OUTPUT_DIRECTORY})

add_graphx_plugin(
    TARGET_NAME h2d_async_node
    SOURCE_FILES
        h2d_async_node_plugin.cpp
        ../src/H2DAsyncAccelNode.cpp
    DESCRIPTION "SAR host-to-device async transfer stage"
    DEPENDENCIES graph gpu
)

target_include_directories(h2d_async_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_graphx_plugin(
    TARGET_NAME d2h_async_node
    SOURCE_FILES
        d2h_async_node_plugin.cpp
        ../src/D2HAsyncAccelNode.cpp
    DESCRIPTION "SAR device-to-host async transfer stage"
    DEPENDENCIES graph gpu
)

target_include_directories(d2h_async_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_graphx_plugin(
    TARGET_NAME azimuth_tile_split_node
    SOURCE_FILES
        azimuth_tile_split_node_plugin.cpp
        ../src/AzimuthTileSplitNode.cpp
    DESCRIPTION "SAR azimuth tile split node"
    DEPENDENCIES graph gpu
)

target_include_directories(azimuth_tile_split_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_graphx_plugin(
    TARGET_NAME range_window_node
    SOURCE_FILES
        range_window_node_plugin.cpp
        ../src/RangeWindowNode.cpp
    DESCRIPTION "SAR deterministic range window node"
    DEPENDENCIES graph gpu
)

target_include_directories(range_window_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_graphx_plugin(
    TARGET_NAME range_compression_node
    SOURCE_FILES
        range_compression_node_plugin.cpp
        ../src/RangeCompressionNode.cpp
    DESCRIPTION "SAR FFT-backed range compression node"
    DEPENDENCIES graph gpu dsp
)

target_include_directories(range_compression_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_graphx_plugin(
    TARGET_NAME sar_pulse_fanout_node
    SOURCE_FILES
        sar_pulse_fanout_node_plugin.cpp
    DESCRIPTION "SAR pulse fan-out node"
    DEPENDENCIES graph gpu
)

target_include_directories(sar_pulse_fanout_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_graphx_plugin(
    TARGET_NAME sar_backprojection_transform_node
    SOURCE_FILES
        sar_backprojection_transform_node_plugin.cpp
        ../src/SarBackprojectionTransformAccelNode.cpp
    DESCRIPTION "SAR backprojection transform node"
    DEPENDENCIES graph gpu sar_materialized_payload_runtime
)

target_include_directories(sar_backprojection_transform_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_graphx_plugin(
    TARGET_NAME sar_diagnostics_sink_node
    SOURCE_FILES
        sar_diagnostics_sink_node_plugin.cpp
        ../src/SarDiagnosticsSinkNode.cpp
    DESCRIPTION "SAR diagnostics sink node"
    DEPENDENCIES graph gpu
)

target_include_directories(sar_diagnostics_sink_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_graphx_plugin(
    TARGET_NAME sar_visualization_sink_node
    SOURCE_FILES
        sar_visualization_sink_node_plugin.cpp
        ../src/SarVisualizationSinkNode.cpp
    DESCRIPTION "SAR visualization sink node"
    DEPENDENCIES graph gpu
)

target_include_directories(sar_visualization_sink_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_graphx_plugin(
    TARGET_NAME sar_materialized_image_sink_node
    SOURCE_FILES
        sar_materialized_image_sink_node_plugin.cpp
        ../src/SarMaterializedImageSinkNode.cpp
    DESCRIPTION "SAR deterministic materialized image sink node"
    DEPENDENCIES graph gpu sar_materialized_payload_runtime
)

target_include_directories(sar_materialized_image_sink_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_graphx_plugin(
    TARGET_NAME image_tile_merge_node
    SOURCE_FILES
        image_tile_merge_node_plugin.cpp
        ../src/ImageTileMergeNode.cpp
    DESCRIPTION "SAR image tile merge node"
    DEPENDENCIES graph gpu
)

target_include_directories(image_tile_merge_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_graphx_plugin(
    TARGET_NAME synthetic_aperture_iq_source_node
    SOURCE_FILES
        synthetic_aperture_iq_source_node_plugin.cpp
        ../src/SyntheticApertureIqSourceNode.cpp
    DESCRIPTION "SAR synthetic aperture IQ source node"
    DEPENDENCIES graph gpu
)

target_include_directories(synthetic_aperture_iq_source_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_graphx_plugin(
    TARGET_NAME gotcha_replay_source_node
    SOURCE_FILES
        gotcha_replay_source_node_plugin.cpp
        ../src/GotchaReplaySourceNode.cpp
    DESCRIPTION "SAR Gotcha replay source node"
    DEPENDENCIES graph gpu
)

target_include_directories(gotcha_replay_source_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
