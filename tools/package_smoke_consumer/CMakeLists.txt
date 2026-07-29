cmake_minimum_required(VERSION 3.23)
project(graphx_package_smoke_consumer LANGUAGES CXX)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-std=gnu++26)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    add_compile_options(-std=c++2c)
else()
    message(FATAL_ERROR "Package smoke requires a compiler with a known C++26 flag")
endif()

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
