cmake_minimum_required(VERSION 3.23)

project(graphx_dsp_example LANGUAGES CXX)

find_package(log4cxx REQUIRED)

add_executable(dsp_spectrum_demo
    src/main.cpp
)
set_target_properties(dsp_spectrum_demo PROPERTIES
    OUTPUT_NAME "graphx-dsp-spectrum-demo"
)

target_compile_features(dsp_spectrum_demo PRIVATE cxx_std_26)
# The graph runtime currently resolves its GPU capability bootstrap symbol from libgpu;
# the DSP demo topology itself remains CPU-only.
target_link_libraries(dsp_spectrum_demo PRIVATE graph gpu dsp log4cxx)
target_compile_definitions(dsp_spectrum_demo PRIVATE
    GRAPHX_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    DSP_PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    DSP_SPECTRUM_CONFIG_PATH="${CMAKE_SOURCE_DIR}/libdsp/config/dsp_sine_fft_spectrum_256.json"
    DSP_METAL_SPECTRUM_CONFIG_PATH="${CMAKE_SOURCE_DIR}/libdsp/config/dsp_sine_metal_dft_spectrum_256.json"
)
if(TARGET graphx_dynamic_nodes)
    add_dependencies(dsp_spectrum_demo graphx_dynamic_nodes)
endif()

add_executable(dsp_fhss_demo
    src/fhss_demo.cpp
)
set_target_properties(dsp_fhss_demo PROPERTIES
    OUTPUT_NAME "graphx-dsp-fhss-demo"
)

target_compile_features(dsp_fhss_demo PRIVATE cxx_std_26)
# The FHSS fixture graph is CPU-only, but the graph runtime resolves its
# accelerator token bootstrap symbols from libgpu.
target_link_libraries(dsp_fhss_demo PRIVATE graph gpu dsp log4cxx)
target_compile_definitions(dsp_fhss_demo PRIVATE
    GRAPHX_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    DSP_PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/plugins"
    DSP_FHSS_CHANNELIZED_CONFIG_PATH="${CMAKE_SOURCE_DIR}/libdsp/config/fhss_cpsm_channelized_fixture_500msps.json"
    DSP_FHSS_REFERENCE_CONFIG_PATH="${CMAKE_SOURCE_DIR}/libdsp/config/fhss_cpsm_fixture_500msps.json"
)
if(TARGET graphx_dynamic_nodes)
    add_dependencies(dsp_fhss_demo graphx_dynamic_nodes)
endif()

install(TARGETS dsp_spectrum_demo
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS dsp_fhss_demo
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
