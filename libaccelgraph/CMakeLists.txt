cmake_minimum_required(VERSION 3.23)
project(libaccelgraph VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(_accelgraph_enable_metal_default OFF)
if(DEFINED GRAPHX_METAL_NATIVE_RUNTIME_ENABLED AND GRAPHX_METAL_NATIVE_RUNTIME_ENABLED)
    set(_accelgraph_enable_metal_default ON)
endif()

option(ACCELGRAPH_ENABLE_METAL "Enable libaccelgraph native Metal backend" ${_accelgraph_enable_metal_default})
option(ACCELGRAPH_ENABLE_CUDA "Enable libaccelgraph native CUDA backend" OFF)

set(ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE OFF)
set(ACCELGRAPH_CUDA_RUNTIME_HEADER_AVAILABLE OFF)
if(DEFINED GRAPHX_HAS_CUDA_TOOLKIT AND GRAPHX_HAS_CUDA_TOOLKIT)
    set(ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE ON)
endif()

set(ACCELGRAPH_CUDA_INCLUDE_DIR "")
set(ACCELGRAPH_CUDART_LIBRARY "")
if(ACCELGRAPH_ENABLE_CUDA)
    if(TARGET CUDA::cudart)
        set(ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE ON)
        if(CUDAToolkit_BIN_DIR)
            cmake_path(GET CUDAToolkit_BIN_DIR PARENT_PATH _accelgraph_cuda_root)
            list(APPEND _accelgraph_cuda_include_hints
                "${_accelgraph_cuda_root}/include"
                "${_accelgraph_cuda_root}/targets/sbsa-linux/include"
                "${_accelgraph_cuda_root}/targets/aarch64-linux/include")
        endif()
        find_path(ACCELGRAPH_CUDA_INCLUDE_DIR
            NAMES cuda_runtime_api.h
            HINTS ${_accelgraph_cuda_include_hints}
                /usr/local/cuda/include
                /usr/local/cuda-13.2/include
        )

        set(ACCELGRAPH_CUDART_LIBRARY CUDA::cudart)
    else()
        find_path(ACCELGRAPH_CUDA_INCLUDE_DIR
            NAMES cuda_runtime_api.h
            HINTS
                /usr/local/cuda/include
                /usr/local/cuda-13.2/include
        )
        find_library(ACCELGRAPH_CUDART_LIBRARY
            NAMES cudart
            HINTS
                /usr/local/cuda/lib64
                /usr/local/cuda-13.2/lib64
                /usr/local/cuda/targets/sbsa-linux/lib
                /usr/local/cuda-13.2/targets/sbsa-linux/lib
        )
    endif()

    if(ACCELGRAPH_CUDA_INCLUDE_DIR AND ACCELGRAPH_CUDART_LIBRARY)
        set(ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE ON)
    endif()

    if(ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE)
        if(TARGET CUDA::cudart)
            set(ACCELGRAPH_CUDA_RUNTIME_HEADER_AVAILABLE ON)
        elseif(ACCELGRAPH_CUDA_INCLUDE_DIR AND
               EXISTS "${ACCELGRAPH_CUDA_INCLUDE_DIR}/cuda_runtime_api.h")
            set(ACCELGRAPH_CUDA_RUNTIME_HEADER_AVAILABLE ON)
        endif()
    endif()
endif()

set(ACCELGRAPH_METAL_HEADERS_AVAILABLE OFF)
if(ACCELGRAPH_ENABLE_METAL AND APPLE)
    if(DEFINED GRAPHX_METAL_CPP_INCLUDE_DIR_RESOLVED AND GRAPHX_METAL_CPP_INCLUDE_DIR_RESOLVED)
        set(ACCELGRAPH_METAL_HEADERS_AVAILABLE ON)
    endif()
endif()

file(GLOB SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/**/*.cpp")

add_library(accelgraph STATIC ${SOURCES})
target_compile_definitions(accelgraph PUBLIC
    ACCELGRAPH_ENABLE_METAL=$<BOOL:${ACCELGRAPH_ENABLE_METAL}>
    ACCELGRAPH_ENABLE_CUDA=$<BOOL:${ACCELGRAPH_ENABLE_CUDA}>
    ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE=$<BOOL:${ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE}>
    ACCELGRAPH_CUDA_RUNTIME_HEADER_AVAILABLE=$<BOOL:${ACCELGRAPH_CUDA_RUNTIME_HEADER_AVAILABLE}>
    ACCELGRAPH_METAL_HEADERS_AVAILABLE=$<BOOL:${ACCELGRAPH_METAL_HEADERS_AVAILABLE}>
)
target_include_directories(accelgraph PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
if(ACCELGRAPH_METAL_HEADERS_AVAILABLE)
    target_include_directories(accelgraph PRIVATE
        ${GRAPHX_METAL_CPP_INCLUDE_DIR_RESOLVED}
    )
endif()
if(ACCELGRAPH_ENABLE_CUDA AND ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE)
    target_include_directories(accelgraph PRIVATE
        ${ACCELGRAPH_CUDA_INCLUDE_DIR}
    )
endif()
target_link_libraries(accelgraph PUBLIC
    graph
)
if(ACCELGRAPH_METAL_HEADERS_AVAILABLE)
    target_link_libraries(accelgraph PRIVATE
        ${GRAPHX_METAL_FRAMEWORK}
        ${GRAPHX_FOUNDATION_FRAMEWORK}
        ${GRAPHX_QUARTZCORE_FRAMEWORK}
    )
endif()
if(ACCELGRAPH_ENABLE_CUDA AND ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE)
    target_link_libraries(accelgraph PRIVATE
        ${ACCELGRAPH_CUDART_LIBRARY}
    )
endif()

if(MSVC)
    target_compile_options(accelgraph PRIVATE /W4)
else()
    target_compile_options(accelgraph PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ENABLE_TSAN)
    target_compile_options(accelgraph PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(accelgraph PRIVATE -fsanitize=thread)
endif()

add_subdirectory(plugins)

set(ACCELGRAPH_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/accelgraph")

install(TARGETS accelgraph EXPORT accelgraphTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/accelgraph DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN ".DS_Store" EXCLUDE
)

install(EXPORT accelgraphTargets
    FILE accelgraphTargets.cmake
    NAMESPACE GraphX::
    DESTINATION ${ACCELGRAPH_CMAKE_INSTALL_DIR}
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/accelgraphConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/accelgraphConfig.cmake
    INSTALL_DESTINATION ${ACCELGRAPH_CMAKE_INSTALL_DIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/accelgraphConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/accelgraphConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/accelgraphConfigVersion.cmake
    DESTINATION ${ACCELGRAPH_CMAKE_INSTALL_DIR}
)