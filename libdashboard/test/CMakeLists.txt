find_package(GTest REQUIRED)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

# Placeholder test file
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/test_placeholder.cpp"
"#include <gtest/gtest.h>\n\nTEST(PlaceholderTest, Dummy) {\n  EXPECT_TRUE(true);\n}\n")

# Unit tests executable
add_executable(test_libdashboard_unit "${CMAKE_CURRENT_BINARY_DIR}/test_placeholder.cpp")
target_link_libraries(test_libdashboard_unit PRIVATE dashboard graph GTest::gtest_main)
add_test(NAME libdashboard_unit COMMAND test_libdashboard_unit)

# Test target
add_custom_target(test_libdashboard
    COMMAND ${CMAKE_CTEST_COMMAND} -R "libdashboard_unit" --output-on-failure
    COMMENT "Running libdashboard tests"
)
