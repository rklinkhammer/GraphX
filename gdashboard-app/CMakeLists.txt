cmake_minimum_required(VERSION 3.23)
project(gdashboard-app VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find all dependencies
find_package(graph REQUIRED)
find_package(nodes REQUIRED)
find_package(dashboard REQUIRED)
find_package(log4cxx REQUIRED)
find_package(Threads REQUIRED)

# Create executable stub (will be populated with main.cpp in Stage 5)
# For now, just create a stub executable
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/stub_main.cpp" 
"#include <iostream>\nint main() {\n  std::cout << \"Dashboard placeholder\" << std::endl;\n  return 0;\n}\n")

add_executable(gdashboard "${CMAKE_CURRENT_BINARY_DIR}/stub_main.cpp")

target_link_libraries(gdashboard PRIVATE
    graph::graph
    nodes::nodes
    dashboard::dashboard
    log4cxx::log4cxx
    Threads::Threads
)

# Installation
install(TARGETS gdashboard DESTINATION bin)
install(DIRECTORY config/ DESTINATION etc/gdashboard)
