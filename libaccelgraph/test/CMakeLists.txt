find_package(GTest REQUIRED)

file(GLOB ACCELGRAPH_UNIT_TEST_SOURCES CONFIGURE_DEPENDS "unit/test_*.cpp")

if(ACCELGRAPH_UNIT_TEST_SOURCES)
    add_executable(test_libaccelgraph_smoke ${ACCELGRAPH_UNIT_TEST_SOURCES})
    target_include_directories(test_libaccelgraph_smoke PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
    target_link_libraries(test_libaccelgraph_smoke PRIVATE
        accelgraph
        GTest::gtest_main
        graphx_project_options
    )
    add_test(NAME libaccelgraph_smoke COMMAND test_libaccelgraph_smoke)
    set_tests_properties(libaccelgraph_smoke PROPERTIES
        LABELS "accelgraph;smoke;unit;host-any")
endif()