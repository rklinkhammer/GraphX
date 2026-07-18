find_package(GTest REQUIRED)
find_package(Threads REQUIRED)
find_package(log4cxx REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GRAPHX_DASHBOARD_CONTRACT_PYTHON "${Python3_EXECUTABLE}" CACHE FILEPATH
    "Python interpreter provisioned with dashboard requirements-contracts.lock")

add_executable(test_dsp_example_unit
    test_dsp_spectrum_demo.cpp
    test_dsp_fhss_demo.cpp
    test_dsp_fhss_iq_generator.cpp
)
if(GRAPHX_BUILD_WEB_DASHBOARD)
target_sources(test_dsp_example_unit PRIVATE
    test_dsp_fhss_dashboard_step1.cpp
    test_dsp_fhss_dashboard_step2_browser.cpp
    test_dsp_fhss_dashboard_step2.cpp
    test_dsp_fhss_dashboard_step3.cpp
    test_dsp_fhss_dashboard_step6.cpp
    test_dsp_fhss_dashboard_step7.cpp
)
endif()

target_compile_definitions(test_dsp_example_unit PRIVATE
    DSP_SPECTRUM_DEMO_EXECUTABLE_PATH="$<TARGET_FILE:dsp_spectrum_demo>"
    DSP_FHSS_DEMO_EXECUTABLE_PATH="$<TARGET_FILE:dsp_fhss_demo>"
    DSP_FHSS_IQ_GENERATOR_EXECUTABLE_PATH="$<TARGET_FILE:dsp_fhss_iq_generator>"
    DSP_SPECTRUM_CONFIG_PATH="${CMAKE_SOURCE_DIR}/libdsp/config/dsp_sine_fft_spectrum_256.json"
    DSP_METAL_SPECTRUM_CONFIG_PATH="${CMAKE_SOURCE_DIR}/libdsp/config/dsp_sine_metal_dft_spectrum_256.json"
    DSP_FHSS_CHANNELIZED_CONFIG_PATH="${CMAKE_SOURCE_DIR}/libdsp/config/fhss_cpsm_channelized_fixture_500msps.json"
    DSP_FHSS_BINARY_RECEIVER_CONFIG_PATH="${CMAKE_SOURCE_DIR}/libdsp/config/fhss_phase2_binary_iq_receiver.json"
    DSP_FHSS_BINARY_CONFIG_PATH="${CMAKE_SOURCE_DIR}/libdsp/config/fhss_cpsm_binary_iq_500msps.json"
    DSP_FHSS_DEMO_MESSAGE_PATH="${CMAKE_SOURCE_DIR}/examples/DSP/fixtures/fhss_demo_messages.json"
    DSP_FHSS_MESSAGE_TOOL_PATH="${CMAKE_SOURCE_DIR}/examples/DSP/tools/fhss_message_tool.py"
    DSP_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
    DSP_CPU_VS_METAL_SCHEMA_PATH="${CMAKE_SOURCE_DIR}/examples/DSP/tools/dsp_cpu_vs_metal_performance_report.schema.json"
    DSP_PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    GRAPHX_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
target_include_directories(test_dsp_example_unit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_dsp_example_unit PRIVATE
    GTest::gtest_main
    Threads::Threads
    log4cxx
    nlohmann_json::nlohmann_json
)
if(GRAPHX_BUILD_WEB_DASHBOARD)
    target_link_libraries(test_dsp_example_unit PRIVATE dsp_fhss_dashboard_api)
endif()
add_dependencies(test_dsp_example_unit
    dsp_spectrum_demo
    dsp_fhss_demo
    dsp_fhss_iq_generator
    dsp_fhss_phase2_characterize
)
if(TARGET graphx_dynamic_nodes)
    add_dependencies(test_dsp_example_unit graphx_dynamic_nodes)
endif()

add_test(NAME dsp_example_unit COMMAND test_dsp_example_unit)
if(GRAPHX_BUILD_WEB_DASHBOARD)
  set(DSP_EXAMPLE_DISCOVERY_EXPECTED_SUITES
    FhssDashboardServerContractTest
    FhssDashboardServerFailureTest
    FhssDashboardConfigurationConcurrencyTest
    FhssDashboardConfigurationTest
    FhssDashboardConfigurationServiceTest
    FhssDashboardRebuildControlTest
    FhssDashboardEventReplayTest
    FhssDashboardVisualizationTest
    FhssDashboardVisualizationCancellationTest)
else()
  set(DSP_EXAMPLE_DISCOVERY_EXPECTED_SUITES "")
endif()
set(DSP_EXAMPLE_DISCOVERY_UNEXPECTED_SUITES
    DashboardServerStep1Test
    DashboardServerStep2Test
    DashboardServerStep3Test
    DashboardServerStep6Test
    DashboardServerStep7Test
    DashboardServerStep8Test)
add_test(NAME dsp_example_unit_discovery
    COMMAND ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:test_dsp_example_unit>
        -DEXPECTED_SUITES=${DSP_EXAMPLE_DISCOVERY_EXPECTED_SUITES}
        -DUNEXPECTED_SUITES=${DSP_EXAMPLE_DISCOVERY_UNEXPECTED_SUITES}
        -P ${CMAKE_SOURCE_DIR}/cmake/CheckGTestDiscovery.cmake)
set_tests_properties(dsp_example_unit_discovery PROPERTIES
    LABELS "dsp;fhss;dashboard;discovery;ci-safe;host-any")

if(GRAPHX_BUILD_WEB_DASHBOARD)
    add_test(NAME fhss_dashboard_api_contracts
        COMMAND ${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
                ${CMAKE_SOURCE_DIR}/examples/DSP/dashboard/api/validate_contracts.py)
    set_tests_properties(fhss_dashboard_api_contracts PROPERTIES
        LABELS "dsp;fhss;dashboard;contracts;ci-safe;host-any")
    add_test(NAME fhss_dashboard_external_operator
        COMMAND ${CMAKE_COMMAND}
            -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DINSTALLED_TREE=OFF -P ${CMAKE_SOURCE_DIR}/cmake/RunDashboardOperator.cmake)
    add_test(NAME fhss_dashboard_installed_tree_operator
        COMMAND ${CMAKE_COMMAND}
            -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DINSTALLED_TREE=ON -P ${CMAKE_SOURCE_DIR}/cmake/RunDashboardOperator.cmake)
    set_tests_properties(fhss_dashboard_external_operator fhss_dashboard_installed_tree_operator
        PROPERTIES LABELS "dsp;fhss;dashboard;operator;ci-safe;host-any" TIMEOUT 60 RUN_SERIAL TRUE)
endif()

add_test(NAME fhss_dashboard_off_build
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DPARENT_BUILD_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/ValidateDashboardOffBuild.cmake)
set_tests_properties(fhss_dashboard_off_build PROPERTIES
    LABELS "dsp;fhss;dashboard;off-build;ci-safe;host-any" TIMEOUT 180 RUN_SERIAL TRUE)

add_test(
    NAME fhss_fixture_topology_generator
    COMMAND ${CMAKE_COMMAND} -E env
            GRAPHX_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_fhss_fixture_topology_generator.py
)

add_test(
    NAME fhss_phase2_candidate_characterization_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/examples/DSP/tools/characterize_fhss_phase2.py
            --characterizer $<TARGET_FILE:dsp_fhss_phase2_characterize>
            --trials 2
            --seed-partition evaluation
)
set_tests_properties(fhss_phase2_candidate_characterization_smoke PROPERTIES
    LABELS "dsp;fhss;phase2;characterization;ci-safe;host-any")

add_test(
    NAME fhss_phase3_independent_analytical
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_fhss_phase3_independent.py
)
set_tests_properties(fhss_phase3_independent_analytical PROPERTIES
    LABELS "dsp;fhss;phase3;independent;ci-safe;host-any"
    ENVIRONMENT "GRAPHX_FHSS_CANONICAL_GENERATOR=$<TARGET_FILE:dsp_fhss_iq_generator>")

add_test(
    NAME fhss_phase3_actual_graph_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/examples/DSP/tools/fhss_phase3_independent.py
            evaluate
            --profile ${CMAKE_SOURCE_DIR}/libdsp/config/fhss_phase3_validation_profile_v6.json
            --receiver $<TARGET_FILE:dsp_fhss_demo>
            --graph ${CMAKE_SOURCE_DIR}/libdsp/config/fhss_phase2_binary_iq_receiver.json
            --plugins ${CMAKE_BINARY_DIR}/plugins
            --partition validation
            --limit 1
            --seed-limit 1
            --smoke
            --raw ${CMAKE_BINARY_DIR}/fhss_phase3_smoke_raw.json
            --report ${CMAKE_BINARY_DIR}/fhss_phase3_smoke_report.json
)
set_tests_properties(fhss_phase3_actual_graph_smoke PROPERTIES
    LABELS "dsp;fhss;phase3;actual-graph;ci-safe;host-any"
    TIMEOUT 90)

add_test(
    NAME fhss_phase4_recorded_infrastructure
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_fhss_phase4_recorded.py
)
set_tests_properties(fhss_phase4_recorded_infrastructure PROPERTIES
    LABELS "dsp;fhss;phase4;recorded-iq;ci-safe;host-any"
    TIMEOUT 30)

add_test(
    NAME fhss_phase5_qualification
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_fhss_phase5_qualification.py
)
set_tests_properties(fhss_phase5_qualification PROPERTIES
    LABELS "dsp;fhss;phase5;qualification;ci-safe;host-any"
    TIMEOUT 30)
