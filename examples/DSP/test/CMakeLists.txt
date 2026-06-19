find_package(GTest REQUIRED)
find_package(Threads REQUIRED)
find_package(log4cxx REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(test_dsp_example_unit
    test_dsp_spectrum_demo.cpp
)

target_compile_features(test_dsp_example_unit PRIVATE cxx_std_26)
target_compile_definitions(test_dsp_example_unit PRIVATE
    DSP_SPECTRUM_DEMO_EXECUTABLE_PATH="$<TARGET_FILE:dsp_spectrum_demo>"
    DSP_SPECTRUM_CONFIG_PATH="${CMAKE_SOURCE_DIR}/libdsp/config/dsp_sine_fft_spectrum_256.json"
    DSP_PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    GRAPHX_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_dsp_example_unit PRIVATE
    GTest::gtest_main
    Threads::Threads
    log4cxx
    nlohmann_json::nlohmann_json
)
add_dependencies(test_dsp_example_unit
    dsp_spectrum_demo
)
if(TARGET graphx_dynamic_nodes)
    add_dependencies(test_dsp_example_unit graphx_dynamic_nodes)
endif()

add_test(NAME dsp_example_unit COMMAND test_dsp_example_unit)
