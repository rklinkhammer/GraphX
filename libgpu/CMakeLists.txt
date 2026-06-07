cmake_minimum_required(VERSION 3.23)
project(libgpu VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CXX_STANDARD EQUAL 26)
    message(FATAL_ERROR "libgpu requires C++26 as the only supported standard")
endif()

# Find dependencies
find_package(nlohmann_json REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(Threads REQUIRED)

# Plugin targets are split between always-available stub plugins used for
# topology/lifecycle validation and future backend-gated runtime plugins.
add_subdirectory(plugins)

# Collect source files
file(GLOB SOURCES CONFIGURE_DEPENDS "src/gpu/*.cpp")

# Always include CPU-safe simulation backends. These are stub/default
# implementations used for contract validation and CPU-only topology tests.
# They are not evidence of a real CUDA or SYCL runtime being exercised.
file(GLOB CUDA_STUB_CAPABILITY_SOURCES CONFIGURE_DEPENDS
    "src/gpu/cuda/capabilities/*.cpp"
    "src/gpu/cuda/capabilities/**/*.cpp")
list(APPEND SOURCES ${CUDA_STUB_CAPABILITY_SOURCES})

file(GLOB SYCL_STUB_CAPABILITY_SOURCES CONFIGURE_DEPENDS
    "src/gpu/sycl/capabilities/*.cpp"
    "src/gpu/sycl/capabilities/**/*.cpp")
list(APPEND SOURCES ${SYCL_STUB_CAPABILITY_SOURCES})

file(GLOB METAL_STUB_CAPABILITY_SOURCES CONFIGURE_DEPENDS
    "src/gpu/metal/capabilities/*.cpp"
    "src/gpu/metal/capabilities/**/*.cpp")
list(APPEND SOURCES ${METAL_STUB_CAPABILITY_SOURCES})

if(GRAPHX_CUDA_GRAPH_NODES_ENABLED)
    file(GLOB CUDA_SOURCES CONFIGURE_DEPENDS "src/gpu/cuda/*.cpp" "src/gpu/cuda/**/*.cpp")
    list(APPEND SOURCES ${CUDA_SOURCES})
endif()

if(GRAPHX_SYCL_GRAPH_NODES_ENABLED)
    file(GLOB SYCL_SOURCES CONFIGURE_DEPENDS "src/gpu/sycl/*.cpp" "src/gpu/sycl/**/*.cpp")
    list(APPEND SOURCES ${SYCL_SOURCES})
endif()

if(GRAPHX_METAL_GRAPH_NODES_ENABLED)
    file(GLOB METAL_SOURCES CONFIGURE_DEPENDS "src/gpu/metal/*.cpp" "src/gpu/metal/**/*.cpp")
    list(FILTER METAL_SOURCES EXCLUDE REGEX "/native/")
    list(APPEND SOURCES ${METAL_SOURCES})
endif()

if(GRAPHX_METAL_NATIVE_RUNTIME_ENABLED)
    file(GLOB METAL_NATIVE_SOURCES CONFIGURE_DEPENDS
        "src/gpu/metal/native/*.cpp")
    list(APPEND SOURCES ${METAL_NATIVE_SOURCES})
endif()

# Only create library if sources found
if(SOURCES)
    add_library(gpu STATIC ${SOURCES})
    target_compile_definitions(gpu PRIVATE BUILD_DIR="${CMAKE_BINARY_DIR}")
    target_compile_definitions(gpu PUBLIC
        GRAPHX_ENABLE_CUDA_GRAPH_NODES=$<BOOL:${GRAPHX_CUDA_GRAPH_NODES_ENABLED}>
        GRAPHX_ENABLE_SYCL_GRAPH_NODES=$<BOOL:${GRAPHX_SYCL_GRAPH_NODES_ENABLED}>
        GRAPHX_ENABLE_METAL_GRAPH_NODES=$<BOOL:${GRAPHX_METAL_GRAPH_NODES_ENABLED}>
        GRAPHX_ENABLE_METAL_NATIVE_RUNTIME=$<BOOL:${GRAPHX_METAL_NATIVE_RUNTIME_ENABLED}>
        GRAPHX_ENABLE_GPU_TEST_HOOKS=$<BOOL:${GRAPHX_GPU_TEST_HOOKS_ENABLED}>
        GRAPHX_MULTI_GPU_TESTS=$<BOOL:${MULTI_GPU_TESTS}>
        GRAPHX_GPU_STUB_BACKENDS=1
    )
    target_include_directories(gpu PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_include_directories(gpu PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(GRAPHX_METAL_NATIVE_RUNTIME_ENABLED AND GRAPHX_METAL_CPP_INCLUDE_DIR_RESOLVED)
        target_include_directories(gpu SYSTEM PRIVATE
            ${GRAPHX_METAL_CPP_INCLUDE_DIR_RESOLVED}
        )
    endif()
    # Link with libgraph (required dependency)
    target_link_libraries(gpu PUBLIC
        graph
        sensor
        nlohmann_json::nlohmann_json
        Eigen3::Eigen
        Threads::Threads
    )

    if(GRAPHX_METAL_NATIVE_RUNTIME_ENABLED)
        target_link_libraries(gpu PUBLIC
            ${GRAPHX_METAL_FRAMEWORK}
            ${GRAPHX_FOUNDATION_FRAMEWORK}
            ${GRAPHX_QUARTZCORE_FRAMEWORK}
        )
    endif()

    # Keep warning/sanitizer options local for install-exported static library.
    if(MSVC)
        target_compile_options(gpu PRIVATE /W4)
    else()
        target_compile_options(gpu PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(ENABLE_TSAN)
        target_compile_options(gpu PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(gpu PRIVATE -fsanitize=thread)
    endif()
else()
    # Create empty interface library if no sources (placeholder for future development)
    add_library(gpu INTERFACE)
    target_include_directories(gpu INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(gpu INTERFACE
        GRAPHX_ENABLE_CUDA_GRAPH_NODES=$<BOOL:${GRAPHX_CUDA_GRAPH_NODES_ENABLED}>
        GRAPHX_ENABLE_SYCL_GRAPH_NODES=$<BOOL:${GRAPHX_SYCL_GRAPH_NODES_ENABLED}>
        GRAPHX_ENABLE_METAL_GRAPH_NODES=$<BOOL:${GRAPHX_METAL_GRAPH_NODES_ENABLED}>
        GRAPHX_ENABLE_METAL_NATIVE_RUNTIME=$<BOOL:${GRAPHX_METAL_NATIVE_RUNTIME_ENABLED}>
        GRAPHX_ENABLE_GPU_TEST_HOOKS=$<BOOL:${GRAPHX_GPU_TEST_HOOKS_ENABLED}>
        GRAPHX_MULTI_GPU_TESTS=$<BOOL:${MULTI_GPU_TESTS}>
        GRAPHX_GPU_STUB_BACKENDS=1
    )
    target_link_libraries(gpu INTERFACE
        graph
        sensor
        nlohmann_json::nlohmann_json
        Eigen3::Eigen
        Threads::Threads
    )
endif()

# Installation
set(GPU_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/gpu")

install(TARGETS gpu EXPORT gpuTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/gpu DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN ".DS_Store" EXCLUDE
)

# Export cmake config
install(EXPORT gpuTargets
    FILE gpuTargets.cmake
    NAMESPACE GraphX::
    DESTINATION ${GPU_CMAKE_INSTALL_DIR}
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gpuConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/gpuConfig.cmake
    INSTALL_DESTINATION ${GPU_CMAKE_INSTALL_DIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/gpuConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/gpuConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/gpuConfigVersion.cmake
    DESTINATION ${GPU_CMAKE_INSTALL_DIR}
)
