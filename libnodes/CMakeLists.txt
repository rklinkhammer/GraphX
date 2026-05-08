cmake_minimum_required(VERSION 3.23)
project(libnodes VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(graph REQUIRED)
find_package(log4cxx REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(Threads REQUIRED)

# Create library as stub (will be populated in Stage 3)
add_library(nodes STATIC)

# Placeholder source
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/placeholder.cpp" "// Placeholder\n")
target_sources(nodes PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/placeholder.cpp")

target_include_directories(nodes PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(nodes PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(nodes PUBLIC
    graph::graph
    log4cxx::log4cxx
    Eigen3::Eigen
    Threads::Threads
)

# Set compile options
if(NOT MSVC)
    target_compile_options(nodes PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Installation
install(TARGETS nodes EXPORT nodesTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/nodes DESTINATION include)

# Export cmake config
install(EXPORT nodesTargets FILE nodesConfig.cmake DESTINATION lib/cmake/nodes)
