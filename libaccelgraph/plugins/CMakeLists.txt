cmake_minimum_required(VERSION 3.23)

include(${CMAKE_SOURCE_DIR}/cmake/PluginTemplate.cmake)

add_graphx_plugins_directory()

add_graphx_plugin(
    TARGET_NAME accelgraph_host_ingress_node
    SOURCE_FILE host_ingress_node_plugin.cpp
    DESCRIPTION "Accelgraph host ingress transfer node"
    DEPENDENCIES accelgraph
)
target_include_directories(accelgraph_host_ingress_node PRIVATE
    ${CMAKE_SOURCE_DIR}/libaccelgraph/include
    ${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
    TARGET_NAME accelgraph_host_to_device_node
    SOURCE_FILE host_to_device_node_plugin.cpp
    DESCRIPTION "Accelgraph host-to-device transfer node"
    DEPENDENCIES accelgraph
)
target_include_directories(accelgraph_host_to_device_node PRIVATE
    ${CMAKE_SOURCE_DIR}/libaccelgraph/include
    ${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
    TARGET_NAME accelgraph_device_to_host_node
    SOURCE_FILE device_to_host_node_plugin.cpp
    DESCRIPTION "Accelgraph device-to-host transfer node"
    DEPENDENCIES accelgraph
)
target_include_directories(accelgraph_device_to_host_node PRIVATE
    ${CMAKE_SOURCE_DIR}/libaccelgraph/include
    ${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
    TARGET_NAME accelgraph_host_egress_node
    SOURCE_FILE host_egress_node_plugin.cpp
    DESCRIPTION "Accelgraph host egress transfer node"
    DEPENDENCIES accelgraph
)
target_include_directories(accelgraph_host_egress_node PRIVATE
    ${CMAKE_SOURCE_DIR}/libaccelgraph/include
    ${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
    TARGET_NAME accelgraph_release_lease_node
    SOURCE_FILE release_lease_node_plugin.cpp
    DESCRIPTION "Accelgraph transfer lease release node"
    DEPENDENCIES accelgraph
)
target_include_directories(accelgraph_release_lease_node PRIVATE
    ${CMAKE_SOURCE_DIR}/libaccelgraph/include
    ${CMAKE_SOURCE_DIR}/libgraph/include
)
