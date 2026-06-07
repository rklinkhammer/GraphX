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
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/D2HAsyncNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/H2DAsyncNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ImageTileMergeNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_image_tile_merge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_image_tile_merge_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SarDiagnosticsSinkNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_diagnostics_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SarBackprojectionTransformNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_baseline_compare.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_json_pipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_backprojection_transform_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_json_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SyntheticApertureIqSourceNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_synthetic_aperture_iq_source_node.cpp
)

target_compile_features(test_sar_example_unit PRIVATE cxx_std_26)
target_include_directories(test_sar_example_unit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_definitions(test_sar_example_unit PRIVATE
    PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    SAR_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_pr1.json"
)
target_link_libraries(test_sar_example_unit PRIVATE
    graph
    gpu
    ${SAR_LOG4CXX_LIB}
    GTest::gtest_main
    Threads::Threads
    graphx_project_options
)
add_dependencies(test_sar_example_unit azimuth_tile_split_node)
add_dependencies(test_sar_example_unit d2h_async_node)
add_dependencies(test_sar_example_unit h2d_async_node)
add_dependencies(test_sar_example_unit image_tile_merge_node)
add_dependencies(test_sar_example_unit sar_diagnostics_sink_node)
add_dependencies(test_sar_example_unit sar_backprojection_transform_node)
add_dependencies(test_sar_example_unit synthetic_aperture_iq_source_node)

add_test(NAME sar_example_unit COMMAND test_sar_example_unit)
