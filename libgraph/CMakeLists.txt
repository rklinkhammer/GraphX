cmake_minimum_required(VERSION 3.23)
project(libgraph VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(nlohmann_json REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(Threads REQUIRED)

# Create library as interface (stub - will be populated in Stage 3)
add_library(graph STATIC)

# Placeholder source (will be replaced with actual sources in Stage 3)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/placeholder.cpp" "// Placeholder\n")
target_sources(graph PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/placeholder.cpp")

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
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/graph DESTINATION include)

# Export cmake config
install(EXPORT graphTargets FILE graphConfig.cmake DESTINATION lib/cmake/graph)
