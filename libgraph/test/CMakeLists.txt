find_package(GTest REQUIRED)
find_package(log4cxx REQUIRED)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Collect unit test files
file(GLOB UNIT_TEST_SOURCES "unit/test_*.cpp" "unit/TestGraphTopologies.cpp")
set(THREADPOOL_EXTENDED_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_thread_pool_benchmarks.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_thread_pool_chaos.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_thread_pool_integration.cpp"
)
list(REMOVE_ITEM UNIT_TEST_SOURCES ${THREADPOOL_EXTENDED_TEST_SOURCES})
list(REMOVE_DUPLICATES UNIT_TEST_SOURCES)

option(ENABLE_THREADPOOL_EXTENDED_TESTS "Build and run ThreadPool benchmark/chaos/scaling tests" OFF)

add_subdirectory(plugins)

# Unit tests executable
add_executable(test_libgraph_unit ${UNIT_TEST_SOURCES})
target_compile_definitions(test_libgraph_unit PRIVATE
    PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
)
target_link_libraries(test_libgraph_unit PRIVATE dsp GTest::gtest_main Threads::Threads log4cxx)
add_dependencies(test_libgraph_unit
    test_node
    source_test_node
    sink_test_node
    interior_test_node
    merge_test_node
    split_test_node
    int_producer
    double_producer
    int_sink
    double_sink
    completion_node
    sine_signal_node_256
    fft_node_256
    spectrum_sink_node_256
)
add_test(NAME libgraph_unit COMMAND test_libgraph_unit)

if(ENABLE_THREADPOOL_EXTENDED_TESTS)
    add_executable(test_threadpool_extended ${THREADPOOL_EXTENDED_TEST_SOURCES})
    target_compile_definitions(test_threadpool_extended PRIVATE
        PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    )
    target_link_libraries(test_threadpool_extended PRIVATE graph GTest::gtest_main Threads::Threads log4cxx)
    add_test(NAME threadpool_extended COMMAND test_threadpool_extended)
    set_tests_properties(threadpool_extended PROPERTIES LABELS "threadpool;extended;benchmark")
endif()

# Collect integration test files
file(GLOB INTEGRATION_TEST_SOURCES "integration/test_*.cpp")

# Integration tests executable (if integration tests exist)
if(INTEGRATION_TEST_SOURCES)
    add_executable(test_libgraph_integration ${INTEGRATION_TEST_SOURCES})
    target_link_libraries(test_libgraph_integration PRIVATE graph GTest::gtest_main Threads::Threads log4cxx)
    add_test(NAME libgraph_integration COMMAND test_libgraph_integration)
endif()

# Test target
add_custom_target(test_libgraph
    COMMAND ${CMAKE_CTEST_COMMAND} -R "libgraph_unit" --output-on-failure
    COMMENT "Running libgraph tests"
)
