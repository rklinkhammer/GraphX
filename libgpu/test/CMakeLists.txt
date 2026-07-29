find_package(GTest REQUIRED)
find_package(log4cxx REQUIRED)

file(GLOB GPU_STUB_UNIT_TEST_SOURCES CONFIGURE_DEPENDS "unit/test_*.cpp")
file(GLOB GPU_BACKEND_UNIT_TEST_SOURCES CONFIGURE_DEPENDS "backend_unit/test_*.cpp")
file(GLOB GPU_INTEGRATION_TEST_SOURCES CONFIGURE_DEPENDS "integration/test_*.cpp")
file(GLOB GPU_PERF_TEST_SOURCES CONFIGURE_DEPENDS "perf/test_*.cpp")

# Backend-specific integration round-trip coverage is consolidated into the
# typed suite in unit/test_gpu_topologies.cpp (test_libgpu_stub_unit).
list(FILTER GPU_INTEGRATION_TEST_SOURCES EXCLUDE REGEX "/test_metal_")

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
    if(TARGET graphx_dynamic_nodes)
        add_dependencies(test_libgpu_stub_unit graphx_dynamic_nodes)
    endif()
    add_test(NAME libgpu_stub_unit COMMAND test_libgpu_stub_unit)
    set_tests_properties(libgpu_stub_unit PROPERTIES
        LABELS "gpu;unit;ci-safe;host-any")
endif()

if(GRAPHX_CUDA_GRAPH_NODES_ENABLED AND GPU_BACKEND_UNIT_TEST_SOURCES)
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
    if(TARGET graphx_dynamic_nodes)
        add_dependencies(test_libgpu_backend_unit graphx_dynamic_nodes)
    endif()
    add_test(NAME libgpu_backend_unit COMMAND test_libgpu_backend_unit)
    set_tests_properties(libgpu_backend_unit PROPERTIES
        LABELS "gpu;backend;unit;host-linux")
endif()

if(GRAPHX_METAL_NATIVE_RUNTIME_ENABLED)
    file(GLOB GPU_METAL_RUNTIME_TEST_SOURCES CONFIGURE_DEPENDS "runtime/test_metal_*.cpp")
    if(GPU_METAL_RUNTIME_TEST_SOURCES)
        add_executable(test_libgpu_metal_runtime ${GPU_METAL_RUNTIME_TEST_SOURCES})
        target_include_directories(test_libgpu_metal_runtime PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../include
            ${CMAKE_SOURCE_DIR}/libgraph/test/include
        )
        target_compile_definitions(test_libgpu_metal_runtime PRIVATE
            PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
        )
        target_link_libraries(test_libgpu_metal_runtime PRIVATE
            gpu
            GTest::gtest_main
            Threads::Threads
            log4cxx
            graphx_project_options
        )
        if(TARGET graphx_dynamic_nodes)
            add_dependencies(test_libgpu_metal_runtime graphx_dynamic_nodes)
        endif()
        add_test(NAME libgpu_metal_runtime
            COMMAND ${CMAKE_SOURCE_DIR}/cmake/run_if_host_matches.sh
                Darwin
                libgpu_metal_runtime
                $<TARGET_FILE:test_libgpu_metal_runtime>)
        set_tests_properties(libgpu_metal_runtime PROPERTIES
            LABELS "gpu;metal;runtime;host-macos"
            SKIP_RETURN_CODE 77)
    endif()
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
    if(TARGET graphx_dynamic_nodes)
        add_dependencies(test_libgpu_integration graphx_dynamic_nodes)
    endif()
    add_test(NAME libgpu_integration COMMAND test_libgpu_integration)
    set_tests_properties(libgpu_integration PROPERTIES
        LABELS "gpu;integration;host-any")
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
    if(TARGET graphx_dynamic_nodes)
        add_dependencies(test_libgpu_perf graphx_dynamic_nodes)
    endif()
    add_test(NAME libgpu_perf COMMAND test_libgpu_perf)
    set_tests_properties(libgpu_perf PROPERTIES
        LABELS "gpu;perf;host-any")
endif()
