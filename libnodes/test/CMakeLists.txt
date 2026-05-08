find_package(GTest REQUIRED)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

# Placeholder test file
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/test_placeholder.cpp"
"#include <gtest/gtest.h>\n\nTEST(PlaceholderTest, Dummy) {\n  EXPECT_TRUE(true);\n}\n")

# Unit tests executable
add_executable(test_libnodes_unit "${CMAKE_CURRENT_BINARY_DIR}/test_placeholder.cpp")
target_link_libraries(test_libnodes_unit PRIVATE nodes graph GTest::gtest_main)
add_test(NAME libnodes_unit COMMAND test_libnodes_unit)

# Test target
add_custom_target(test_libnodes
    COMMAND ${CMAKE_CTEST_COMMAND} -R "libnodes_unit" --output-on-failure
    COMMENT "Running libnodes tests"
)
