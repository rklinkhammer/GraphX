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
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SarMaterializedImageSinkNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_materialized_image_sink_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SarVisualizationSinkNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_visualization_sink_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SarBackprojectionTransformAccelNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_accel_nodes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_cpu_reference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/GotchaReplaySourceNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gotcha_dataset_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_json_pipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_pr3_metal_json.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_accel_token_guardrails.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_pr2_fanout_json.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_projectile_scenario.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_token_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_trace_schema.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_json_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_pr2_token_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_external_baseline_policy_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_scenario_manifest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_rrp1_local_runner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_rrp2_scenario_image_path.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_rrp3_gotcha_back_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_rrp4_image_comparator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SyntheticApertureIqSourceNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_synthetic_aperture_iq_source_node.cpp
)

target_compile_features(test_sar_example_unit PRIVATE cxx_std_26)
target_include_directories(test_sar_example_unit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_definitions(test_sar_example_unit PRIVATE
    PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/examples/SAR/plugins"
    GPU_PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    SAR_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_pr1.json"
    SAR_PR2_FANOUT_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_pr2_fanout.json"
    SAR_PR3_METAL_WINDOW_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_pr3_metal_window.json"
    SAR_PR3_METAL_COMPRESSION_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_pr3_metal_compression.json"
    SAR_PR3_METAL_FANOUT_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_pr3_metal_fanout.json"
    SAR_PR6_MATCHED_FILTER_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_pr6_matched_filter.json"
    SAR_PR7_MATERIALIZED_IMAGE_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_pr7_materialized_image.json"
    SAR_DEFINITIVE_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_definitive.json"
    SAR_GOTCHA_REPLAY_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gotcha_replay_fixture.json"
    SAR_PROJECTILE_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_projectile_approach_pr1.json"
    SAR_BENCHMARK_EXECUTABLE_PATH="$<TARGET_FILE:sar_benchmark>"
    SAR_EXTERNAL_BASELINE_POLICY_PATH="${CMAKE_SOURCE_DIR}/plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md"
    SAR_BASELINE_PACKAGE_REGISTRY_PATH="${CMAKE_SOURCE_DIR}/plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json"
    SAR_SCENARIO_001_JSON_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/scenarios/scenario_001.json"
    SAR_SCENARIO_001_MD_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/scenarios/scenario_001.md"
    SAR_RRP1_LOCAL_RUNNER_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/rrp1_local_runner.py"
    SAR_RRP3_GOTCHA_BACK_ADAPTER_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/rrp3_gotcha_back_adapter.py"
    SAR_RRP4_IMAGE_COMPARATOR_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/rrp4_image_comparator.py"
    SAR_RRP4_IMAGE_COMPARISON_SCHEMA_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/rrp4_image_comparison_report.schema.json"
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
add_dependencies(test_sar_example_unit sar_materialized_image_sink_node)
add_dependencies(test_sar_example_unit sar_visualization_sink_node)
add_dependencies(test_sar_example_unit sar_backprojection_transform_node)
add_dependencies(test_sar_example_unit synthetic_aperture_iq_source_node)
add_dependencies(test_sar_example_unit gotcha_replay_source_node)
add_dependencies(test_sar_example_unit sar_benchmark)
if(TARGET metal_h2d_async_node)
    add_dependencies(test_sar_example_unit metal_h2d_async_node)
endif()
if(TARGET metal_d2h_async_node)
    add_dependencies(test_sar_example_unit metal_d2h_async_node)
endif()
if(TARGET metal_device_kernel_node)
    add_dependencies(test_sar_example_unit metal_device_kernel_node)
endif()

add_test(NAME sar_example_unit COMMAND test_sar_example_unit)
