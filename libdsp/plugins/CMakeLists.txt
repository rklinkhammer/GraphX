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

# CpuSpectrumDftNode Plugins - DSP FFT processor with different packet sizes
add_graphx_plugin(
    TARGET_NAME cpu_spectrum_dft_node_256
    SOURCE_FILE cpu_spectrum_dft_node_256_plugin.cpp
    DESCRIPTION "FFT DSP processor (256 samples/packet)"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME cpu_spectrum_dft_node_512
    SOURCE_FILE cpu_spectrum_dft_node_512_plugin.cpp
    DESCRIPTION "FFT DSP processor (512 samples/packet)"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME cpu_spectrum_dft_node_1024
    SOURCE_FILE cpu_spectrum_dft_node_1024_plugin.cpp
    DESCRIPTION "FFT DSP processor (1024 samples/packet)"
    DEPENDENCIES dsp

)

# DSP IQ H2D transfer plugin - token-preserving host-to-device boundary
add_graphx_plugin(
    TARGET_NAME dsp_iq_h2d_node_256
    SOURCE_FILE dsp_iq_h2d_node_256_plugin.cpp
    DESCRIPTION "DSP IQ host-to-device transfer node (256 samples/packet)"
    DEPENDENCIES dsp
)

# MetalSpectrumDftNode Plugin - real Metal direct DFT spectrum transform
add_graphx_plugin(
    TARGET_NAME metal_spectrum_dft_node_256
    SOURCE_FILE metal_spectrum_dft_node_256_plugin.cpp
    DESCRIPTION "Metal direct DFT spectrum transform (256 samples/packet)"
    DEPENDENCIES dsp
)

# DSP magnitude D2H transfer plugin - token-preserving device-to-host boundary
add_graphx_plugin(
    TARGET_NAME dsp_magnitude_d2h_node_256
    SOURCE_FILE dsp_magnitude_d2h_node_256_plugin.cpp
    DESCRIPTION "DSP magnitude device-to-host transfer node (256 samples/packet)"
    DEPENDENCIES dsp
)

# SpectrumSinkNode Plugin - DSP analyzer sink for FFT magnitude packets
add_graphx_plugin(
    TARGET_NAME spectrum_sink_node_256
    SOURCE_FILE spectrum_sink_node_256_plugin.cpp
    DESCRIPTION "Spectrum sink DSP analyzer (256 samples/packet)"
    DEPENDENCIES dsp
)

# FHSS GraphX node plugins - one dynamically loadable plugin per node.
add_graphx_plugin(
    TARGET_NAME fhss_synthetic_iq_source_node
    SOURCE_FILE fhss_synthetic_iq_source_node_plugin.cpp
    DESCRIPTION "FHSS synthetic IQ source GraphX node"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME fhss_correlator_bank_detector_node
    SOURCE_FILE fhss_correlator_bank_detector_node_plugin.cpp
    DESCRIPTION "FHSS correlator-bank detector GraphX node"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME fhss_downconverter_node
    SOURCE_FILE fhss_downconverter_node_plugin.cpp
    DESCRIPTION "FHSS downconverter GraphX node"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME channelizer_node
    SOURCE_FILE channelizer_node_plugin.cpp
    DESCRIPTION "FHSS frequency-parallel CPU channelizer GraphX node"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME per_channel_pulse_detector_node
    SOURCE_FILE per_channel_pulse_detector_node_plugin.cpp
    DESCRIPTION "FHSS per-channel pulse detector GraphX node"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME fhss_pulse_merge_node
    SOURCE_FILE fhss_pulse_merge_node_plugin.cpp
    DESCRIPTION "FHSS pulse merge GraphX node"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME fhss_pulse_candidate_node
    SOURCE_FILE fhss_pulse_candidate_node_plugin.cpp
    DESCRIPTION "FHSS pulse candidate boundary GraphX node"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME cpsm_branch_metric_node
    SOURCE_FILE cpsm_branch_metric_node_plugin.cpp
    DESCRIPTION "CPSM branch metric GraphX node"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME cpsm_viterbi_decoder_node
    SOURCE_FILE cpsm_viterbi_decoder_node_plugin.cpp
    DESCRIPTION "CPSM Viterbi decoder GraphX node"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME fhss_pulse_word_decoder_node
    SOURCE_FILE fhss_pulse_word_decoder_node_plugin.cpp
    DESCRIPTION "FHSS pulse word decoder GraphX node"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME fhss_preamble_detector_node
    SOURCE_FILE fhss_preamble_detector_node_plugin.cpp
    DESCRIPTION "FHSS preamble detector GraphX node"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME fhss_message_assembler_node
    SOURCE_FILE fhss_message_assembler_node_plugin.cpp
    DESCRIPTION "FHSS message assembler GraphX node"
    DEPENDENCIES dsp
)

add_graphx_plugin(
    TARGET_NAME fhss_message_sink_node
    SOURCE_FILE fhss_message_sink_node_plugin.cpp
    DESCRIPTION "FHSS message sink GraphX node"
    DEPENDENCIES dsp
)
