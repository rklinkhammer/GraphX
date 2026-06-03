cmake_minimum_required(VERSION 3.23)
project(libgraph VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CXX_STANDARD EQUAL 26)
    message(FATAL_ERROR "libgraph requires C++26 as the only supported standard")
endif()

# Find dependencies
find_package(nlohmann_json REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(Threads REQUIRED)

# Collect source files
file(GLOB SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/**/*.cpp")

# Create library
add_library(graph STATIC ${SOURCES})
target_compile_definitions(graph PUBLIC
    GRAPHX_ENABLE_CUDA_GRAPH_NODES=$<BOOL:${GRAPHX_CUDA_GRAPH_NODES_ENABLED}>
    GRAPHX_ENABLE_SYCL_GRAPH_NODES=$<BOOL:${GRAPHX_SYCL_GRAPH_NODES_ENABLED}>
    GRAPHX_MULTI_GPU_TESTS=$<BOOL:${MULTI_GPU_TESTS}>
    GRAPHX_GPU_STUB_BACKENDS=1
)
if(GRAPHX_ENABLE_MODULE_PILOT)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        message(WARNING
            "GRAPHX_ENABLE_MODULE_PILOT is ON, but AppleClang does not provide "
            "the required module dependency scanning support for this target. "
            "Skipping module pilot sources.")
    else()
        set(CMAKE_CXX_SCAN_FOR_MODULES ON)
        target_sources(graph
            PUBLIC
                FILE_SET CXX_MODULES FILES
                    modules/graph.port_metadata.ixx
        )
    endif()
endif()
target_compile_definitions(graph PRIVATE BUILD_DIR="${CMAKE_BINARY_DIR}")
target_include_directories(graph PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(graph PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(EXISTS "${CMAKE_SOURCE_DIR}/libgpu/include")
    target_include_directories(graph PRIVATE
        ${CMAKE_SOURCE_DIR}/libgpu/include
    )
endif()

target_link_libraries(graph PUBLIC
    nlohmann_json::nlohmann_json
    Eigen3::Eigen
    Threads::Threads
)

# Keep warning/sanitizer options local for install-exported static library.
if(MSVC)
    target_compile_options(graph PRIVATE /W4)
else()
    target_compile_options(graph PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ENABLE_TSAN)
    target_compile_options(graph PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(graph PRIVATE -fsanitize=thread)
endif()

# Installation
set(GRAPH_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/graph")

install(TARGETS graph EXPORT graphTargets
    FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_LIBDIR}/modules/graph
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN ".DS_Store" EXCLUDE
)

# Export cmake config
install(EXPORT graphTargets
    FILE graphTargets.cmake
    NAMESPACE GraphX::
    DESTINATION ${GRAPH_CMAKE_INSTALL_DIR}
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/graphConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/graphConfig.cmake
    INSTALL_DESTINATION ${GRAPH_CMAKE_INSTALL_DIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/graphConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/graphConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/graphConfigVersion.cmake
    DESTINATION ${GRAPH_CMAKE_INSTALL_DIR}
)
