cmake_minimum_required(VERSION 3.23)
project(graphx_package_smoke_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(GraphX CONFIG REQUIRED)

if(NOT TARGET GraphX::graph)
    message(FATAL_ERROR "Expected exported target GraphX::graph")
endif()

if(NOT TARGET GraphX::sensor)
    message(FATAL_ERROR "Expected exported target GraphX::sensor")
endif()

if(NOT TARGET GraphX::dsp)
    message(FATAL_ERROR "Expected exported target GraphX::dsp")
endif()

add_executable(package_smoke_consumer main.cpp)
target_link_libraries(package_smoke_consumer PRIVATE
    GraphX::dsp
)
