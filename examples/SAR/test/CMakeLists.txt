find_package(GTest REQUIRED)
find_package(Threads REQUIRED)
find_package(log4cxx REQUIRED)

set(SAR_LOG4CXX_LIB log4cxx)
if(TARGET log4cxx::log4cxx)
    set(SAR_LOG4CXX_LIB log4cxx::log4cxx)
endif()

add_executable(test_sar_example_unit
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/AzimuthTileSplitNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_azimuth_tile_split_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/D2HAsyncAccelNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/H2DAsyncAccelNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ImageTileMergeNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_image_tile_merge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_image_tile_merge_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/RangeCompressionNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_range_compression_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/RangeWindowNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_range_window_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SarDiagnosticsSinkNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_diagnostics_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SarVisualizationSinkNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_visualization_sink_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SarBackprojectionTransformAccelNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_accel_nodes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_json_pipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_pr3_metal_json.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_pr2_fanout_json.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_projectile_scenario.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_trace_schema.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_json_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SyntheticApertureIqSourceNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_synthetic_aperture_iq_source_node.cpp
)

target_compile_features(test_sar_example_unit PRIVATE cxx_std_26)
target_include_directories(test_sar_example_unit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_definitions(test_sar_example_unit PRIVATE
    PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/examples/SAR/plugins"
    SAR_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_pr1.json"
    SAR_PR2_FANOUT_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_pr2_fanout.json"
    SAR_PR3_METAL_WINDOW_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_pr3_metal_window.json"
    SAR_PR3_METAL_COMPRESSION_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_pr3_metal_compression.json"
    SAR_PROJECTILE_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_projectile_approach_pr1.json"
    SAR_BENCHMARK_EXECUTABLE_PATH="$<TARGET_FILE:sar_benchmark>"
)
target_link_libraries(test_sar_example_unit PRIVATE
    graph
    gpu
    dsp
    ${SAR_LOG4CXX_LIB}
    GTest::gtest_main
    Threads::Threads
    graphx_project_options
)
add_dependencies(test_sar_example_unit azimuth_tile_split_node)
add_dependencies(test_sar_example_unit d2h_async_node)
add_dependencies(test_sar_example_unit h2d_async_node)
add_dependencies(test_sar_example_unit image_tile_merge_node)
add_dependencies(test_sar_example_unit range_window_node)
add_dependencies(test_sar_example_unit range_compression_node)
add_dependencies(test_sar_example_unit sar_pulse_fanout_node)
add_dependencies(test_sar_example_unit sar_diagnostics_sink_node)
add_dependencies(test_sar_example_unit sar_visualization_sink_node)
add_dependencies(test_sar_example_unit sar_backprojection_transform_node)
add_dependencies(test_sar_example_unit synthetic_aperture_iq_source_node)

add_test(NAME sar_example_unit COMMAND test_sar_example_unit)
