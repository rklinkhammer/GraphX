find_package(GTest REQUIRED)
find_package(log4cxx REQUIRED)

# Collect unit test files
file(GLOB UNIT_TEST_SOURCES CONFIGURE_DEPENDS "unit/test_*.cpp" "unit/TestGraphTopologies.cpp")
set(THREADPOOL_EXTENDED_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_thread_pool_benchmarks.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_thread_pool_chaos.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_thread_pool_integration.cpp"
)
list(REMOVE_ITEM UNIT_TEST_SOURCES ${THREADPOOL_EXTENDED_TEST_SOURCES})
list(REMOVE_DUPLICATES UNIT_TEST_SOURCES)

foreach(_source IN LISTS UNIT_TEST_SOURCES)
    get_filename_component(_name "${_source}" NAME)
    if(_name MATCHES "^test_(dsp_|fhss_)" OR
       _name MATCHES "^test_(metal_spectrum_dft_node|sdr_graph|sine_signal_node_standalone)\\.cpp$")
        message(FATAL_ERROR
            "DSP/FHSS test source ${_name} must be owned by libdsp/test")
    endif()
endforeach()

option(ENABLE_THREADPOOL_EXTENDED_TESTS "Build and run ThreadPool benchmark/chaos/scaling tests" OFF)

add_subdirectory(plugins)

set(GRAPHX_LINK_GROUP_BEGIN "")
set(GRAPHX_LINK_GROUP_END "")
if(UNIX AND NOT APPLE)
    set(GRAPHX_LINK_GROUP_BEGIN "-Wl,--start-group")
    set(GRAPHX_LINK_GROUP_END "-Wl,--end-group")
endif()

# Unit tests executable
add_executable(test_libgraph_unit ${UNIT_TEST_SOURCES})

set(SELF_CONTAINED_HEADERS
    Lifecycle.hpp PortSpec.hpp PortTypes.hpp InputFunction.hpp
    OutputFunction.hpp TransferFunction.hpp MergeFunction.hpp ThreadMetrics.hpp)
foreach(_header IN LISTS SELF_CONTAINED_HEADERS)
    string(REPLACE ".hpp" "" _stem "${_header}")
    string(TOLOWER "${_stem}" _stem)
    set(_target "header_self_containment_${_stem}")
    add_library(${_target} OBJECT header_self_containment.cpp)
    target_include_directories(${_target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include)
    if(MSVC)
        target_compile_options(${_target} PRIVATE "/FIgraph/${_header}")
    else()
        target_compile_options(${_target} PRIVATE
            "-include" "graph/${_header}")
    endif()
    target_link_libraries(${_target} PRIVATE
        graphx_project_options graph log4cxx)
    add_dependencies(test_libgraph_unit ${_target})
endforeach()
target_include_directories(test_libgraph_unit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(test_libgraph_unit PRIVATE
    PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    GRAPHX_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_libgraph_unit PRIVATE
    ${GRAPHX_LINK_GROUP_BEGIN}
    graph
    gpu
    ${GRAPHX_LINK_GROUP_END}
    GTest::gtest_main
    Threads::Threads
    log4cxx
)
target_link_libraries(test_libgraph_unit PRIVATE graphx_project_options)
if(TARGET graphx_dynamic_nodes)
    add_dependencies(test_libgraph_unit graphx_dynamic_nodes)
endif()
add_test(NAME libgraph_unit COMMAND test_libgraph_unit)
set_tests_properties(libgraph_unit PROPERTIES LABELS "core;unit;ci-safe;host-any")
add_test(NAME libgraph_unit_discovery
    COMMAND ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:test_libgraph_unit>
        -DEXPECTED_SUITE=ActiveQueueTest
        -P ${CMAKE_SOURCE_DIR}/cmake/CheckGTestDiscovery.cmake)
set_tests_properties(libgraph_unit_discovery PROPERTIES
    LABELS "core;unit;ownership;discovery;ci-safe;host-any")

if(ENABLE_THREADPOOL_EXTENDED_TESTS)
    add_executable(test_threadpool_extended ${THREADPOOL_EXTENDED_TEST_SOURCES})
    target_include_directories(test_threadpool_extended PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_definitions(test_threadpool_extended PRIVATE
        PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    )
    target_link_libraries(test_threadpool_extended PRIVATE
        ${GRAPHX_LINK_GROUP_BEGIN}
        graph
        gpu
        ${GRAPHX_LINK_GROUP_END}
        GTest::gtest_main
        Threads::Threads
        log4cxx
    )
    target_link_libraries(test_threadpool_extended PRIVATE graphx_project_options)
    add_test(NAME threadpool_extended COMMAND test_threadpool_extended)
    set_tests_properties(threadpool_extended PROPERTIES LABELS "threadpool;extended;benchmark;host-any")
endif()

# Collect integration test files
file(GLOB INTEGRATION_TEST_SOURCES CONFIGURE_DEPENDS "integration/test_*.cpp")

# Integration tests executable (if integration tests exist)
if(INTEGRATION_TEST_SOURCES)
    add_executable(test_libgraph_integration ${INTEGRATION_TEST_SOURCES})
    target_include_directories(test_libgraph_integration PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(test_libgraph_integration PRIVATE
        ${GRAPHX_LINK_GROUP_BEGIN}
        graph
        gpu
        ${GRAPHX_LINK_GROUP_END}
        GTest::gtest_main
        Threads::Threads
        log4cxx
    )
    target_link_libraries(test_libgraph_integration PRIVATE graphx_project_options)
    if(TARGET graphx_dynamic_nodes)
        add_dependencies(test_libgraph_integration graphx_dynamic_nodes)
    endif()
    add_test(NAME libgraph_integration COMMAND test_libgraph_integration)
    set_tests_properties(libgraph_integration PROPERTIES
        LABELS "core;integration;ci-safe;host-any")
endif()

# Test target
add_custom_target(test_libgraph
    COMMAND ${CMAKE_CTEST_COMMAND} -R "libgraph_unit" --output-on-failure
    COMMENT "Running libgraph tests"
)
