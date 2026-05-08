cmake_minimum_required(VERSION 3.23)
project(libdashboard VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(graph REQUIRED)
find_package(FTXUI REQUIRED)
find_package(log4cxx REQUIRED)
find_package(ncurses REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

# Create library as stub (will be populated in Stage 3)
add_library(dashboard STATIC)

# Placeholder source
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/placeholder.cpp" "// Placeholder\n")
target_sources(dashboard PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/placeholder.cpp")

target_include_directories(dashboard PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(dashboard PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dashboard PUBLIC
    graph::graph
    FTXUI::screen
    FTXUI::component
    log4cxx::log4cxx
    ncurses
    nlohmann_json::nlohmann_json
    Threads::Threads
)

# Set compile options
if(NOT MSVC)
    target_compile_options(dashboard PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Installation
install(TARGETS dashboard EXPORT dashboardTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/dashboard DESTINATION include)

# Export cmake config
install(EXPORT dashboardTargets FILE dashboardConfig.cmake DESTINATION lib/cmake/dashboard)
