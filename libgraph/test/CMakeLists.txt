find_package(GTest REQUIRED)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

# Placeholder test file (will be populated with actual tests in Stage 4)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/test_placeholder.cpp"
"#include <gtest/gtest.h>\n\nTEST(PlaceholderTest, Dummy) {\n  EXPECT_TRUE(true);\n}\n")

# Unit tests executable
add_executable(test_libgraph_unit "${CMAKE_CURRENT_BINARY_DIR}/test_placeholder.cpp")
target_link_libraries(test_libgraph_unit PRIVATE graph GTest::gtest_main)
add_test(NAME libgraph_unit COMMAND test_libgraph_unit)

# Test target
add_custom_target(test_libgraph
    COMMAND ${CMAKE_CTEST_COMMAND} -R "libgraph_unit" --output-on-failure
    COMMENT "Running libgraph tests"
)
