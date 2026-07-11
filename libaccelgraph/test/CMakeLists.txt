find_package(GTest REQUIRED)

file(GLOB ACCELGRAPH_UNIT_TEST_SOURCES CONFIGURE_DEPENDS "unit/test_*.cpp")
file(GLOB ACCELGRAPH_BENCHMARK_SOURCES CONFIGURE_DEPENDS "bench/*.cpp")

if(NOT ACCELGRAPH_ENABLE_CUDA)
    list(FILTER ACCELGRAPH_UNIT_TEST_SOURCES EXCLUDE REGEX "test_accelgraph_phase6b_spectrum_cuda\\.cpp$")
endif()

if(ACCELGRAPH_UNIT_TEST_SOURCES)
    add_executable(test_libaccelgraph_smoke ${ACCELGRAPH_UNIT_TEST_SOURCES})
    target_compile_definitions(test_libaccelgraph_smoke PRIVATE
        PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    )
    target_include_directories(test_libaccelgraph_smoke PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_SOURCE_DIR}/libdsp/include
    )
    target_link_libraries(test_libaccelgraph_smoke PRIVATE
        accelgraph
        graph
        gpu
        GTest::gtest_main
        graphx_project_options
    )
    add_dependencies(test_libaccelgraph_smoke
        accelgraph_host_ingress_node
        accelgraph_host_to_device_node
        accelgraph_device_to_host_node
        accelgraph_host_egress_node
        accelgraph_release_lease_node
        accelgraph_sine_wave_source_node
        accelgraph_spectrum_analysis_node
        accelgraph_spectrum_sink_node
        accelgraph_fhss_downconverter_node
        accelgraph_fhss_downconverter_sink_node
        accelgraph_fhss_channelizer_node
        accelgraph_fhss_channelizer_sink_node
        accelgraph_fhss_per_channel_pulse_detector_node
        accelgraph_fhss_per_channel_pulse_detector_sink_node
        accelgraph_fhss_branch_metric_node
        accelgraph_fhss_branch_metric_sink_node
    )
    if(TARGET graphx_dynamic_nodes)
        add_dependencies(test_libaccelgraph_smoke graphx_dynamic_nodes)
    endif()
    add_test(NAME libaccelgraph_smoke COMMAND test_libaccelgraph_smoke)
    set_tests_properties(libaccelgraph_smoke PROPERTIES
        LABELS "accelgraph;smoke;unit;host-any"
        TIMEOUT 1800)

    add_test(NAME libaccelgraph_fhss_hybrid_pipeline
        COMMAND test_libaccelgraph_smoke
            --gtest_filter=AccelGraphFhssHybridPipelineTest.*)
    set_tests_properties(libaccelgraph_fhss_hybrid_pipeline PROPERTIES
        LABELS "accelgraph;fhss;hybrid;pipeline;unit;host-any"
        TIMEOUT 600)

    set(ACCELGRAPH_DISCOVERY_EXPECTED_SUITES
        AccelGraphFhssContractTest
        AccelGraphFhssBranchMetricTopologyTest
        AccelGraphFhssChannelizerTopologyTest
        AccelGraphFhssEvidenceTest
        AccelGraphFhssHybridPipelineTest
        AccelGraphFhssDetectorTopologyTest
        AccelGraphFhssDownconverterTopologyTest
        AccelGraphScaffoldTest
        AccelGraphTopologyJsonOwnershipTest
        AccelGraphTransferTopologyTest
        AccelGraphCudaTransferTopologyTest
        CudaGraphExecutorContractTest
        AccelGraphSpectrumTopologyTest
    )
    if(ACCELGRAPH_ENABLE_CUDA)
        list(APPEND ACCELGRAPH_DISCOVERY_EXPECTED_SUITES
            AccelGraphCudaSpectrumBackendMatrixTest)
    endif()

    add_test(NAME libaccelgraph_smoke_discovery
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXECUTABLE=$<TARGET_FILE:test_libaccelgraph_smoke>
            -DEXPECTED_SUITES=${ACCELGRAPH_DISCOVERY_EXPECTED_SUITES}
            -P ${CMAKE_SOURCE_DIR}/cmake/CheckGTestDiscovery.cmake)
    set_tests_properties(libaccelgraph_smoke_discovery PROPERTIES
        LABELS "accelgraph;smoke;unit;ownership;discovery;host-any")
endif()

if(ACCELGRAPH_BENCHMARK_SOURCES)
    add_executable(accelgraph_phase7_benchmark ${ACCELGRAPH_BENCHMARK_SOURCES})
    target_compile_definitions(accelgraph_phase7_benchmark PRIVATE
        PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    )
    target_include_directories(accelgraph_phase7_benchmark PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
    target_link_libraries(accelgraph_phase7_benchmark PRIVATE
        accelgraph
        graph
        gpu
        graphx_project_options
    )

    if(TARGET graphx_dynamic_nodes)
        add_dependencies(accelgraph_phase7_benchmark graphx_dynamic_nodes)
    endif()

    add_test(NAME accelgraph_phase7_benchmark_smoke
        COMMAND accelgraph_phase7_benchmark --frames=4 --warmup=1)
    set_tests_properties(accelgraph_phase7_benchmark_smoke PROPERTIES
        LABELS "accelgraph;benchmark;phase7;host-any")
endif()
