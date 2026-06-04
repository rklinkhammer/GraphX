cmake_minimum_required(VERSION 3.23)

# Include the plugin template
include(${CMAKE_SOURCE_DIR}/cmake/PluginTemplate.cmake)

# ============================================================================
# Plugin Directory Setup
# ============================================================================

add_graphx_plugins_directory()

# ============================================================================
# Define Plugins
# ============================================================================

option(GRAPHX_BUILD_GPU_STUB_PLUGINS
	"Build CPU-safe stub GPU plugins used by topology/lifecycle tests"
	ON)

if(GRAPHX_BUILD_GPU_STUB_PLUGINS)

add_graphx_plugin(
	TARGET_NAME cuda_host_ingress_pinned_source_node
	SOURCE_FILE cuda_host_ingress_pinned_source_node_plugin.cpp
	DESCRIPTION "CUDA stub host ingress pinned source node"
	DEPENDENCIES gpu
)
target_include_directories(cuda_host_ingress_pinned_source_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME cuda_h2d_async_node
	SOURCE_FILE cuda_h2d_async_node_plugin.cpp
	DESCRIPTION "CUDA stub async host-to-device transfer node"
	DEPENDENCIES gpu
)
target_include_directories(cuda_h2d_async_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME cuda_d2h_async_node
	SOURCE_FILE cuda_d2h_async_node_plugin.cpp
	DESCRIPTION "CUDA stub async device-to-host transfer node"
	DEPENDENCIES gpu
)
target_include_directories(cuda_d2h_async_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME cuda_host_egress_sink_node
	SOURCE_FILE cuda_host_egress_sink_node_plugin.cpp
	DESCRIPTION "CUDA stub host egress sink node"
	DEPENDENCIES gpu
)
target_include_directories(cuda_host_egress_sink_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME cuda_lease_release_node
	SOURCE_FILE cuda_lease_release_node_plugin.cpp
	DESCRIPTION "CUDA stub lease release node"
	DEPENDENCIES gpu
)
target_include_directories(cuda_lease_release_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME sycl_host_ingress_pinned_source_node
	SOURCE_FILE sycl_host_ingress_pinned_source_node_plugin.cpp
	DESCRIPTION "SYCL stub host ingress pinned source node"
	DEPENDENCIES gpu
)
target_include_directories(sycl_host_ingress_pinned_source_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME sycl_h2d_async_node
	SOURCE_FILE sycl_h2d_async_node_plugin.cpp
	DESCRIPTION "SYCL stub async host-to-device transfer node"
	DEPENDENCIES gpu
)
target_include_directories(sycl_h2d_async_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME sycl_d2h_async_node
	SOURCE_FILE sycl_d2h_async_node_plugin.cpp
	DESCRIPTION "SYCL stub async device-to-host transfer node"
	DEPENDENCIES gpu
)
target_include_directories(sycl_d2h_async_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME sycl_host_egress_sink_node
	SOURCE_FILE sycl_host_egress_sink_node_plugin.cpp
	DESCRIPTION "SYCL stub host egress sink node"
	DEPENDENCIES gpu
)
target_include_directories(sycl_host_egress_sink_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME sycl_lease_release_node
	SOURCE_FILE sycl_lease_release_node_plugin.cpp
	DESCRIPTION "SYCL stub lease release node"
	DEPENDENCIES gpu
)
target_include_directories(sycl_lease_release_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME metal_host_ingress_pinned_source_node
	SOURCE_FILE metal_host_ingress_pinned_source_node_plugin.cpp
	DESCRIPTION "Metal stub host ingress pinned source node"
	DEPENDENCIES gpu
)
target_include_directories(metal_host_ingress_pinned_source_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME metal_h2d_async_node
	SOURCE_FILE metal_h2d_async_node_plugin.cpp
	DESCRIPTION "Metal stub async host-to-device transfer node"
	DEPENDENCIES gpu
)
target_include_directories(metal_h2d_async_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME metal_d2h_async_node
	SOURCE_FILE metal_d2h_async_node_plugin.cpp
	DESCRIPTION "Metal stub async device-to-host transfer node"
	DEPENDENCIES gpu
)
target_include_directories(metal_d2h_async_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME metal_host_egress_sink_node
	SOURCE_FILE metal_host_egress_sink_node_plugin.cpp
	DESCRIPTION "Metal stub host egress sink node"
	DEPENDENCIES gpu
)
target_include_directories(metal_host_egress_sink_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME metal_lease_release_node
	SOURCE_FILE metal_lease_release_node_plugin.cpp
	DESCRIPTION "Metal stub lease release node"
	DEPENDENCIES gpu
)
target_include_directories(metal_lease_release_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME metal_device_transform_node
	SOURCE_FILE metal_device_transform_node_plugin.cpp
	DESCRIPTION "Metal stub device transform node"
	DEPENDENCIES gpu
)
target_include_directories(metal_device_transform_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME metal_device_reduce_node
	SOURCE_FILE metal_device_reduce_node_plugin.cpp
	DESCRIPTION "Metal stub device reduce node"
	DEPENDENCIES gpu
)
target_include_directories(metal_device_reduce_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME metal_queue_sync_node
	SOURCE_FILE metal_queue_sync_node_plugin.cpp
	DESCRIPTION "Metal stub queue sync node"
	DEPENDENCIES gpu
)
target_include_directories(metal_queue_sync_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME metal_device_shard_node
	SOURCE_FILE metal_device_shard_node_plugin.cpp
	DESCRIPTION "Metal stub device shard node"
	DEPENDENCIES gpu
)
target_include_directories(metal_device_shard_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME metal_peer_copy_node
	SOURCE_FILE metal_peer_copy_node_plugin.cpp
	DESCRIPTION "Metal stub peer copy node"
	DEPENDENCIES gpu
)
target_include_directories(metal_peer_copy_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

add_graphx_plugin(
	TARGET_NAME metal_collective_reduce_node
	SOURCE_FILE metal_collective_reduce_node_plugin.cpp
	DESCRIPTION "Metal stub collective reduce node"
	DEPENDENCIES gpu
)
target_include_directories(metal_collective_reduce_node PRIVATE
	${CMAKE_SOURCE_DIR}/libgpu/include
	${CMAKE_SOURCE_DIR}/libgraph/include
)

endif()

# Real backend plugins remain backend-gated work. They should be introduced in
# separate blocks keyed off GRAPHX_CUDA_GRAPH_NODES_ENABLED and
# GRAPHX_SYCL_GRAPH_NODES_ENABLED once plugin construction is routed through a
# shared CapabilityBus/bootstrap path rather than private default capabilities.
