find_package(GTest REQUIRED)
find_package(log4cxx REQUIRED)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Collect unit test files
file(GLOB UNIT_TEST_SOURCES "unit/test_*.cpp" "unit/TestGraphTopologies.cpp")

add_subdirectory(plugins)

# Unit tests executable
add_executable(test_libgraph_unit ${UNIT_TEST_SOURCES})
target_compile_definitions(test_libgraph_unit PRIVATE
    PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
)
target_link_libraries(test_libgraph_unit PRIVATE graph GTest::gtest_main Threads::Threads log4cxx)
add_test(NAME libgraph_unit COMMAND test_libgraph_unit)

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
