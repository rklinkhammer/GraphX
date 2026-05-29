// MIT License
//
// Copyright (c) 2025 graphlib contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <log4cxx/logger.h>
#include <chrono>
#include "graph/IDataInjectionSource.hpp"

// Forward declaration to avoid circular dependency
namespace graph {
template <typename NodeT>
struct NodePluginInstance;
}

// Forward declaration of IDataInjectionSource in framework datasources namespace
namespace graph {

// Opaque handle to a node instance
typedef void* NodeHandle;

/**
 * @struct PortMetadataC
 * @brief C-compatible port metadata with owned string storage
 *
 * Strings are stored directly in the struct using fixed-size character arrays.
 * This ensures strings remain valid even after the source C++ vector is destroyed
 * or goes out of scope. All fields are initialized and null-terminated.
 *
 * Used by GetInputPortMetadata() and GetOutputPortMetadata() functions in NodeFacade
 * to provide port introspection across plugin boundaries.
 *
 * @see NodeFacade::GetInputPortMetadata
 * @see NodeFacade::GetOutputPortMetadata
 */
typedef struct {
    size_t index;                   ///< Port index (0-based)
    char port_name[256];            ///< Human-readable port name (e.g., "Accel")
    char payload_type[256];         ///< Data type name (e.g., "AccelerometerData")
    char direction[16];             ///< "input" or "output"
} PortMetadataC;

/**
 * @struct ThreadMetricsC
 * @brief C-compatible thread metrics (mirrors nodes/sys/ThreadMetrics.hpp)
 * 
 * Contains thread performance data for a node's worker thread.
 * All fields are plain-old-data (POD) for C compatibility.
 * Used to expose thread metrics across plugin boundaries.
 * 
 * @see NodeFacade::GetThreadMetrics
 */
typedef struct {
    uint64_t total_iterations;       ///< Main loop iterations
    uint64_t produce_calls;          ///< Produce() invocations
    uint64_t consume_calls;          ///< Consume() invocations
    uint64_t transfer_calls;         ///< Transfer() invocations
    uint64_t total_produce_time_ns;  ///< Cumulative produce time
    uint64_t total_consume_time_ns;  ///< Cumulative consume time
    uint64_t total_transfer_time_ns; ///< Cumulative transfer time
    uint64_t total_idle_time_ns;     ///< Time waiting for input
    bool thread_active;              ///< Is thread currently running?
} ThreadMetricsC;

/**
 * @struct PortQueueMetrics
 * @brief Metrics for a single port's queue
 * 
 * Captures queue behavior for one input or output port.
 * Used to expose per-port queue statistics across plugin boundaries.
 * 
 * @see NodeFacade::GetInputQueueMetrics
 * @see NodeFacade::GetOutputQueueMetrics
 */
typedef struct {
    size_t port_index;               ///< Port index (0-based)
    char port_name[256];             ///< Human-readable port name
    uint64_t messages_enqueued;      ///< Total messages added to queue
    uint64_t messages_dequeued;      ///< Total messages removed from queue
    uint64_t messages_rejected;      ///< Messages rejected due to queue full
    uint64_t peak_queue_depth;       ///< Maximum queue size observed
    uint64_t current_queue_depth;    ///< Current number of messages in queue
    double average_latency_us;       ///< Average message latency in microseconds
} PortQueueMetrics;

/**
 * @struct NodeFacade
 * @brief C-compatible facade for dynamically loaded nodes
 *
 * This structure contains function pointers that define the interface
 * for nodes loaded from plugins. All functions must be C-compatible
 * (C linkage, no C++ features) to work across plugin boundaries.
 *
 * REQUIRED CALLBACKS (must not be null):
 * - Init: Initialize the node (allocate resources, setup)
 * - Start: Start the node (begin execution, spawn threads)
 * - Stop: Stop the node (signal shutdown)
 * - Destroy: Destroy the node (free resources)
 *
 * OPTIONAL CALLBACKS (may be null if interface not supported):
 * - GetAsCSVNodeConfig: CSV data source interface
 * - GetAsIConfigurable: Configuration interface
 * - GetAsIDiagnosable: Diagnostics interface
 * - GetAsIParameterized: Parameter management interface
 * - GetAsIMetricsCallbackProvider: Metrics callback interface
 *
 * Plugin nodes must implement all required callbacks. Optional callbacks
 * are only required if the plugin supports the corresponding interface.
 *
 * NodeFacadeAdapter constructor enforces these preconditions with asserts
 * in debug builds. This is a programming error if violated.
 *
 * @see PluginRegistry
 * @see PluginLoader
 * @see NodeFacadeAdapter
 */
struct NodeFacade {
    // ====== Lifecycle Management ======
    
    int (*GetLifecycleState)(NodeHandle handle);

    /// Initialize the node (allocate resources, setup)
    bool (*Init)(NodeHandle handle);
    
    /// Start the node (begin execution, spawn threads)
    bool (*Start)(NodeHandle handle);
    
    /// Stop the node (signal shutdown)
    void (*Stop)(NodeHandle handle);
    
    /// Join the node (wait for shutdown to complete)
    bool (*Join)(NodeHandle handle);
      
    /// Join the node with a timeout
    bool (*JoinWithTimeout)(NodeHandle handle, std::chrono::milliseconds timeout);
    
    // ====== Execution ======
    
    /// Execute one cycle of the node's work
    void (*Execute)(NodeHandle handle);
    
    // ====== Metadata Queries ======
    
    /// Get the human-readable name of this node instance
    /// @return Pointer to null-terminated string (valid until node destroyed)
    const char* (*GetName)(NodeHandle handle);
   
    /// Set the human-readable name of this node instance
    /// @param name Pointer to null-terminated string
    void (*SetName)(NodeHandle handle, const char* name);

    /// Get the type name of this node class
    /// @return Pointer to null-terminated string (valid until node destroyed)
    const char* (*GetType)(NodeHandle handle);
    
    // ====== Port Information ======
    
    /// Get the number of input ports
    size_t (*GetInputPortCount)(NodeHandle handle);
    
    /// Get the number of output ports
    size_t (*GetOutputPortCount)(NodeHandle handle);
    
    /// Get the name of an input port
    /// @param port Port index (0-based)
    /// @return Pointer to null-terminated string, or nullptr if invalid port
    const char* (*GetInputPortName)(NodeHandle handle, size_t port);
    
    /// Get the name of an output port
    /// @param port Port index (0-based)
    /// @return Pointer to null-terminated string, or nullptr if invalid port
    const char* (*GetOutputPortName)(NodeHandle handle, size_t port);
    
    // ====== Port Metadata Queries (Option C: Stable Memory) ======
    
    /**
     * Get detailed metadata for all input ports
     *
     * Returns a heap-allocated array of PortMetadataC structs with string storage.
     * All strings are copied into the struct (not pointers to transient data).
     * The returned array must be freed by calling FreePortMetadata().
     *
     * @param handle Opaque node handle
     * @param out_count Output parameter: number of input ports
     * @return Pointer to array of PortMetadataC (heap-allocated), or nullptr on error
     *         Caller must free with FreePortMetadata() when done
     *
     * Example:
     * @code
     *   size_t count = 0;
     *   PortMetadataC* metadata = facade.GetInputPortMetadata(handle, &count);
     *   if (metadata) {
     *       for (size_t i = 0; i < count; ++i) {
     *           printf("Port %zu: %s\n", metadata[i].index, metadata[i].name);
     *       }
     *       FreePortMetadata(metadata);
     *   }
     * @endcode
     */
    PortMetadataC* (*GetInputPortMetadata)(NodeHandle handle, size_t* out_count);
    
    /**
     * Get detailed metadata for all output ports
     *
     * Returns a heap-allocated array of PortMetadataC structs with string storage.
     * All strings are copied into the struct (not pointers to transient data).
     * The returned array must be freed by calling FreePortMetadata().
     *
     * @param handle Opaque node handle
     * @param out_count Output parameter: number of output ports
     * @return Pointer to array of PortMetadataC (heap-allocated), or nullptr on error
     *         Caller must free with FreePortMetadata() when done
     *
     * Example:
     * @code
     *   size_t count = 0;
     *   PortMetadataC* metadata = facade.GetOutputPortMetadata(handle, &count);
     *   if (metadata) {
     *       for (size_t i = 0; i < count; ++i) {
     *           printf("Port %zu: %s (type: %s)\n", 
     *                  metadata[i].index, metadata[i].name, metadata[i].type_name);
     *       }
     *       FreePortMetadata(metadata);
     *   }
     * @endcode
     */
    PortMetadataC* (*GetOutputPortMetadata)(NodeHandle handle, size_t* out_count);
    
    /**
     * Free a PortMetadataC array returned from GetInputPortMetadata or GetOutputPortMetadata
     *
     * Must be called exactly once for each non-null array returned from the
     * GetInputPortMetadata or GetOutputPortMetadata function pointers.
     * Safe to call with nullptr.
     *
     * @param metadata Array to free (may be nullptr)
     */
    void (*FreePortMetadata)(PortMetadataC* metadata);
    
    // ====== Thread Metrics Queries ======
    
    /// Get thread metrics for the node's worker thread
    /// @param handle Opaque node handle
    /// @return Pointer to ThreadMetricsC (read-only, valid until node destroyed)
    /// @note Do not free the returned pointer - it points to internal memory
    /// @return nullptr if node has no worker thread or metrics unavailable
    const ThreadMetricsC* (*GetThreadMetrics)(NodeHandle handle);
    
    /// Get high-level thread utilization percentage
    /// @param handle Opaque node handle
    /// @return Percentage 0-100 (0 = idle, 100 = constantly busy)
    /// @note Calculated from thread metrics, 0 if metrics unavailable
    double (*GetThreadUtilizationPercent)(NodeHandle handle);
    
    // ====== Framework Interface Queries ======
    
    /**
     * Get pointer to CSVNodeConfig interface (if node is a CSV sensor source)
     * 
     * This callback allows CSV plugins to expose their CSVNodeConfig interface
     * without requiring the caller to know the concrete node type.
     * 
     * @param handle Opaque node handle
     * @return void* pointer that can be cast to graph::datasources::IDataInjectionSource*, or nullptr if not a CSV node
     * @note Returned pointer is valid for the lifetime of the node
     * @note Do not delete the returned pointer
     * 
     * Only CSV sensor plugins (accelerometer, gyroscope, magnetometer, barometric, GPS)
     * implement this callback. Non-CSV nodes return nullptr.
     * 
     * Example:
     * @code
     *   void* csv_ptr = facade->GetAsCSVNodeConfig(handle);
     *   if (csv_ptr) {
     *       auto csv_config = reinterpret_cast<graph::datasources::IDataInjectionSource*>(csv_ptr);
     *       // Use csv_config to access CSV-specific functionality
     *   }
     * @endcode
     */
    void* (*GetAsDataInjectionNodeConfig)(NodeHandle handle);
    
    /**
     * Get pointer to IMetricsCallbackProvider interface (if node supports metrics callbacks)
     * 
     * @param handle Opaque node handle
     * @return void* pointer that can be cast to graph::IMetricsCallbackProvider*, or nullptr
     * @note Returned pointer is valid for the lifetime of the node
     * @note Do not delete the returned pointer
     */
    void* (*GetAsIMetricsCallbackProvider)(NodeHandle handle);
    
    /**
     * Get pointer to ICompletionCallback interface (if node supports completion callbacks)
     * 
     * @param handle Opaque node handle
     * @return void* pointer that can be cast to graph::CompletionCallbackProvider*, or nullptr
     * @note Returned pointer is valid for the lifetime of the node
     * @note Do not delete the returned pointer
     */
    void* (*GetAsICompletionCallback)(NodeHandle handle);
    
    void (*Destroy)(NodeHandle handle);
};

// ============================================================================
// Modern C++ Node Interface (Unified)
// ============================================================================

/**
 * @class INodeFacade
 * @brief Modern unified C++ interface for dynamic nodes
 *
 * This interface provides a clean, type-safe way to interact with
 * dynamically-loaded nodes. It replaces the V1 individual getter pattern
 * with consolidated methods that provide better performance and usability.
 *
 * All port and node metadata is accessed through a single GetMetadata() call,
 * eliminating the need for multiple separate method calls.
 *
 * Optional capabilities (CSV, metrics, diagnostics) are accessed through
 * GetInterface(name), providing type-safe optional access without void* casting.
 *
 * ## Thread Safety
 * - Init(), Start(), Stop(), Execute() are NOT thread-safe
 * - GetMetadata() is thread-safe for reading
 * - GetInterface() is thread-safe for reading
 * - All returned shared_ptr<> are thread-safe (atomic ref counting)
 *
 * ## Memory Management
 * All returned objects via shared_ptr have automatic lifetime management.
 * No manual deletion required.
 */
class INodeFacade {
public:
    virtual ~INodeFacade() = default;

    // ========================================================================
    // Metadata Structures
    // ========================================================================

    /**
     * @struct PortInfo
     * @brief Information about a single port
     */
    struct PortInfo {
        std::string name;       ///< Port name (e.g., "temperature")
        std::string type;       ///< Data type name (e.g., "float")
        std::string direction;  ///< "input" or "output"
    };

    /**
     * @struct NodeMetadata
     * @brief Complete metadata for a node
     *
     * Provides all introspection info in a single structure.
     * Replaces the V1 pattern of calling separate methods for each piece of data.
     */
    struct NodeMetadata {
        std::string name;                       ///< Human-readable node name
        std::string type;                       ///< Node class type
        std::string description;                ///< Node description
        size_t input_port_count = 0;            ///< Number of inputs
        size_t output_port_count = 0;           ///< Number of outputs
        std::vector<PortInfo> input_ports;      ///< Input port details
        std::vector<PortInfo> output_ports;     ///< Output port details
    };

    // ========================================================================
    // Lifecycle Methods
    // ========================================================================

    /**
     * Initialize the node.
     * @return true if initialization succeeded
     */
    virtual bool Init() = 0;

    /**
     * Start node execution.
     * @return true if start succeeded
     */
    virtual bool Start() = 0;

    /**
     * Stop node execution.
     */
    virtual void Stop() = 0;

    /**
     * Execute one iteration of node processing.
     */
    virtual void Execute() = 0;

    // ========================================================================
    // Introspection Methods
    // ========================================================================

    /**
     * Get complete metadata for this node in a single call.
     *
     * This replaces the V1 pattern of calling GetName(), GetInputPortCount(),
     * GetInputPortName() for each port, etc. All info is now available in one call.
     *
     * @return NodeMetadata with all introspection information
     * @thread-safe for reading
     */
    virtual NodeMetadata GetMetadata() const = 0;

    /**
     * Get a typed interface for optional capabilities.
     *
     * Provides type-safe access to plugin-specific interfaces without void* casting.
     *
     * @param name Interface name (e.g., "csv_injection", "metrics_callback", "diagnostics")
     * @return shared_ptr to interface (nullptr if not supported)
     * @thread-safe for reading
     *
     * @example
     * ```cpp
     * if (auto csv = node->GetInterface("csv_injection")) {
     *     auto csv_inject = std::dynamic_pointer_cast<IDataInjectionSource>(csv);
     *     if (csv_inject) {
     *         csv_inject->InjectData(data);
     *     }
     * }
     * ```
     */
    virtual std::shared_ptr<void> GetInterface(const std::string& name) const = 0;

    /**
     * Get CSV data injection interface if supported.
     *
     * Convenience method providing typed access without casting.
     *
     * @return shared_ptr<IDataInjectionSource> if supported, nullptr otherwise
     * @thread-safe for reading
     */
    virtual std::shared_ptr<void> GetCSVInterface() const = 0;

    // ========================================================================
    // Phase 3: Convenience Methods for Port Names
    // ========================================================================

    /**
     * Get all input port names in a single call.
     *
     * Convenience method for when you just need the port names without full metadata.
     * Equivalent to iterating metadata.input_ports, but more convenient.
     *
     * @return Vector of input port names (empty if no inputs or not available)
     * @thread-safe for reading
     *
     * @example
     * ```cpp
     * auto input_names = node->GetInputPortNames();
     * for (const auto& name : input_names) {
     *     std::cout << "Input: " << name << "\n";
     * }
     * ```
     */
    virtual std::vector<std::string> GetInputPortNames() const = 0;

    /**
     * Get all output port names in a single call.
     *
     * Convenience method for when you just need the port names without full metadata.
     * Equivalent to iterating metadata.output_ports, but more convenient.
     *
     * @return Vector of output port names (empty if no outputs or not available)
     * @thread-safe for reading
     *
     * @example
     * ```cpp
     * auto output_names = node->GetOutputPortNames();
     * for (const auto& name : output_names) {
     *     std::cout << "Output: " << name << "\n";
     * }
     * ```
     */
    virtual std::vector<std::string> GetOutputPortNames() const = 0;
};

/**
 * @class NodeFacadeAdapter
 * @brief C++ wrapper implementing unified modern interface
 *
 * Wraps the C NodeFacade struct in a modern C++ interface (INodeFacade).
 * Plugins continue to export NodeFacadeC, but NodeFacadeAdapter provides
 * clean, type-safe access through the INodeFacade interface.
 *
 * Usage:
 * ```cpp
 * NodeFacadeAdapter adapter(handle, &facade);
 * if (!adapter.Init()) return false;
 * if (!adapter.Start()) return false;
 *
 * auto metadata = adapter.GetMetadata();  // Single call for all info
 * for (const auto& port : metadata.input_ports) {
 *     // Process port
 * }
 *
 * adapter.Execute();
 * adapter.Stop();
 * ```
 */
class NodeFacadeAdapter : public INodeFacade {
private:
    static log4cxx::LoggerPtr logger_;
    
    NodeHandle handle_;
    const NodeFacade* facade_;
    bool initialized_;
    bool started_;
    
    // Interface pointers extracted from callbacks at construction
    std::shared_ptr<void> data_injection_node_config_ptr_;           ///< Points to CSVNodeConfig if plugin provides it
    std::shared_ptr<void> configurable_ptr_;              ///< Points to IConfigurable if plugin provides it
    std::shared_ptr<void> diagnosable_ptr_;               ///< Points to IDiagnosable if plugin provides it
    std::shared_ptr<void> parameterized_ptr_;             ///< Points to IParameterized if plugin provides it
    std::shared_ptr<void> metrics_callback_provider_ptr_; ///< Points to IMetricsCallbackProvider if plugin provides it
    std::shared_ptr<void> completion_callback_provider_ptr_; ///< Points to CompletionCallbackProvider if plugin provides it
    
    // Extract interface pointers from plugin callbacks
    void ExtractInterfaces();

public:
    /**
     * Construct a facade adapter
     *
     * PRECONDITIONS (enforced by asserts):
     * - handle must not be null
     * - facade must not be null
     * - facade->Init must not be null (required callback)
     * - facade->Start must not be null (required callback)
     * - facade->Stop must not be null (required callback)
     * - facade->Destroy must not be null (required callback)
     *
     * If any precondition is violated, an assertion will fail in debug builds.
     * This indicates a programming error in the caller.
     *
     * OPTIONAL CALLBACKS:
     * The following callbacks may be null (checked at runtime):
     * - facade->GetAsCSVNodeConfig (if node is not CSV-capable)
     * - facade->GetAsIConfigurable (if node doesn't support configuration)
     * - facade->GetAsIDiagnosable (if node doesn't support diagnostics)
     * - facade->GetAsIParameterized (if node doesn't have parameters)
     * - facade->GetAsIMetricsCallbackProvider (if node doesn't support metrics)
     *
     * @param handle Opaque node handle from plugin (must not be null)
     * @param facade Pointer to NodeFacade struct with function pointers (must not be null)
     *
     * @throws std::assertion_failure in debug builds if preconditions violated
     */
    NodeFacadeAdapter(NodeHandle handle, const NodeFacade* facade);

    /**
     * Destructor - calls Destroy() on the facade if it's set
     */
    ~NodeFacadeAdapter();

    /**
     * Move constructor - transfer ownership of node handle
     *
     * @param other Other adapter to move from
     */
    NodeFacadeAdapter(NodeFacadeAdapter&& other) noexcept;

    /**
     * Move assignment operator - transfer ownership of node handle
     *
     * @param other Other adapter to move from
     * @return Reference to this object
     */
    NodeFacadeAdapter& operator=(NodeFacadeAdapter&& other) noexcept;

    // Disable copy semantics - each adapter owns a unique node handle
    NodeFacadeAdapter(const NodeFacadeAdapter&) = delete;
    NodeFacadeAdapter& operator=(const NodeFacadeAdapter&) = delete;

    /*
     * Get the current lifecycle state of the wrapped node
     * @return Current LifecycleState enum value
    */
    int GetLifecycleState() const;


    /**
     * Initialize the node
     *
     * @return true if initialization succeeded
     */
    bool Init() override;

    /**
     * Start the node
     *
     * @return true if start succeeded
     */
    bool Start() override;

    /**
     * Stop the node
     */
    void Stop() override;

    /**
     * Cleanup the node - calls facade Destroy() callback
     * 
     * This must be called explicitly BEFORE the adapter is destroyed
     * to ensure Destroy() happens while the node is still alive.
     * 
     * Called automatically by GraphManager during its destructor
     * BEFORE clearing the nodes_ vector.
     * 
     * Safe to call multiple times (subsequent calls are no-ops).
     */
    void Cleanup();

    /**
     * Join the node (wait for shutdown)
     *
     * @return true if join succeeded
     */
    bool Join();

    /**
     * Join the node (timed wait for shutdown)
     *
     * @return true if join succeeded
     */
    bool JoinWithTimeout(std::chrono::milliseconds timeout);

    /**
     * Execute one cycle of the node
     */
    void Execute() override;

    /**
     * Set the node's instance name
     * 
     * @param name New name for the node instance
     */
    void SetName(const std::string& name);

    /**
     * Get the node's instance name
     *
     * @return Human-readable name, or empty string if facade doesn't implement it
     */
    const std::string GetName() const;

    /**
     * Get the node's type name
     *
     * @return Type name, or empty string if facade doesn't implement it
     */
    const std::string GetType() const;

    /**
     * Get the node's description
     *
     * @return Description, or empty string if facade doesn't implement it
     */
    std::string GetDescription() const;

    /**
     * Get the number of input ports
     *
     * @return Number of input ports, or 0 if facade doesn't implement it
     */
    size_t GetInputPortCount() const;

    /**
     * Get the number of output ports
     *
     * @return Number of output ports, or 0 if facade doesn't implement it
     */
    size_t GetOutputPortCount() const;

    /**
     * Get the name of an input port
     *
     * @param port Port index
     * @return Port name, or empty string if invalid port or not implemented
     */
    std::string GetInputPortName(size_t port) const;

    /**
     * Get the name of an output port
     *
     * @param port Port index
     * @return Port name, or empty string if invalid port or not implemented
     */
    std::string GetOutputPortName(size_t port) const;

    /**
     * Get detailed metadata for all input ports (Option C: Stable Memory)
     *
     * Returns metadata with strings owned by the struct (no dangling pointers).
     * Metadata is collected at call time from the underlying node.
     *
     * @return Vector of PortMetadataC (empty on error or if not implemented)
     *
     * Example:
     * @code
     *   auto metadata = adapter.GetInputPortMetadata();
     *   for (const auto& port : metadata) {
     *       std::cout << "Port " << port.index << ": " << port.name << "\n";
     *   }
     * @endcode
     */
    std::vector<PortMetadataC> GetInputPortMetadata() const;

    /**
     * Get detailed metadata for all output ports (Option C: Stable Memory)
     *
     * Returns metadata with strings owned by the struct (no dangling pointers).
     * Metadata is collected at call time from the underlying node.
     *
     * @return Vector of PortMetadataC (empty on error or if not implemented)
     *
     * Example:
     * @code
     *   auto metadata = adapter.GetOutputPortMetadata();
     *   for (const auto& port : metadata) {
     *       std::cout << "Port " << port.index << ": " 
     *                 << port.type_name << " (" << port.direction << ")\n";
     *   }
     * @endcode
     */
    std::vector<PortMetadataC> GetOutputPortMetadata() const;

    /**
     * Set a configuration property
     *
     * @param key Property name
     * @param value Property value
     * @return true if property was set
     */
    bool SetProperty(const std::string& key, const std::string& value);

    /**
     * Get a configuration property
     *
     * @param key Property name
     * @return Property value, or empty string if not found
     */
    std::string GetProperty(const std::string& key) const;

    // ====== NEW: Thread Metrics Accessors ======
    
    /// Get thread metrics for this node
    /// @return Pointer to ThreadMetricsC, or nullptr if not available
    /// @note Do not delete returned pointer - points to internal memory
    const ThreadMetricsC* GetThreadMetrics() const {
        if (!facade_ || !facade_->GetThreadMetrics) return nullptr;
        return facade_->GetThreadMetrics(handle_);
    }
    
    /// Get thread utilization as percentage (0-100)
    /// @return Percentage, or 0 if not available
    double GetThreadUtilizationPercent() const {
        if (!facade_ || !facade_->GetThreadUtilizationPercent) return 0.0;
        return facade_->GetThreadUtilizationPercent(handle_);
    }
    
    // ====== NEW: Port Queue Metrics Accessors ======
    
    // ====== NEW: State Accessors ======
    
    /// Check if node's worker thread has metrics available
    /// @return true if node has worker thread with metrics
    bool HasThreadMetrics() const {
        return GetThreadMetrics() != nullptr;
    }
    
    /// Get comprehensive node status
    /// @return Status string with node name, state, and key metrics
    std::string GetStatusString() const;

    // Capability accessors for interfaces extracted at construction

    std::shared_ptr<void> GetConfigurablePtr() const {
        return configurable_ptr_;
    }

    std::shared_ptr<void> GetDiagnosablePtr() const {
        return diagnosable_ptr_;
    }   
    
    std::shared_ptr<void> GetParameterizedPtr() const {
        return parameterized_ptr_;
    }   

    std::shared_ptr<void> GetDataInjectionNodeConfigPtr() const {
        return data_injection_node_config_ptr_;
    }

    std::shared_ptr<void> GetMetricsCallbackProviderPtr() const {
        return metrics_callback_provider_ptr_;
    }

    std::shared_ptr<void> GetCompletionCallbackProviderPtr() const {
        return completion_callback_provider_ptr_;
    }
    
    // ====== NEW: Configuration & Diagnostics Accessors ======
    
    /**
     * Get configuration as JSON string (if node supports IConfigurable)
     *
     * @return JSON string with configuration, or empty string if not configurable
     */
    std::string GetConfigurationJSON() const;
    
    /**
     * Set configuration from JSON string (if node supports IConfigurable)
     *
     * @param json_str JSON string with configuration parameters
     * @return true if configuration accepted, false otherwise
     */
    bool SetConfigurationJSON(const std::string& json_str);
    
/**
     * Get diagnostics as JSON string (if node supports IDiagnosable)
     *
     * @return JSON string with diagnostics/metrics, or empty string if not available
     */
    std::string GetDiagnosticsJSON() const;
    
/**
     * Get parameters as JSON string (if node supports IParameterized)
     *
     * @return JSON string with parameter names and values, or empty string
     */
    std::string GetParametersJSON() const;
    
    /**
     * Set individual parameter (if node supports IParameterized)
     *
     * @param param_name Parameter name (e.g., "outlier_threshold")
     * @param json_value Parameter value as JSON (e.g., "2.5" or "true")
     * @return true if parameter set, false if unknown or invalid
     */
    bool SetParameter(const std::string& param_name, const std::string& json_value);

    /**
     * Check if the node is initialized
     */
    bool IsInitialized() const { return initialized_; }

    /**
     * Check if the node is started
     */
    bool IsStarted() const { return started_; }

    /**
     * Get the underlying handle (for advanced use)
     */
    NodeHandle GetHandle() const { return handle_; }

    /**
     * Get the underlying facade pointer (for advanced use)
     */
    const NodeFacade* GetFacade() const { return facade_; }

    /**
     * Extract the underlying typed node from the opaque handle
     *
     * This method casts the opaque NodeHandle back to its original type,
     * allowing you to work with the concrete node class. This is useful for
     * operations that require the full typed interface, such as adding edges
     * to a GraphManager.
     *
     * @tparam NodeT The concrete node type (e.g., DataInjectionAccelerometerNode, FlightFSMNode)
     * @return std::shared_ptr<NodeT> The underlying node, or nullptr if cast fails
     *
     * Supports casting to both concrete types and base classes. For example:
     * - GetNode<DataInjectionAccelerometerNode>() returns the concrete node
     * - GetNode<CSVNodeConfig>() returns the base class interface (for CSV nodes)
     * - GetNode<INode>() returns the plugin interface
     *
     * Example:
     * @code
     *   auto adapter = factory.CreateDynamicNode("DataInjectionAccelerometerNode");
     *   auto facade_adapter = std::make_shared<NodeFacadeAdapter>(adapter.first, adapter.second);
     *   
     *   // Extract the underlying typed node
     *   auto accel_node = facade_adapter->GetNode<DataInjectionAccelerometerNode>();
     *   if (accel_node) {
     *       // Now you can use it with GraphManager
     *       graph_manager.AddEdge<DataInjectionAccelerometerNode, 0, FlightFSMNode, 0>(
     *           accel_node, fsm_node);
     *   }
     *   
     *   // Or cast to base class
     *   auto csv_config = facade_adapter->GetNode<CSVNodeConfig>();
     *   if (csv_config) {
     *       // Access CSV configuration
     *       auto& queue = csv_config->GetCSVQueue();
     *   }
     * @endcode
     */
    template <typename NodeT>
    std::shared_ptr<NodeT> GetNode() const {
        // Strategy: Try to find a NodePluginInstance with a node type that can be cast to NodeT
        // This handles both concrete types (DataInjectionAccelerometerNode) and base classes (CSVNodeConfig)
        
        // First, try direct cast as concrete type
        // This works when NodeT is the exact plugin node type
        using ConcreteInstance = graph::NodePluginInstance<NodeT>;
        if (auto instance = static_cast<ConcreteInstance*>(handle_)) {
            if (instance && instance->node) {
                return instance->node;
            }
        }
        return nullptr;
    }
    
    /**
     * Try to get a typed interface from the plugin node
     * 
     * This method provides type-safe access to optional plugin interfaces
     * without requiring RTTI or virtual methods on the INode hierarchy.
     * Interfaces are extracted at plugin load time via callbacks and cached.
     * 
     * Currently supported interfaces:
     * - graph::datasources::IDataInjectionSource: For CSV data producer nodes
     * 
     * @tparam InterfaceT The interface type to query (e.g., graph::datasources::IDataInjectionSource)
     * @return shared_ptr to the interface, or nullptr if not supported
     * 
     * Example:
     * @code
     *   auto adapter = std::make_shared<NodeFacadeAdapter>(handle, facade);
     *   auto csv_config = adapter->TryGetInterface<graph::datasources::IDataInjectionSource>();
     *   if (csv_config) {
     *       // Use CSV configuration
     *       auto& queue = csv_config->GetCSVQueue();
     *   }
     * @endcode
     * 
     * @note The actual type checking is done at compile time via template specialization
     */
    template <typename InterfaceT>
    std::shared_ptr<InterfaceT> TryGetInterface() const {
        // Default implementation returns nullptr
        // Specializations are provided for specific interface types
        return nullptr;
    }

    // ========================================================================
    // Phase 2: INodeFacadeV2 Implementation Methods
    // ========================================================================

    /**
     * @implements INodeFacade::GetMetadata()
     * 
     * Returns complete node metadata (name, type, description, ports) in a single call.
     * Replaces multiple GetName(), GetType(), Get*PortCount(), Get*PortName() calls.
     */
    INodeFacade::NodeMetadata GetMetadata() const override;

    /**
     * @implements INodeFacade::GetInterface(const std::string&)
     * 
     * Returns optional capability interfaces by name:
     * - "csv_injection" → IDataInjectionSource
     * - "metrics_callback" → IMetricsCallbackProvider
     * - "completion_callback" → CompletionCallback
     * 
     * Returns nullptr if interface not supported.
     */
    std::shared_ptr<void> GetInterface(const std::string& name) const override;

    /**
     * @implements INodeFacade::GetCSVInterface()
     * 
     * Returns typed interface for CSV data injection if supported.
     * Convenience wrapper around GetInterface("csv_injection").
     */
    std::shared_ptr<void> GetCSVInterface() const override;

    // ========================================================================
    // Phase 3: Convenience Methods for Port Names
    // ========================================================================

    /**
     * @implements INodeFacade::GetInputPortNames()
     * 
     * Convenience method returning all input port names.
     * Equivalent to extracting names from GetMetadata().input_ports vector,
     * but simpler when you only need the names.
     */
    std::vector<std::string> GetInputPortNames() const override;

    /**
     * @implements INodeFacade::GetOutputPortNames()
     * 
     * Convenience method returning all output port names.
     * Equivalent to extracting names from GetMetadata().output_ports vector,
     * but simpler when you only need the names.
     */
    std::vector<std::string> GetOutputPortNames() const override;

    // ========================================================================
    // Phase 3: Typed Interface Access Template
    // ========================================================================

    /**
     * Get a typed interface for optional capabilities with compile-time type safety.
     *
     * This template method provides type-safe access to plugin-specific interfaces,
     * replacing the unsafe void* casting pattern used in earlier phases.
     *
     * @tparam InterfaceT The target interface type (e.g., IDataInjectionSource)
     * @param name Interface name (e.g., "csv_injection", "metrics_callback")
     * @return shared_ptr<InterfaceT> to interface (nullptr if not supported or type mismatch)
     * @thread-safe for reading
     *
     * @example
     * ```cpp
     * // Typed interface access - compile-time safe
     * if (auto csv = adapter->GetInterface<IDataInjectionSource>("csv_injection")) {
     *     csv->InjectData(port_id, data);  // ✅ Type-safe, no casting
     * }
     * 
     * // Non-existent interface returns nullptr
     * auto metrics = adapter->GetInterface<IMetricsProvider>("nonexistent");
     * EXPECT_EQ(metrics, nullptr);
     * ```
     */
    template <typename InterfaceT>
    std::shared_ptr<InterfaceT> GetInterface(const std::string& name) const {
        // Get the void* interface
        auto void_ptr = GetInterface(name);
        if (!void_ptr) {
            return nullptr;
        }
        // Type-safe cast to requested interface type
        return std::static_pointer_cast<InterfaceT>(void_ptr);
    }
};

void DisplayNodeFacadeAdapter(std::shared_ptr<NodeFacadeAdapter> adapter);

}  // namespace graph

