find_package(GTest REQUIRED)

file(GLOB ACCELGRAPH_UNIT_TEST_SOURCES CONFIGURE_DEPENDS "unit/test_*.cpp")

if(ACCELGRAPH_UNIT_TEST_SOURCES)
    add_executable(test_libaccelgraph_smoke ${ACCELGRAPH_UNIT_TEST_SOURCES})
    target_compile_definitions(test_libaccelgraph_smoke PRIVATE
        PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    )
    target_include_directories(test_libaccelgraph_smoke PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
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
    )
    add_test(NAME libaccelgraph_smoke COMMAND test_libaccelgraph_smoke)
    set_tests_properties(libaccelgraph_smoke PROPERTIES
        LABELS "accelgraph;smoke;unit;host-any")
endif()