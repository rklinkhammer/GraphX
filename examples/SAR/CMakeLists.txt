cmake_minimum_required(VERSION 3.23)

project(graphx_sar_example LANGUAGES CXX)

find_package(log4cxx REQUIRED)
find_package(HDF5 COMPONENTS C QUIET)

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

add_executable(graphx_gotcha_to_crsd
    src/graphx_gotcha_to_crsd.cpp
)
set_target_properties(graphx_gotcha_to_crsd PROPERTIES
    OUTPUT_NAME "graphx-gotcha-to-crsd"
)

target_compile_features(sar_example PRIVATE cxx_std_26)
target_link_libraries(sar_example PRIVATE graph gpu dsp log4cxx)
target_compile_definitions(sar_example PRIVATE
    GRAPHX_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    SAR_PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/examples/SAR/plugins"
)
if(TARGET graphx_dynamic_nodes)
    add_dependencies(sar_example graphx_dynamic_nodes)
endif()

target_compile_features(sar_benchmark PRIVATE cxx_std_26)
target_link_libraries(sar_benchmark PRIVATE graph gpu dsp log4cxx)
target_compile_definitions(sar_benchmark PRIVATE
    GRAPHX_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    SAR_PLUGIN_OUTPUT_DIRECTORY="${CMAKE_BINARY_DIR}/examples/SAR/plugins"
)
if(TARGET graphx_dynamic_nodes)
    add_dependencies(sar_benchmark graphx_dynamic_nodes)
endif()

target_compile_features(graphx_gotcha_to_crsd PRIVATE cxx_std_26)
target_link_libraries(graphx_gotcha_to_crsd PRIVATE graph gpu dsp log4cxx)
target_compile_definitions(graphx_gotcha_to_crsd PRIVATE
    GRAPHX_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
if(HDF5_FOUND)
    target_compile_definitions(graphx_gotcha_to_crsd PRIVATE GRAPHX_SAR_HAS_HDF5=1)
    target_include_directories(graphx_gotcha_to_crsd PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(graphx_gotcha_to_crsd PRIVATE ${HDF5_LIBRARIES})
else()
    target_compile_definitions(graphx_gotcha_to_crsd PRIVATE GRAPHX_SAR_HAS_HDF5=0)
endif()

target_include_directories(sar_example PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(sar_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(graphx_gotcha_to_crsd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

install(TARGETS sar_example
    sar_benchmark
    graphx_gotcha_to_crsd
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
