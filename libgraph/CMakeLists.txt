cmake_minimum_required(VERSION 3.23)
project(libgraph VERSION 1.0.0 LANGUAGES CXX)

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
file(GLOB SOURCES "src/*.cpp" "src/**/*.cpp")

# Create library
add_library(graph STATIC ${SOURCES})
if(GRAPHX_ENABLE_MODULE_PILOT)
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
    target_sources(graph
        PUBLIC
            FILE_SET CXX_MODULES FILES
                modules/graph.port_metadata.ixx
    )
endif()
target_compile_definitions(graph PRIVATE BUILD_DIR="${CMAKE_BINARY_DIR}")
target_include_directories(graph PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(graph PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(graph PUBLIC
    nlohmann_json::nlohmann_json
    Eigen3::Eigen
    Threads::Threads
)

# Set compile options
if(NOT MSVC)
    target_compile_options(graph PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Installation
install(TARGETS graph EXPORT graphTargets
    FILE_SET CXX_MODULES DESTINATION lib/modules/graph
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/graph DESTINATION include)

# Export cmake config
install(EXPORT graphTargets FILE graphConfig.cmake DESTINATION lib/cmake/graph)
