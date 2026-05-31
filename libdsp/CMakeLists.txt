cmake_minimum_required(VERSION 3.23)
project(libsensor VERSION 1.0.0 LANGUAGES CXX)

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

# Include directories (for all targets)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_subdirectory(plugins)

# Collect source files
file(GLOB SOURCES "src/dsp/*.cpp")

# Only create library if sources found
if(SOURCES)
    add_library(dsp STATIC ${SOURCES})
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
        sensor
        nlohmann_json::nlohmann_json
        Eigen3::Eigen
        Threads::Threads
    )
    
    # Set compile options
    if(NOT MSVC)
        target_compile_options(dsp PRIVATE -Wall -Wextra -Wpedantic)
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
        sensor
        nlohmann_json::nlohmann_json
        Eigen3::Eigen
        Threads::Threads
    )
endif()

# Installation
install(TARGETS dsp EXPORT dspTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/dsp DESTINATION include)

# Export cmake config
install(EXPORT dspTargets FILE dspConfig.cmake DESTINATION lib/cmake/dsp)
