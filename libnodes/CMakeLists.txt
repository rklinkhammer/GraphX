cmake_minimum_required(VERSION 3.23)
project(libnodes VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
# Note: In Stage 3, we'll find all real dependencies
find_package(Threads REQUIRED)

# Collect source files
file(GLOB SOURCES "src/*.cpp" "src/**/*.cpp")

# Create library
add_library(nodes STATIC ${SOURCES})

target_include_directories(nodes PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(nodes PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(nodes PUBLIC
    Threads::Threads
)

# Link to graph once it's built (in parent project)
if(TARGET graph)
    target_link_libraries(nodes PUBLIC graph)
endif()

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
