cmake_minimum_required(VERSION 3.23)
project(libdsp VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CXX_STANDARD EQUAL 26)
    message(FATAL_ERROR "libdsp requires C++26 as the only supported standard")
endif()

# Find dependencies
find_package(nlohmann_json REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(Threads REQUIRED)

add_subdirectory(plugins)

# Collect source files
file(GLOB SOURCES CONFIGURE_DEPENDS "src/dsp/*.cpp")

# Only create library if sources found
if(SOURCES)
    add_library(dsp STATIC ${SOURCES})
    # dsp is linked into shared plugin modules, so compile objects as PIC.
    set_target_properties(dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(dsp PRIVATE BUILD_DIR="${CMAKE_BINARY_DIR}")
    target_include_directories(dsp PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_include_directories(dsp PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    # Link with libgraph (required dependency)
    target_link_libraries(dsp PUBLIC
        graph
        gpu
        sensor
        nlohmann_json::nlohmann_json
        Eigen3::Eigen
        Threads::Threads
    )

    # Keep warning/sanitizer options local for install-exported static library.
    if(MSVC)
        target_compile_options(dsp PRIVATE /W4)
    else()
        target_compile_options(dsp PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(ENABLE_TSAN)
        target_compile_options(dsp PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(dsp PRIVATE -fsanitize=thread)
    endif()
else()
    # Create empty interface library if no sources (placeholder for future development)
    add_library(dsp INTERFACE)
    target_include_directories(dsp INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(dsp INTERFACE
        graph
        gpu
        sensor
        nlohmann_json::nlohmann_json
        Eigen3::Eigen
        Threads::Threads
    )
endif()

# Installation
set(DSP_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/dsp")

install(TARGETS dsp EXPORT dspTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/dsp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN ".DS_Store" EXCLUDE
)

# Export cmake config
install(EXPORT dspTargets
    FILE dspTargets.cmake
    NAMESPACE GraphX::
    DESTINATION ${DSP_CMAKE_INSTALL_DIR}
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/dspConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/dspConfig.cmake
    INSTALL_DESTINATION ${DSP_CMAKE_INSTALL_DIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/dspConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/dspConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/dspConfigVersion.cmake
    DESTINATION ${DSP_CMAKE_INSTALL_DIR}
)
