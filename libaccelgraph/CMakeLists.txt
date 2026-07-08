cmake_minimum_required(VERSION 3.23)
project(libaccelgraph VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(ACCELGRAPH_ENABLE_METAL "Enable libaccelgraph native Metal backend" OFF)
option(ACCELGRAPH_ENABLE_CUDA "Enable libaccelgraph native CUDA backend" OFF)

file(GLOB SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/**/*.cpp")

add_library(accelgraph STATIC ${SOURCES})
target_compile_definitions(accelgraph PUBLIC
    ACCELGRAPH_ENABLE_METAL=$<BOOL:${ACCELGRAPH_ENABLE_METAL}>
    ACCELGRAPH_ENABLE_CUDA=$<BOOL:${ACCELGRAPH_ENABLE_CUDA}>
)
target_include_directories(accelgraph PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(accelgraph PUBLIC
    graph
)

if(MSVC)
    target_compile_options(accelgraph PRIVATE /W4)
else()
    target_compile_options(accelgraph PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ENABLE_TSAN)
    target_compile_options(accelgraph PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(accelgraph PRIVATE -fsanitize=thread)
endif()

set(ACCELGRAPH_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/accelgraph")

install(TARGETS accelgraph EXPORT accelgraphTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/accelgraph DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN ".DS_Store" EXCLUDE
)

install(EXPORT accelgraphTargets
    FILE accelgraphTargets.cmake
    NAMESPACE GraphX::
    DESTINATION ${ACCELGRAPH_CMAKE_INSTALL_DIR}
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/accelgraphConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/accelgraphConfig.cmake
    INSTALL_DESTINATION ${ACCELGRAPH_CMAKE_INSTALL_DIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/accelgraphConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/accelgraphConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/accelgraphConfigVersion.cmake
    DESTINATION ${ACCELGRAPH_CMAKE_INSTALL_DIR}
)