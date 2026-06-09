cmake_minimum_required(VERSION 3.23)

project(graphx_sar_example LANGUAGES CXX)

find_package(log4cxx REQUIRED)

add_library(sar_materialized_payload_runtime SHARED
    src/SarAccelTokenImagePayloadStore.cpp
)

target_compile_features(sar_materialized_payload_runtime PRIVATE cxx_std_26)
target_include_directories(sar_materialized_payload_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(sar_materialized_payload_runtime PUBLIC gpu)

set_target_properties(sar_materialized_payload_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

add_executable(sar_example
    src/main.cpp
    src/AzimuthTileSplitNode.cpp
    src/D2HAsyncAccelNode.cpp
    src/H2DAsyncAccelNode.cpp
    src/ImageTileMergeNode.cpp
    src/RangeCompressionNode.cpp
    src/RangeWindowNode.cpp
    src/SarDiagnosticsSinkNode.cpp
    src/SarMaterializedImageSinkNode.cpp
    src/SarBackprojectionTransformAccelNode.cpp
    src/SyntheticApertureIqSourceNode.cpp
)

add_executable(sar_benchmark
    src/sar_benchmark.cpp
    src/AzimuthTileSplitNode.cpp
    src/D2HAsyncAccelNode.cpp
    src/H2DAsyncAccelNode.cpp
    src/ImageTileMergeNode.cpp
    src/RangeCompressionNode.cpp
    src/RangeWindowNode.cpp
    src/SarDiagnosticsSinkNode.cpp
    src/SarMaterializedImageSinkNode.cpp
    src/SarBackprojectionTransformAccelNode.cpp
    src/SyntheticApertureIqSourceNode.cpp
)

target_compile_features(sar_example PRIVATE cxx_std_26)
target_link_libraries(sar_example PRIVATE graph gpu dsp log4cxx sar_materialized_payload_runtime)
target_compile_definitions(sar_example PRIVATE
    SAR_PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/examples/SAR/plugins"
)

target_compile_features(sar_benchmark PRIVATE cxx_std_26)
target_link_libraries(sar_benchmark PRIVATE graph gpu dsp log4cxx sar_materialized_payload_runtime)
target_compile_definitions(sar_benchmark PRIVATE
    SAR_PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/examples/SAR/plugins"
)

target_include_directories(sar_example PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(sar_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

install(TARGETS sar_example
    sar_benchmark
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
