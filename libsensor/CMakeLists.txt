cmake_minimum_required(VERSION 3.23)
project(libsensor VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Find dependencies
find_package(nlohmann_json REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(Threads REQUIRED)

# Collect source files
file(GLOB SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/**/*.cpp")

# Create library
add_library(sensor STATIC ${SOURCES})
target_compile_definitions(sensor PRIVATE BUILD_DIR="${CMAKE_BINARY_DIR}")
target_include_directories(sensor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(sensor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link with libgraph (required dependency)
target_link_libraries(sensor PUBLIC
    graph
    nlohmann_json::nlohmann_json
    Eigen3::Eigen
    Threads::Threads
)

# Keep warning/sanitizer options local for install-exported static library.
if(MSVC)
    target_compile_options(sensor PRIVATE /W4)
else()
    target_compile_options(sensor PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ENABLE_TSAN)
    target_compile_options(sensor PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(sensor PRIVATE -fsanitize=thread)
endif()
if(ENABLE_ASAN_UBSAN)
    target_compile_options(sensor PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(sensor PUBLIC -fsanitize=address,undefined)
endif()

# Installation
set(SENSOR_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/sensor")

install(TARGETS sensor EXPORT sensorTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/sensor DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN ".DS_Store" EXCLUDE
)

# Export cmake config
install(EXPORT sensorTargets
    FILE sensorTargets.cmake
    NAMESPACE GraphX::
    DESTINATION ${SENSOR_CMAKE_INSTALL_DIR}
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sensorConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/sensorConfig.cmake
    INSTALL_DESTINATION ${SENSOR_CMAKE_INSTALL_DIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/sensorConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/sensorConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/sensorConfigVersion.cmake
    DESTINATION ${SENSOR_CMAKE_INSTALL_DIR}
)
