cmake_minimum_required(VERSION 3.23)

# Include the plugin template
include(${CMAKE_SOURCE_DIR}/cmake/PluginTemplate.cmake)

# ============================================================================
# Plugin Directory Setup
# ============================================================================

add_graphx_plugins_directory()

# ============================================================================
# Define Plugins
# ============================================================================

# SineSignalNode Plugins - DSP sine generator with different packet sizes
add_graphx_plugin(
    TARGET_NAME sine_signal_node_256
    SOURCE_FILE sine_signal_node_256_plugin.cpp
    DESCRIPTION "Sine wave DSP node (256 samples/packet)"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME sine_signal_node_512
    SOURCE_FILE sine_signal_node_512_plugin.cpp
    DESCRIPTION "Sine wave DSP node (512 samples/packet)"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME sine_signal_node_1024
    SOURCE_FILE sine_signal_node_1024_plugin.cpp
    DESCRIPTION "Sine wave DSP node (1024 samples/packet)"
    DEPENDENCIES dsp
)

# FFTNode Plugins - DSP FFT processor with different packet sizes
add_graphx_plugin(
    TARGET_NAME fft_node_256
    SOURCE_FILE fft_node_256_plugin.cpp
    DESCRIPTION "FFT DSP processor (256 samples/packet)"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME fft_node_512
    SOURCE_FILE fft_node_512_plugin.cpp
    DESCRIPTION "FFT DSP processor (512 samples/packet)"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME fft_node_1024
    SOURCE_FILE fft_node_1024_plugin.cpp
    DESCRIPTION "FFT DSP processor (1024 samples/packet)"
    DEPENDENCIES dsp

)

# SpectrumSinkNode Plugin - DSP analyzer sink for FFT magnitude packets
add_graphx_plugin(
    TARGET_NAME spectrum_sink_node_256
    SOURCE_FILE spectrum_sink_node_256_plugin.cpp
    DESCRIPTION "Spectrum sink DSP analyzer (256 samples/packet)"
    DEPENDENCIES dsp
)
