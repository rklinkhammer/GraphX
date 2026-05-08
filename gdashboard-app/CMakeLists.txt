cmake_minimum_required(VERSION 3.23)
project(gdashboard-app VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
# Note: In Stage 5, we'll find all real dependencies
find_package(Threads REQUIRED)

# Create executable stub (will be populated with main.cpp in Stage 5)
# For now, just create a stub executable
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/stub_main.cpp" 
"#include <iostream>\nint main() {\n  std::cout << \"Dashboard placeholder\" << std::endl;\n  return 0;\n}\n")

add_executable(gdashboard "${CMAKE_CURRENT_BINARY_DIR}/stub_main.cpp")

target_link_libraries(gdashboard PRIVATE
    Threads::Threads
)

# Link to other projects if available
if(TARGET graph)
    target_link_libraries(gdashboard PRIVATE graph)
endif()
if(TARGET nodes)
    target_link_libraries(gdashboard PRIVATE nodes)
endif()
if(TARGET dashboard)
    target_link_libraries(gdashboard PRIVATE dashboard)
endif()

# Installation
install(TARGETS gdashboard DESTINATION bin)
install(DIRECTORY config/ DESTINATION etc/gdashboard)
