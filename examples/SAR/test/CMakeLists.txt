find_package(GTest REQUIRED)
find_package(Threads REQUIRED)
find_package(log4cxx REQUIRED)
find_package(HDF5 COMPONENTS C QUIET)

set(SAR_LOG4CXX_LIB log4cxx)
if(TARGET log4cxx::log4cxx)
    set(SAR_LOG4CXX_LIB log4cxx::log4cxx)
endif()

set(SAR_TEST_SUPPORT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/AzimuthTileSplitNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/D2HAsyncAccelNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/H2DAsyncAccelNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ImageTileMergeNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/RangeCompressionNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/RangeWindowNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SarDiagnosticsSinkNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SarMaterializedImageSinkNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SarVisualizationSinkNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SarBackprojectionTransformAccelNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/GotchaReplaySourceNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/CrsdFocusedImageTransformNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/CrsdFocusedImageTransformMetal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/CrsdFocusedImageSinkNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/SyntheticApertureIqSourceNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/OrderedCrsdSetInputSourceNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/CrsdApertureAssemblyAdapterNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/io/CrsdReader.cpp
)

set(SAR_CRSD_IO_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_graphx_gotcha_to_crsd_cli.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_normalized_sar_product.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_product_validator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gotcha_input_ordering.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gotcha_mat_inspector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gotcha_to_crsd_metadata_mapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_crsd_io.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_product_chunker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_io_utilities.cpp
)

set(SAR_NODE_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_azimuth_tile_split_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_image_tile_merge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_image_tile_merge_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_range_compression_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_range_window_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_diagnostics_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_materialized_image_sink_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_visualization_sink_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_accel_nodes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_cpu_reference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_cpu_reference_backprojection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gotcha_dataset_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_accel_token_guardrails.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_token_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_typed_transport_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_crsd_input_source_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_crsd_aperture_assembly_adapter_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_crsd_focused_image_transform_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_crsd_focused_image_metal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_crsd_focused_image_sink.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_metal_truth_in_labeling_guardrails.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_synthetic_aperture_iq_source_node.cpp
)

set(SAR_RUNTIME_INTEGRATION_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_json_pipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_projectile_scenario.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_runtime_helpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_trace_schema.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_json_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_main_executable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_scenario_manifest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_scenario_fixture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ci_correctness_lane.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ci_tiny_fixture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ci_validation_lane.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_baseline_compare.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_baseline_guardrails.cpp
)

set(SAR_LOCAL_ONLY_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_external_baseline_policy_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_local_runner_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_scenario_image_path_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_graphx_scenario_runner_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gotcha_back_adapter_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_image_comparator_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_image_comparator_metrics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_frozen_scenario_replay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sarpy_metadata_harness.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_local_baseline_runner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sarpy_reference_compare_tools.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_graphx_vs_baseline_harness.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sar_baseline_substitution_experiment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_sarpy_crsd_validation_harness.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_graphx_image_comparison_lane.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_local_gotcha_validation_lane.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gotcha_real_full_aperture_validation.cpp
)

file(GLOB SAR_ALL_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
set(SAR_OWNED_TEST_SOURCES
    ${SAR_CRSD_IO_TEST_SOURCES}
    ${SAR_NODE_TEST_SOURCES}
    ${SAR_RUNTIME_INTEGRATION_TEST_SOURCES}
    ${SAR_LOCAL_ONLY_TEST_SOURCES}
)
set(_sar_all_sorted ${SAR_ALL_TEST_SOURCES})
set(_sar_owned_sorted ${SAR_OWNED_TEST_SOURCES})
list(SORT _sar_all_sorted)
list(SORT _sar_owned_sorted)
set(_sar_owned_unique ${_sar_owned_sorted})
list(REMOVE_DUPLICATES _sar_owned_unique)
if(NOT "${_sar_all_sorted}" STREQUAL "${_sar_owned_sorted}" OR
   NOT "${_sar_owned_sorted}" STREQUAL "${_sar_owned_unique}")
    message(FATAL_ERROR
        "Every SAR test source must belong to exactly one PR2 ownership lane")
endif()

add_library(graphx_sar_test_support STATIC ${SAR_TEST_SUPPORT_SOURCES})
target_include_directories(graphx_sar_test_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(graphx_sar_test_support PUBLIC
    graph gpu dsp graphx_project_options)
if(HDF5_FOUND)
    target_compile_definitions(graphx_sar_test_support PRIVATE
        GRAPHX_SAR_HAS_HDF5=1)
    target_include_directories(graphx_sar_test_support PRIVATE
        ${HDF5_INCLUDE_DIRS})
    target_link_libraries(graphx_sar_test_support PRIVATE ${HDF5_LIBRARIES})
else()
    target_compile_definitions(graphx_sar_test_support PRIVATE
        GRAPHX_SAR_HAS_HDF5=0)
endif()

add_executable(test_sar_crsd_io ${SAR_CRSD_IO_TEST_SOURCES})
add_executable(test_sar_nodes ${SAR_NODE_TEST_SOURCES})
add_executable(test_sar_runtime_integration
    ${SAR_RUNTIME_INTEGRATION_TEST_SOURCES})
add_executable(test_sar_local_only ${SAR_LOCAL_ONLY_TEST_SOURCES})

function(configure_sar_test_target target)
target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_definitions(${target} PRIVATE
    GRAPHX_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/examples/SAR/plugins"
    GPU_PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    # PR7: Canonical SAR configs (CPU stripmap family)
    SAR_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_simulated.json"
    SAR_DEFINITIVE_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_definitive.json"
    # PR7: Specialized stripmap variants (kept for dedicated test scenarios)
    SAR_FANOUT_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_fanout.json"
    SAR_MATCHED_FILTER_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_matched_filter.json"
    SAR_MATERIALIZED_IMAGE_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_stripmap_materialized_image.json"
    SAR_GOTCHA_REPLAY_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gotcha_replay_fixture.json"
    SAR_PROJECTILE_JSON_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_projectile_approach.json"
    SAR_BENCHMARK_EXECUTABLE_PATH="$<TARGET_FILE:sar_benchmark>"
    SAR_EXAMPLE_EXECUTABLE_PATH="$<TARGET_FILE:sar_example>"
    SAR_GOTCHA_TO_CRSD_EXECUTABLE_PATH="$<TARGET_FILE:graphx_gotcha_to_crsd>"
    SAR_EXTERNAL_BASELINE_POLICY_PATH="${CMAKE_SOURCE_DIR}/plan/archive/2026-06-baseline/reviews/SAR_EXTERNAL_BASELINE_POLICY.md"
    SAR_BASELINE_PACKAGE_REGISTRY_PATH="${CMAKE_SOURCE_DIR}/plan/archive/2026-06-baseline/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json"
    SAR_GOTCHA_INPUT_MANIFEST_SCHEMA_PATH="${CMAKE_SOURCE_DIR}/README.md"
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
    SAR_FOCUSED_IMAGE_SCHEMA_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/sar_focused_image_artifact_schema.json"
    SARPY_METADATA_HARNESS_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/sarpy_metadata_harness.py"
    SAR_LOCAL_BASELINE_RUNNER_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/sar_local_baseline_runner.py"
    SAR_GRAPHX_VS_BASELINE_HARNESS_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/sar_graphx_vs_baseline_harness.py"
    SAR_BASELINE_SUBSTITUTION_EXPERIMENT_PATH="${CMAKE_SOURCE_DIR}/examples/SAR/tools/sar_baseline_substitution_experiment.py"
    SARPY_METADATA_HARNESS_GUIDE_PATH="${CMAKE_SOURCE_DIR}/README.md"
    SARPY_REFERENCE_IMAGE_FROM_GOTCHA_TOOL_PATH="${CMAKE_SOURCE_DIR}/tools/sarpy/reference_image_from_gotcha.py"
    SARPY_COMPARE_IMAGES_TOOL_PATH="${CMAKE_SOURCE_DIR}/tools/sarpy/compare_images.py"
    SARPY_COMPARE_IMAGES_SCHEMA_PATH="${CMAKE_SOURCE_DIR}/tools/sarpy/compare_images_report.schema.json"
    SARPY_REQUIREMENTS_PATH="${CMAKE_SOURCE_DIR}/tools/sarpy/requirements.txt"
    SARPY_VALIDATE_CRSD_TOOL_PATH="${CMAKE_SOURCE_DIR}/tools/sarpy/validate_crsd.py"
    SARPY_REFERENCE_IMAGE_FROM_CRSD_TOOL_PATH="${CMAKE_SOURCE_DIR}/tools/sarpy/reference_image_from_crsd.py"
    SAR_CRSD_TINY_FIXTURE_BASE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/crsd_binary_tiny_multisegment"
    # PR7: CRSD input mode variants (kept for dedicated input source tests)
    SAR_CRSD_TINY_CONFIG_PATHS_JSON="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_crsd_tiny_fixture_set_input.json"
    SAR_CRSD_TINY_CONFIG_DIRECTORY_JSON="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_crsd_tiny_fixture_set_input_directory.json"
    SAR_CRSD_TINY_CONFIG_MANIFEST_JSON="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_crsd_tiny_fixture_set_input_manifest.json"
    # PR7: CRSD CPU canonical and full pipeline variant
    SAR_CRSD_TINY_FOCUSED_IMAGE_CPU_CONFIG_JSON="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_crsd_tiny_fixture_focused_image_cpu.json"
    SAR_CRSD_TINY_FULL_PIPELINE_CONFIG_JSON="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_crsd_tiny_fixture_full_pipeline.json"
    # PR7: CRSD GPU experimental candidate (marked as such in BASELINE.md)
    SAR_CRSD_TINY_FOCUSED_IMAGE_METAL_CONFIG_JSON="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_crsd_tiny_fixture_focused_image_metal.json"
    # PR7: Local-only GOTCHA validation config
    SAR_CRSD_GOTCHA_LOCAL_VALIDATION_CONFIG_JSON="${CMAKE_CURRENT_SOURCE_DIR}/../config/sar_crsd_gotcha_local_validation.json"
)
target_link_libraries(${target} PRIVATE
    graphx_sar_test_support
    ${SAR_LOG4CXX_LIB}
    GTest::gtest_main
    Threads::Threads
    graphx_project_options
)
if(HDF5_FOUND)
    target_compile_definitions(${target} PRIVATE GRAPHX_SAR_HAS_HDF5=1)
    target_include_directories(${target} PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(${target} PRIVATE ${HDF5_LIBRARIES})
else()
    target_compile_definitions(${target} PRIVATE GRAPHX_SAR_HAS_HDF5=0)
endif()
add_dependencies(${target}
    azimuth_tile_split_node
    d2h_async_node
    h2d_async_node
    image_tile_merge_node
    range_window_node
    range_compression_node
    sar_pulse_fanout_node
    sar_diagnostics_sink_node
    sar_materialized_image_sink_node
    sar_visualization_sink_node
    sar_backprojection_transform_node
    synthetic_aperture_iq_source_node
    gotcha_replay_source_node
    crsd_input_source_node
    crsd_aperture_assembly_adapter_node
    crsd_focused_image_transform_node
    crsd_focused_image_transform_metal_node
    crsd_focused_image_sink_node
    sar_benchmark
    sar_example
    graphx_gotcha_to_crsd
)
if(TARGET graphx_dynamic_nodes)
    add_dependencies(${target} graphx_dynamic_nodes)
endif()
endfunction()

foreach(_target IN ITEMS
        test_sar_crsd_io
        test_sar_nodes
        test_sar_runtime_integration
        test_sar_local_only)
    configure_sar_test_target(${_target})
endforeach()

add_test(NAME sar_crsd_io COMMAND test_sar_crsd_io)
set_tests_properties(sar_crsd_io PROPERTIES LABELS "sar;crsd;io;ci-safe")
add_test(NAME sar_nodes COMMAND test_sar_nodes)
set_tests_properties(sar_nodes PROPERTIES LABELS "sar;nodes;unit;ci-safe")
add_test(NAME sar_runtime_integration COMMAND test_sar_runtime_integration)
set_tests_properties(sar_runtime_integration PROPERTIES
    LABELS "sar;runtime;integration;ci-safe")
add_test(NAME sar_local_only COMMAND test_sar_local_only)
set_tests_properties(sar_local_only PROPERTIES
    LABELS "sar;local-only;gated")

function(add_sar_discovery_test name target expected_suite labels)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXECUTABLE=$<TARGET_FILE:${target}>
            -DEXPECTED_SUITE=${expected_suite}
            -P ${CMAKE_SOURCE_DIR}/cmake/CheckGTestDiscovery.cmake)
    set_tests_properties(${name} PROPERTIES LABELS "${labels}")
endfunction()

add_sar_discovery_test(sar_crsd_io_discovery test_sar_crsd_io
    CrsdIoTest "sar;crsd;ownership;discovery;ci-safe")
add_sar_discovery_test(sar_nodes_discovery test_sar_nodes
    AzimuthTileSplitNodeTest "sar;nodes;ownership;discovery;ci-safe")
add_sar_discovery_test(sar_runtime_integration_discovery
    test_sar_runtime_integration SarJsonRuntimeTest
    "sar;runtime;ownership;discovery;ci-safe")
add_sar_discovery_test(sar_local_only_discovery test_sar_local_only
    LocalRunnerContractTest "sar;local-only;ownership;discovery;gated")

add_test(NAME sar_example_ci_lane
    COMMAND test_sar_runtime_integration --gtest_filter=CiValidationLaneTest.*)
add_test(NAME sar_example_main_executable
    COMMAND test_sar_runtime_integration --gtest_filter=SarMainExecutableTest.*)

add_test(NAME sar_example_sarpy_probe_lane
    COMMAND test_sar_local_only --gtest_filter=SarpyReferenceCompareToolsTest.ProbeCommandsAreLocalOnlyAndNonBlocking:SarpyCrsdValidationHarnessTest.ProbeCommandsDeclareLocalOnlyHarness)
set_tests_properties(sar_example_sarpy_probe_lane PROPERTIES LABELS "sar;sarpy;probe;local-only;gated")

add_test(NAME sar_example_sarpy_integration_lane
    COMMAND test_sar_local_only --gtest_filter=SarpyReferenceCompareToolsTest.*:SarpyCrsdValidationHarnessTest.*)
set_tests_properties(sar_example_sarpy_integration_lane PROPERTIES LABELS "sar;sarpy;integration;local-only;gated")

add_test(NAME sar_real_gotcha_local_validation
    COMMAND test_sar_local_only --gtest_filter=LocalGotchaValidationLaneTest.OptionalSmokeRunsOnlyWhenRealDatasetEnvironmentIsSet)
set_tests_properties(sar_real_gotcha_local_validation PROPERTIES
    DISABLED TRUE
    LABELS "sar;gotcha;real-data;local-only;gated")
