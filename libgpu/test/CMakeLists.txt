find_package(GTest REQUIRED)
find_package(log4cxx REQUIRED)

file(GLOB GPU_STUB_UNIT_TEST_SOURCES CONFIGURE_DEPENDS "unit/test_*.cpp")
file(GLOB GPU_BACKEND_UNIT_TEST_SOURCES CONFIGURE_DEPENDS "backend_unit/test_*.cpp")
file(GLOB GPU_INTEGRATION_TEST_SOURCES CONFIGURE_DEPENDS "integration/test_*.cpp")
file(GLOB GPU_PERF_TEST_SOURCES CONFIGURE_DEPENDS "perf/test_*.cpp")

if(GPU_STUB_UNIT_TEST_SOURCES)
    add_executable(test_libgpu_stub_unit ${GPU_STUB_UNIT_TEST_SOURCES})
    target_include_directories(test_libgpu_stub_unit PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_SOURCE_DIR}/libgraph/test/include
    )
    target_compile_definitions(test_libgpu_stub_unit PRIVATE
        PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    )
    target_link_libraries(test_libgpu_stub_unit PRIVATE
        gpu
        GTest::gtest_main
        Threads::Threads
        log4cxx
        graphx_project_options
    )
    if(GRAPHX_BUILD_GPU_STUB_PLUGINS)
        add_dependencies(test_libgpu_stub_unit
            cuda_host_ingress_pinned_source_node
            cuda_h2d_async_node
            cuda_d2h_async_node
            cuda_host_egress_sink_node
            cuda_lease_release_node
            sycl_host_ingress_pinned_source_node
            sycl_h2d_async_node
            sycl_d2h_async_node
            sycl_host_egress_sink_node
            sycl_lease_release_node
        )
    endif()
    add_test(NAME libgpu_stub_unit COMMAND test_libgpu_stub_unit)
endif()

if((GRAPHX_CUDA_GRAPH_NODES_ENABLED OR GRAPHX_SYCL_GRAPH_NODES_ENABLED) AND GPU_BACKEND_UNIT_TEST_SOURCES)
    add_executable(test_libgpu_backend_unit ${GPU_BACKEND_UNIT_TEST_SOURCES})
    target_include_directories(test_libgpu_backend_unit PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_SOURCE_DIR}/libgraph/test/include
    )
    target_compile_definitions(test_libgpu_backend_unit PRIVATE
        PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    )
    target_link_libraries(test_libgpu_backend_unit PRIVATE
        gpu
        GTest::gtest_main
        Threads::Threads
        log4cxx
        graphx_project_options
    )
    add_test(NAME libgpu_backend_unit COMMAND test_libgpu_backend_unit)
endif()

if(GPU_INTEGRATION_TEST_SOURCES)
    add_executable(test_libgpu_integration ${GPU_INTEGRATION_TEST_SOURCES})
    target_include_directories(test_libgpu_integration PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_SOURCE_DIR}/libgraph/test/include
    )
    target_compile_definitions(test_libgpu_integration PRIVATE
        PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    )
    target_link_libraries(test_libgpu_integration PRIVATE
        gpu
        GTest::gtest_main
        Threads::Threads
        log4cxx
        graphx_project_options
    )
    add_test(NAME libgpu_integration COMMAND test_libgpu_integration)
endif()

if(GPU_PERF_TEST_SOURCES)
    add_executable(test_libgpu_perf ${GPU_PERF_TEST_SOURCES})
    target_include_directories(test_libgpu_perf PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_SOURCE_DIR}/libgraph/test/include
    )
    target_compile_definitions(test_libgpu_perf PRIVATE
        PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    )
    target_link_libraries(test_libgpu_perf PRIVATE
        gpu
        GTest::gtest_main
        Threads::Threads
        log4cxx
        graphx_project_options
    )
    add_test(NAME libgpu_perf COMMAND test_libgpu_perf)
endif()
