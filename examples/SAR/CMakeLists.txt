cmake_minimum_required(VERSION 3.23)

project(graphx_sar_example LANGUAGES CXX)

find_package(log4cxx REQUIRED)

add_executable(sar_example
    src/main.cpp
    src/SyntheticApertureIqSourceNode.cpp
    src/AzimuthTileSplitNode.cpp
)

target_compile_features(sar_example PRIVATE cxx_std_26)
target_link_libraries(sar_example PRIVATE graph log4cxx)

target_include_directories(sar_example PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

install(TARGETS sar_example
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
