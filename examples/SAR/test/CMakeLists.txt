find_package(GTest REQUIRED)
find_package(Threads REQUIRED)
find_package(log4cxx REQUIRED)
find_package(HDF5 COMPONENTS C QUIET)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/test_cpu_reference_backprojection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/GotchaReplaySourceNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gotcha_dataset_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_json_pipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_accel_token_guardrails.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_projectile_scenario.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_token_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_transport_opaque_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_runtime_helpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_trace_schema.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_json_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_main_executable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_graphx_gotcha_to_crsd_cli.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_normalized_sar_product.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_product_validator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gotcha_input_ordering.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gotcha_mat_inspector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gotcha_field_inventory_validation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gotcha_mat_reader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gotcha_to_crsd_metadata_mapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gotcha_full_pulse_ingestion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_crsd_io.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_product_chunker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_io_utilities.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_external_baseline_policy_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_scenario_manifest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_scenario_fixture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_local_runner_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_scenario_image_path_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_graphx_scenario_runner_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gotcha_back_adapter_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_image_comparator_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_image_comparator_metrics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_frozen_scenario_replay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ci_correctness_lane.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ci_tiny_fixture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ci_validation_lane.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sarpy_metadata_harness.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sarpy_reference_compare_tools.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sarpy_crsd_validation_harness.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_graphx_image_comparison_lane.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gotcha_real_full_aperture_validation.cpp
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
    SAR_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_simulated.json"
    SAR_FANOUT_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_fanout.json"
    SAR_METAL_WINDOW_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_metal_window.json"
    SAR_METAL_COMPRESSION_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_metal_compression.json"
    SAR_METAL_FANOUT_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_metal_fanout.json"
    SAR_MATCHED_FILTER_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_matched_filter.json"
    SAR_MATERIALIZED_IMAGE_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_materialized_image.json"
    SAR_DEFINITIVE_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_definitive.json"
    SAR_GOTCHA_REPLAY_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gotcha_replay_fixture.json"
    SAR_PROJECTILE_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_projectile_approach.json"
    SAR_BENCHMARK_EXECUTABLE_PATH="$<TARGET_FILE:sar_benchmark>"
    SAR_EXAMPLE_EXECUTABLE_PATH="$<TARGET_FILE:sar_example>"
    SAR_GOTCHA_TO_CRSD_EXECUTABLE_PATH="$<TARGET_FILE:graphx_gotcha_to_crsd>"
    SAR_EXTERNAL_BASELINE_POLICY_PATH="${CMAKE_SOURCE_DIR}/plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md"
    SAR_BASELINE_PACKAGE_REGISTRY_PATH="${CMAKE_SOURCE_DIR}/plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json"
    SAR_GOTCHA_INPUT_MANIFEST_SCHEMA_PATH="${CMAKE_SOURCE_DIR}/docs/sar/gotcha_input_manifest_schema.md"
    SAR_SCENARIO_001_JSON_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/scenarios/scenario_001.json"
    SAR_SCENARIO_001_MD_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/scenarios/scenario_001.md"
    SAR_SCENARIO_001_FIXTURE_MANIFEST_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/fixtures/scenario_001/fixture_manifest.json"
    SAR_SCENARIO_001_FIXTURE_PROVENANCE_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/fixtures/scenario_001/provenance.md"
    SAR_SCENARIO_001_FIXTURE_CHECKSUMS_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/fixtures/scenario_001/checksums.txt"
    SAR_SCENARIO_001_FIXTURE_DATA_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/fixtures/scenario_001/deterministic_iq_phase_history_fixture_v1.json"
    SAR_LOCAL_RUNNER_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/sar_local_runner.py"
    SAR_SCENARIO_TO_RUN_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/sar_scenario_to_run.py"
    SAR_GOTCHA_BACK_ADAPTER_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/gotcha_back_adapter.py"
    SAR_IMAGE_COMPARATOR_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/sar_image_comparator.py"
    SAR_IMAGE_COMPARISON_SCHEMA_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/sar_image_comparison_report.schema.json"
    SAR_FROZEN_SCENARIO_REPLAY_GUIDE_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/frozen_scenario_replay.md"
    SAR_CI_TINY_GOTCHA_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/scenario_001_ci_tiny_gotcha_fixture.json"
    SARPY_METADATA_HARNESS_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/sarpy_metadata_harness.py"
    SARPY_METADATA_HARNESS_GUIDE_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/sarpy_metadata_harness.md"
    SARPY_REFERENCE_IMAGE_FROM_GOTCHA_TOOL_PATH="${CMAKE_SOURCE_DIR}/tools/sarpy/reference_image_from_gotcha.py"
    SARPY_COMPARE_IMAGES_TOOL_PATH="${CMAKE_SOURCE_DIR}/tools/sarpy/compare_images.py"
    SARPY_REQUIREMENTS_PATH="${CMAKE_SOURCE_DIR}/tools/sarpy/requirements.txt"
    SARPY_VALIDATE_CRSD_TOOL_PATH="${CMAKE_SOURCE_DIR}/tools/sarpy/validate_crsd.py"
    SARPY_REFERENCE_IMAGE_FROM_CRSD_TOOL_PATH="${CMAKE_SOURCE_DIR}/tools/sarpy/reference_image_from_crsd.py"
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
if(HDF5_FOUND)
    target_compile_definitions(test_sar_example_unit PRIVATE GRAPHX_SAR_HAS_HDF5=1)
    target_include_directories(test_sar_example_unit PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(test_sar_example_unit PRIVATE ${HDF5_LIBRARIES})
else()
    target_compile_definitions(test_sar_example_unit PRIVATE GRAPHX_SAR_HAS_HDF5=0)
endif()
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
add_dependencies(test_sar_example_unit sar_example)
add_dependencies(test_sar_example_unit graphx_gotcha_to_crsd)
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
add_test(NAME sar_example_ci_lane COMMAND test_sar_example_unit --gtest_filter=CiValidationLaneTest.*)
add_test(NAME sar_example_main_executable
    COMMAND test_sar_example_unit --gtest_filter=SarMainExecutableTest.*)

add_test(NAME sar_example_sarpy_probe_lane
    COMMAND test_sar_example_unit --gtest_filter=SarpyReferenceCompareToolsTest.ProbeCommandsAreLocalOnlyAndNonBlocking:SarpyCrsdValidationHarnessTest.ProbeCommandsDeclareLocalOnlyHarness)
set_tests_properties(sar_example_sarpy_probe_lane PROPERTIES LABELS "sar;sarpy;probe;local-only;gated")

add_test(NAME sar_example_sarpy_integration_lane
    COMMAND test_sar_example_unit --gtest_filter=SarpyReferenceCompareToolsTest.*:SarpyCrsdValidationHarnessTest.*)
set_tests_properties(sar_example_sarpy_integration_lane PROPERTIES LABELS "sar;sarpy;integration;local-only;gated")

add_test(NAME sar_real_gotcha_local_validation
    COMMAND test_sar_example_unit --gtest_filter=LocalGotchaValidationLaneTest.OptionalSmokeRunsOnlyWhenRealDatasetEnvironmentIsSet)
set_tests_properties(sar_real_gotcha_local_validation PROPERTIES
    DISABLED TRUE
    LABELS "sar;gotcha;real-data;local-only;gated")
