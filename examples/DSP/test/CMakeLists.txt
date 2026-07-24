find_package(GTest REQUIRED)
find_package(Threads REQUIRED)
find_package(log4cxx REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GRAPHX_DASHBOARD_CONTRACT_PYTHON "${Python3_EXECUTABLE}" CACHE FILEPATH
    "Python interpreter provisioned with dashboard requirements-contracts.lock")

set(GRAPHX_LINK_GROUP_BEGIN "")
set(GRAPHX_LINK_GROUP_END "")
if(UNIX AND NOT APPLE)
    set(GRAPHX_LINK_GROUP_BEGIN "-Wl,--start-group")
    set(GRAPHX_LINK_GROUP_END "-Wl,--end-group")
endif()

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
    test_dsp_fhss_dashboard_step4.cpp
    test_dsp_fhss_dashboard_step5.cpp
    test_dsp_fhss_dashboard_step6.cpp
    test_dsp_fhss_dashboard_step7.cpp
    test_dsp_fhss_dashboard_phase7.cpp
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
    graphx_project_options
    dsp
    dsp_fhss_iq_artifacts
    ${GRAPHX_LINK_GROUP_BEGIN}
    graph
    gpu
    ${GRAPHX_LINK_GROUP_END}
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
    FhssDashboardObservationTest
    FhssDashboardJobControllerTest
    FhssDashboardVisualizationTest
    FhssDashboardVisualizationCancellationTest)
  list(APPEND DSP_EXAMPLE_DISCOVERY_EXPECTED_SUITES
    FhssDashboardInvestigationBundleTest)
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
    add_test(NAME fhss_dashboard_phase4_evidence_smoke
        COMMAND ${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_fhss_dashboard_phase4_operator.py)
    set_tests_properties(fhss_dashboard_phase4_evidence_smoke PROPERTIES
        LABELS "dsp;fhss;dashboard;phase4;evidence;ci-safe;host-any")
    add_test(NAME fhss_dashboard_phase5_external_operator
        COMMAND ${CMAKE_COMMAND}
            -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DPHASE=5 -DINSTALLED_TREE=OFF -P ${CMAKE_SOURCE_DIR}/cmake/RunDashboardOperator.cmake)
    add_test(NAME fhss_dashboard_phase5_installed_tree_operator
        COMMAND ${CMAKE_COMMAND}
            -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DPHASE=5 -DINSTALLED_TREE=ON -P ${CMAKE_SOURCE_DIR}/cmake/RunDashboardOperator.cmake)
    set_tests_properties(fhss_dashboard_phase5_external_operator
        fhss_dashboard_phase5_installed_tree_operator PROPERTIES
        TIMEOUT 600 RUN_SERIAL TRUE
        LABELS "dsp;fhss;dashboard;phase5;operator;host-any")
    add_test(NAME fhss_dashboard_phase7_external_operator
        COMMAND ${CMAKE_COMMAND}
            -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DPHASE=7 -DINSTALLED_TREE=OFF -P ${CMAKE_SOURCE_DIR}/cmake/RunDashboardOperator.cmake)
    add_test(NAME fhss_dashboard_phase7_installed_tree_operator
        COMMAND ${CMAKE_COMMAND}
            -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DPHASE=7 -DINSTALLED_TREE=ON -P ${CMAKE_SOURCE_DIR}/cmake/RunDashboardOperator.cmake)
    set_tests_properties(fhss_dashboard_phase7_external_operator
        fhss_dashboard_phase7_installed_tree_operator PROPERTIES
        TIMEOUT 1500 RUN_SERIAL TRUE
        LABELS "dsp;fhss;dashboard;phase7;operator;browser;host-any")
    add_test(NAME fhss_dashboard_phase8_external_operator
        COMMAND ${CMAKE_COMMAND}
            -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DPHASE=8 -DINSTALLED_TREE=OFF -P ${CMAKE_SOURCE_DIR}/cmake/RunDashboardOperator.cmake)
    add_test(NAME fhss_dashboard_phase8_installed_tree_operator
        COMMAND ${CMAKE_COMMAND}
            -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DPHASE=8 -DINSTALLED_TREE=ON -P ${CMAKE_SOURCE_DIR}/cmake/RunDashboardOperator.cmake)
    set_tests_properties(fhss_dashboard_phase8_external_operator
        fhss_dashboard_phase8_installed_tree_operator PROPERTIES
        TIMEOUT 1800 RUN_SERIAL TRUE
        LABELS "dsp;fhss;dashboard;phase8;qualification;operator;browser;host-any")
    add_test(NAME fhss_dashboard_phase8_security_static
        COMMAND ${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            ${CMAKE_SOURCE_DIR}/examples/DSP/dashboard/operator/phase8_qualification.py
            --static-only --asset-root ${CMAKE_SOURCE_DIR}/examples/DSP/dashboard/dist
            --output ${CMAKE_BINARY_DIR}/fhss-dashboard-phase8-fuzz-security.json)
    set_tests_properties(fhss_dashboard_phase8_security_static PROPERTIES
        TIMEOUT 30
        LABELS "dsp;fhss;dashboard;phase8;security;static;ci-safe;host-any")
    add_test(NAME fhss_dashboard_phase0_baseline
        COMMAND ${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_fhss_dashboard_phase0.py)
    set_tests_properties(fhss_dashboard_phase0_baseline PROPERTIES
        ENVIRONMENT "GRAPHX_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
        TIMEOUT 60
        LABELS "dsp;fhss;dashboard;phase0;baseline;identity;policy;ci-safe;host-any")
    add_test(NAME fhss_dashboard_phase1_frontend
        COMMAND ${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_fhss_dashboard_phase1.py)
    set_tests_properties(fhss_dashboard_phase1_frontend PROPERTIES
        ENVIRONMENT "GRAPHX_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
        TIMEOUT 60
        LABELS "dsp;fhss;dashboard;phase1;frontend;identity;accessibility;ci-safe;host-any")
    add_test(NAME fhss_dashboard_v2_phase3_identity
        COMMAND ${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_fhss_dashboard_v2_phase3.py)
    set_tests_properties(fhss_dashboard_v2_phase3_identity PROPERTIES
        ENVIRONMENT "GRAPHX_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
        TIMEOUT 60
        LABELS "dsp;fhss;dashboard;v2-phase3;identity;metrics;schema;ci-safe;host-any")
    add_test(NAME fhss_dashboard_phase0_source_smoke
        COMMAND ${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            ${CMAKE_SOURCE_DIR}/examples/DSP/dashboard/operator/phase0_smoke.py
            --executable $<TARGET_FILE:dsp_fhss_demo>
            --assets ${CMAKE_BINARY_DIR}/examples/DSP/fhss-dashboard-dist
            --graph-config ${CMAKE_SOURCE_DIR}/libdsp/config/fhss_phase2_binary_iq_receiver.json)
    set_tests_properties(fhss_dashboard_phase0_source_smoke PROPERTIES
        TIMEOUT 30 RUN_SERIAL TRUE
        LABELS "dsp;fhss;dashboard;phase0;source;smoke;ci-safe;host-any")
    add_test(NAME fhss_dashboard_phase0_installed_inventory
        COMMAND ${CMAKE_COMMAND}
            -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/ValidateDashboardPhase0Install.cmake)
    set_tests_properties(fhss_dashboard_phase0_installed_inventory PROPERTIES
        TIMEOUT 180 RUN_SERIAL TRUE
        LABELS "dsp;fhss;dashboard;phase0;installed;inventory;ci-safe;host-any")
    add_test(NAME fhss_dashboard_compiled_live_source
        COMMAND ${CMAKE_COMMAND}
            -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DINSTALLED_TREE=OFF
            -P ${CMAKE_SOURCE_DIR}/cmake/RunCompiledDashboardLive.cmake)
    add_test(NAME fhss_dashboard_compiled_live_installed
        COMMAND ${CMAKE_COMMAND}
            -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DINSTALLED_TREE=ON
            -P ${CMAKE_SOURCE_DIR}/cmake/RunCompiledDashboardLive.cmake)
    set_tests_properties(
        fhss_dashboard_compiled_live_source
        fhss_dashboard_compiled_live_installed PROPERTIES
        TIMEOUT 300 RUN_SERIAL TRUE
        LABELS "dsp;fhss;dashboard;phase1;compiled;live;operator;browser;host-any")
    add_test(NAME fhss_dashboard_phase8_completion_evidence
        COMMAND ${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_fhss_dashboard_phase8_completion.py)
    set_tests_properties(fhss_dashboard_phase8_completion_evidence PROPERTIES
        TIMEOUT 30
        LABELS "dsp;fhss;dashboard;phase8;qualification;evidence;ci-safe;host-any")
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
    add_test(NAME fhss_dashboard_phase2_external_operator
        COMMAND ${CMAKE_COMMAND}
            -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DPHASE=2 -DINSTALLED_TREE=OFF -P ${CMAKE_SOURCE_DIR}/cmake/RunDashboardOperator.cmake)
    add_test(NAME fhss_dashboard_phase2_installed_tree_operator
        COMMAND ${CMAKE_COMMAND}
            -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DPHASE=2 -DINSTALLED_TREE=ON -P ${CMAKE_SOURCE_DIR}/cmake/RunDashboardOperator.cmake)
    add_test(NAME fhss_dashboard_phase3_external_operator
        COMMAND ${CMAKE_COMMAND} -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DPHASE=3 -DINSTALLED_TREE=OFF -P ${CMAKE_SOURCE_DIR}/cmake/RunDashboardOperator.cmake)
    add_test(NAME fhss_dashboard_phase3_installed_tree_operator
        COMMAND ${CMAKE_COMMAND} -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DPHASE=3 -DINSTALLED_TREE=ON -P ${CMAKE_SOURCE_DIR}/cmake/RunDashboardOperator.cmake)
    add_test(NAME fhss_dashboard_phase4_external_operator
        COMMAND ${CMAKE_COMMAND} -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DPHASE=4 -DINSTALLED_TREE=OFF -P ${CMAKE_SOURCE_DIR}/cmake/RunDashboardOperator.cmake)
    add_test(NAME fhss_dashboard_phase4_installed_tree_operator
        COMMAND ${CMAKE_COMMAND} -DCONTRACT_PYTHON=${GRAPHX_DASHBOARD_CONTRACT_PYTHON}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DPHASE=4 -DINSTALLED_TREE=ON -P ${CMAKE_SOURCE_DIR}/cmake/RunDashboardOperator.cmake)
    set_tests_properties(fhss_dashboard_external_operator fhss_dashboard_installed_tree_operator
        fhss_dashboard_phase2_external_operator fhss_dashboard_phase2_installed_tree_operator
        fhss_dashboard_phase3_external_operator fhss_dashboard_phase3_installed_tree_operator
        fhss_dashboard_phase4_external_operator fhss_dashboard_phase4_installed_tree_operator
        PROPERTIES LABELS "dsp;fhss;dashboard;operator;ci-safe;host-any" TIMEOUT 180 RUN_SERIAL TRUE)
    # Phase 4 runs several sequential synthetic receiver cases, each retaining
    # its own strict 120-second execution, 30-second request, and 5-second Stop
    # bounds.  Match the Phase 5 outer envelope so installation and browser
    # evidence overhead cannot expire CTest before those assertions report.
    set_tests_properties(fhss_dashboard_phase4_external_operator
        fhss_dashboard_phase4_installed_tree_operator
        PROPERTIES LABELS "dsp;fhss;dashboard;operator;phase4;pre-browser-partial;ci-safe;host-any"
        TIMEOUT 600)
    # Disabled-test audit (15 total): the source/install pairs for the generic
    # lane and Phases 2, 3, 4, 5, 7, and 8 are seven prototype-DOM pairs (14);
    # phase8_security_static is the prototype handcrafted-script sink scanner
    # (1). Their selectors and scanner model the deleted dashboard/index.html,
    # so enabling them would test a retired implementation. The enabled
    # compiled_live source/install pair above replaces their shared API,
    # lifecycle, job, investigation, event-recovery, and browser-load coverage.
    set_tests_properties(
        fhss_dashboard_external_operator fhss_dashboard_installed_tree_operator
        fhss_dashboard_phase2_external_operator fhss_dashboard_phase2_installed_tree_operator
        fhss_dashboard_phase3_external_operator fhss_dashboard_phase3_installed_tree_operator
        fhss_dashboard_phase4_external_operator fhss_dashboard_phase4_installed_tree_operator
        fhss_dashboard_phase5_external_operator fhss_dashboard_phase5_installed_tree_operator
        fhss_dashboard_phase7_external_operator fhss_dashboard_phase7_installed_tree_operator
        fhss_dashboard_phase8_external_operator fhss_dashboard_phase8_installed_tree_operator
        fhss_dashboard_phase8_security_static
        PROPERTIES DISABLED TRUE
        LABELS "dsp;fhss;dashboard;retired-prototype-operator;deferred")
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
