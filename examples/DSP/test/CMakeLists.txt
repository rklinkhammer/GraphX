find_package(GTest REQUIRED)
find_package(Threads REQUIRED)
find_package(log4cxx REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(test_dsp_example_unit
    test_dsp_spectrum_demo.cpp
    test_dsp_fhss_demo.cpp
    test_dsp_fhss_iq_generator.cpp
    test_dsp_fhss_dashboard_step1.cpp
    test_dsp_fhss_dashboard_step2_browser.cpp
    test_dsp_fhss_dashboard_step2.cpp
    test_dsp_fhss_dashboard_step3.cpp
    test_dsp_fhss_dashboard_step6.cpp
    test_dsp_fhss_dashboard_step7.cpp
    test_dsp_fhss_dashboard_step8.cpp
)

target_compile_definitions(test_dsp_example_unit PRIVATE
    DSP_SPECTRUM_DEMO_EXECUTABLE_PATH="$<TARGET_FILE:dsp_spectrum_demo>"
    DSP_FHSS_DEMO_EXECUTABLE_PATH="$<TARGET_FILE:dsp_fhss_demo>"
    DSP_FHSS_IQ_GENERATOR_EXECUTABLE_PATH="$<TARGET_FILE:dsp_fhss_iq_generator>"
    DSP_SPECTRUM_CONFIG_PATH="${CMAKE_SOURCE_DIR}/libdsp/config/dsp_sine_fft_spectrum_256.json"
    DSP_METAL_SPECTRUM_CONFIG_PATH="${CMAKE_SOURCE_DIR}/libdsp/config/dsp_sine_metal_dft_spectrum_256.json"
    DSP_FHSS_CHANNELIZED_CONFIG_PATH="${CMAKE_SOURCE_DIR}/libdsp/config/fhss_cpsm_channelized_fixture_500msps.json"
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
    dsp_fhss_dashboard_api
    log4cxx
    nlohmann_json::nlohmann_json
)
add_dependencies(test_dsp_example_unit
    dsp_spectrum_demo
    dsp_fhss_demo
    dsp_fhss_iq_generator
)
if(TARGET graphx_dynamic_nodes)
    add_dependencies(test_dsp_example_unit graphx_dynamic_nodes)
endif()

add_test(NAME dsp_example_unit COMMAND test_dsp_example_unit)
set(DSP_EXAMPLE_DISCOVERY_EXPECTED_SUITES
    FhssDashboardServerContractTest
    FhssDashboardServerFailureTest
    FhssDashboardConfigurationConcurrencyTest
    FhssDashboardConfigurationTest
    FhssDashboardConfigurationServiceTest
    FhssDashboardRebuildControlTest
    FhssDashboardEventReplayTest
    FhssDashboardVisualizationTest
    FhssDashboardArtifactExportTest)
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

add_test(
    NAME fhss_fixture_topology_generator
    COMMAND ${CMAKE_COMMAND} -E env
            GRAPHX_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_fhss_fixture_topology_generator.py
)
