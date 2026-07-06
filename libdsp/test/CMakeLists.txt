find_package(GTest REQUIRED)
find_package(log4cxx REQUIRED)

file(GLOB LIBDSP_UNIT_TEST_SOURCES CONFIGURE_DEPENDS "unit/test_*.cpp")

if(NOT LIBDSP_UNIT_TEST_SOURCES)
    message(FATAL_ERROR "libdsp_unit requires DSP/FHSS test sources")
endif()

foreach(_source IN LISTS LIBDSP_UNIT_TEST_SOURCES)
    get_filename_component(_name "${_source}" NAME)
    if(NOT _name MATCHES "^test_(dsp_|fhss_)" AND
       NOT _name MATCHES "^test_(metal_spectrum_dft_node|sdr_graph|sine_signal_node_standalone)\\.cpp$")
        message(FATAL_ERROR
            "Non-DSP test source ${_name} does not belong in libdsp/test")
    endif()
endforeach()

add_executable(test_libdsp_unit ${LIBDSP_UNIT_TEST_SOURCES})
target_include_directories(test_libdsp_unit PRIVATE
    ${CMAKE_SOURCE_DIR}/libgraph/test/include
)
target_compile_definitions(test_libdsp_unit PRIVATE
    PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    GRAPHX_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_libdsp_unit PRIVATE
    dsp
    "-Wl,--start-group"
    graph
    gpu
    "-Wl,--end-group"
    GTest::gtest_main
    Threads::Threads
    log4cxx
    graphx_project_options
)
if(TARGET graphx_dynamic_nodes)
    add_dependencies(test_libdsp_unit graphx_dynamic_nodes)
endif()

add_test(NAME libdsp_unit COMMAND test_libdsp_unit)
set_tests_properties(libdsp_unit PROPERTIES LABELS "dsp;fhss;unit;ci-safe")
add_test(NAME libdsp_unit_discovery
    COMMAND ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:test_libdsp_unit>
        -DEXPECTED_SUITE=FHSSGraphXExecutorTest
        -P ${CMAKE_SOURCE_DIR}/cmake/CheckGTestDiscovery.cmake)
set_tests_properties(libdsp_unit_discovery PROPERTIES
    LABELS "dsp;fhss;unit;ownership;discovery;ci-safe")

add_custom_target(test_libdsp
    COMMAND ${CMAKE_CTEST_COMMAND} -R "^libdsp_unit$" --output-on-failure
    COMMENT "Running libdsp tests"
)
