/**
 * @file NodeFacadeAbi.hpp
 * @brief Node Facade ABI Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace graph {

using NodeHandle = void*;
using CreateNodeFunc = NodeHandle (*)(void);

inline constexpr int kPluginApiVersion = 1;
inline constexpr const char* kPluginAbiTagLibStdCpp = "libstdc++_v1";
inline constexpr const char* kPluginAbiTagLibCpp = "libc++_v1";

/**

 * @struct PortMetadataC

 * @brief Port Metadata C data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct PortMetadataC {
    size_t index;
    char port_name[256];
    char payload_type[256];
    char direction[16];
};

struct ConfigFieldMetadataC {
    char name[256];
    char type[16];
    bool required;
};

inline PortMetadataC MakePortMetadataC(
    std::size_t index,
    std::string_view port_name,
    std::string_view payload_type,
    std::string_view direction)
{
    PortMetadataC metadata{};
    metadata.index = index;

    const auto copy_field = [](char* destination, std::size_t destination_size, std::string_view value) {
        const std::size_t count = value.size() < destination_size - 1 ? value.size() : destination_size - 1;
        std::memcpy(destination, value.data(), count);
        destination[count] = '\0';
    };

    /**
     * @brief Executes the Copy Field operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param port_name Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    copy_field(metadata.port_name, sizeof(metadata.port_name), port_name);
    /**
     * @brief Executes the Copy Field operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param payload_type Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    copy_field(metadata.payload_type, sizeof(metadata.payload_type), payload_type);
    /**
     * @brief Executes the Copy Field operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param direction Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    copy_field(metadata.direction, sizeof(metadata.direction), direction);
    return metadata;
}

inline ConfigFieldMetadataC MakeConfigFieldMetadataC(
    std::string_view name,
    std::string_view type,
    bool required)
{
    ConfigFieldMetadataC metadata{};

    const auto copy_field = [](char* destination, std::size_t destination_size, std::string_view value) {
        const std::size_t count = value.size() < destination_size - 1 ? value.size() : destination_size - 1;
        std::memcpy(destination, value.data(), count);
        destination[count] = '\0';
    };

    copy_field(metadata.name, sizeof(metadata.name), name);
    copy_field(metadata.type, sizeof(metadata.type), type);
    metadata.required = required;
    return metadata;
}

/**

 * @struct ThreadMetricsC

 * @brief Thread Metrics C data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct ThreadMetricsC {
    uint64_t total_iterations;
    uint64_t produce_calls;
    uint64_t consume_calls;
    uint64_t transfer_calls;
    uint64_t total_produce_time_ns;
    uint64_t total_consume_time_ns;
    uint64_t total_transfer_time_ns;
    uint64_t total_idle_time_ns;
    bool thread_active;
};

/**

 * @struct PortQueueMetrics

 * @brief Port Queue Metrics data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct PortQueueMetrics {
    size_t port_index;
    char port_name[256];
    uint64_t messages_enqueued;
    uint64_t messages_dequeued;
    uint64_t messages_rejected;
    uint64_t peak_queue_depth;
    uint64_t current_queue_depth;
    double average_latency_us;
};

/**
 * C-compatible facade exported by plugins.
 *
 * This is the only ABI contract plugin-backed nodes need to share with the
 * host. C++ code should prefer NodeFacadeAdapter, CapabilityContext, and
 * NodeDescriptor instead of touching this table directly.
 */
/**
 * @struct NodeFacade
 * @brief Node Facade data record.
 *
 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
 */
struct NodeFacade {
    int (*GetLifecycleState)(NodeHandle handle);

    bool (*Init)(NodeHandle handle);
    bool (*Start)(NodeHandle handle);
    void (*Stop)(NodeHandle handle);
    bool (*Join)(NodeHandle handle);
    bool (*JoinWithTimeout)(NodeHandle handle, std::chrono::milliseconds timeout);

    void (*Execute)(NodeHandle handle);

    const char* (*GetName)(NodeHandle handle);
    void (*SetName)(NodeHandle handle, const char* name);
    const char* (*GetType)(NodeHandle handle);

    size_t (*GetInputPortCount)(NodeHandle handle);
    size_t (*GetOutputPortCount)(NodeHandle handle);
    const char* (*GetInputPortName)(NodeHandle handle, size_t port);
    const char* (*GetOutputPortName)(NodeHandle handle, size_t port);

    PortMetadataC* (*GetInputPortMetadata)(NodeHandle handle, size_t* out_count);
    PortMetadataC* (*GetOutputPortMetadata)(NodeHandle handle, size_t* out_count);
    void (*FreePortMetadata)(PortMetadataC* metadata);
    ConfigFieldMetadataC* (*GetConfigFieldMetadata)(NodeHandle handle, size_t* out_count);
    void (*FreeConfigFieldMetadata)(ConfigFieldMetadataC* metadata);

    void* (*CreateInputRuntimePort)(NodeHandle handle, size_t port);
    void* (*CreateOutputRuntimePort)(NodeHandle handle, size_t port);
    void (*DestroyRuntimePort)(void* runtime_port);

    const ThreadMetricsC* (*GetThreadMetrics)(NodeHandle handle);
    double (*GetThreadUtilizationPercent)(NodeHandle handle);

    void* (*GetAsDataInjectionNodeConfig)(NodeHandle handle);
    void* (*GetAsIConfigurable)(NodeHandle handle);
    void* (*GetAsIDiagnosable)(NodeHandle handle);
    void* (*GetAsIParameterized)(NodeHandle handle);
    void* (*GetAsIMetricsCallbackProvider)(NodeHandle handle);
    void* (*GetAsICompletionCallback)(NodeHandle handle);
    void* (*GetAsIGpuCapabilityBinding)(NodeHandle handle);

    void (*Destroy)(NodeHandle handle);
};

}  // namespace graph
