/**
 * @file Nodes.hpp
 * @brief GraphX source file.
 */

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

/**
 * @file Nodes.hpp
 * @brief Active Graph Node Framework - Type-safe dataflow processing nodes
 *
 * This file provides a compile-time type-safe framework for building dataflow graphs
 * with strongly-typed input and output ports. The framework supports:
 *
 * - **Source Nodes**: Produce data on output ports
 * - **Sink Nodes**: Consume data on input ports
 * - **Interior Nodes**: Transform data from input to output ports
 * - **Edges**: Connect output ports to input ports with type checking
 *
 * Key Features:
 * - Compile-time type safety for port connections
 * - Runtime reflection for port inspection
 * - Thread-safe queuing between nodes
 * - Backpressure support via bounded queues
 *
 * @author Robert Klinkhammer
 * @date 2025-11-29
 */

#pragma once

#include <cstddef>
#include <atomic>
#include <optional>
#include <span>
#include <tuple>
#include <memory>
#include <utility>
#include <string>
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <future>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <cstdio>
#include <string_view>
#include <log4cxx/logger.h>
#include <string_view>
#include <type_traits>
#include "core/TypeInfo.hpp"
#include "core/ActiveQueue.hpp"
#include "graph/Message.hpp"
#include "graph/PortSpec.hpp"
#include "graph/PortTypes.hpp"
#include "graph/ThreadMetrics.hpp"
#include "graph/INode.hpp"
#include "graph/NamedType.hpp"
#include "graph/Lifecycle.hpp"
#include "graph/IFnBase.hpp"
#include "graph/InputFunction.hpp"
#include "graph/OutputFunction.hpp"
#include "graph/TransferFunction.hpp"
#include "graph/MergeFunction.hpp"

namespace graph
{

    
    // Port types, metrics, and other types now extracted to separate headers

    // Port Function Interfaces - The Core Dataflow API
    // Now extracted to separate headers for better modularity

    // Node Base Classes - Building Blocks for Dataflow Graphs
    // -----------------------------------------------------------------------------------
    // This section defines the node hierarchy:
    //
    // INode: Abstract base for all nodes (lifecycle, port access)
    // SourceNodeBase<Outputs...>: Nodes with only output ports (data producers)
    // SinkNodeBase<Inputs...>: Nodes with only input ports (data consumers)
    // InteriorNodeBase<Inputs..., Outputs...>: Nodes with both (transformers)
    //
    // All nodes support:
    // - Init/Start/Stop/Join lifecycle
    // - Runtime port introspection
    // - Edge registration for graph topology tracking
    // ===================================================================================

    /**
     * @brief Abstract base class for all graph nodes
     *
     * INode defines the common interface that all nodes must implement.
     * It provides lifecycle management, port introspection, and graph
     * topology tracking capabilities.
     */
    // ===================================================================================
    // Extracted Sections - See Separate Headers
    // ===================================================================================
    // - INode: Defined in nodes/INode.hpp
    // - NamedType: Defined in nodes/NamedType.hpp  
    // - NodeLifecycleMixin: Defined in nodes/Lifecycle.hpp
    // ===================================================================================

    template <typename Outputs>
    class SourceNodeBase;

    template <typename... Outputs>
/**
 * @class SourceNodeBase
 * @brief Source node base implementation for GraphX.
 */
    class SourceNodeBase<TypeList<Outputs...>>
        : public NodeLifecycleMixin<SourceNodeBase<TypeList<Outputs...>>>,
          public OutputFn<Outputs>...
    {
    public:
        using OutputFn<Outputs>::Produce...;

        static consteval auto build_outputs()
        {
            return build_port_table<PortDirection::Output>(TypeList<Outputs...>{});
        }
        static constexpr auto output_table = build_outputs();

        virtual std::span<const PortInfo> OutputPorts() const final { return output_table; }

        template <std::size_t PortID>
        using OutputType = typename std::tuple_element<PortID, std::tuple<Outputs...>>::type::type;

        template <std::size_t PortID>
        using OutputPortType = typename std::tuple_element<PortID, std::tuple<Outputs...>>::type;

        static constexpr std::size_t NOutputs = sizeof...(Outputs);
        virtual ~SourceNodeBase() {
            auto logger = log4cxx::Logger::getLogger("SourceNodeBase");
            LOG4CXX_TRACE(logger, "Destroying SourceNodeBase");
        }

        virtual LifecycleState GetLifecycleState() const override {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port GetLifecycleState`.");
            return this->GetLifecycleStateImpl();
        }
  
        virtual bool Init() override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port Init.");
            return this->InitImpl();
        }

        virtual bool Start() override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port Start.");
            return this->StartImpl();
        }

        virtual void Stop() override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port Stop.");
            this->StopImpl();
        }

        virtual void Join() override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port Join.");
            this->JoinImpl();
        }

        virtual bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port JoinWithTimeout.");
            return this->JoinWithTimeoutImpl(timeout_ms);
        }

        // Get output queue size for a specific port
        std::size_t GetOutputPortQueueSize(std::size_t port_id) const
        {
            std::size_t size = 0;
            GetPortQueueSizeImpl<0>(port_id, size);
            return size;
        }

        /// Get const reference to queue metrics for a specific output port
        /// Returns nullptr if port_id is invalid
        const core::QueueMetrics* GetOutputQueueMetrics(std::size_t port_id) const
        {
            const core::QueueMetrics* result = nullptr;
            GetOutputQueueMetricsImpl<0>(port_id, result);
            return result;
        }

        /// Get const reference to thread metrics for a specific output port
        /// Returns nullptr if port_id is invalid
        const ThreadMetrics* GetOutputPortThreadMetrics(std::size_t port_id) const
        {
            const ThreadMetrics* result = nullptr;
            GetOutputPortThreadMetricsImpl<0>(port_id, result);
            return result;
        }

        /// Enable metrics collection for all output ports
        void EnableMetrics(bool enabled = true)
        {
            (OutputFn<Outputs>::EnableMetrics(enabled), ...);
        }

        /// Disable metrics collection for all output ports
        void DisableMetrics()
        {
            EnableMetrics(false);
        }

        /// Reset metrics for all output ports
        void ResetMetrics()
        {
            ResetMetricsImpl<0>();
        }

        int GetOutputPortCount() const 
        {
            return NOutputs;
        }

        int GetInputPortCount() const 
        {
            return 0;
        }   

    private:
        template<std::size_t Port>
        void GetPortQueueSizeImpl(std::size_t port_id, std::size_t& size) const
        {
            if constexpr (Port < NOutputs) {
                if (port_id == Port) {
                    auto* mutable_this = const_cast<SourceNodeBase*>(this);
                    size = static_cast<OutputFn<typename std::tuple_element<Port, std::tuple<Outputs...>>::type> *>(mutable_this)->GetQueue().Size();
                } else {
                    GetPortQueueSizeImpl<Port + 1>(port_id, size);
                }
            }
        }

        template<std::size_t Port>
        void GetOutputQueueMetricsImpl(std::size_t port_id, const core::QueueMetrics*& result) const
        {
            if constexpr (Port < NOutputs) {
                if (port_id == Port) {
                    auto* mutable_this = const_cast<SourceNodeBase*>(this);
                    auto* fn = static_cast<OutputFn<typename std::tuple_element<Port, std::tuple<Outputs...>>::type> *>(mutable_this);
                    result = &fn->GetQueue().GetMetrics();
                } else {
                    GetOutputQueueMetricsImpl<Port + 1>(port_id, result);
                }
            }
        }

        template<std::size_t Port>
        void GetOutputPortThreadMetricsImpl(std::size_t port_id, const ThreadMetrics*& result) const
        {
            if constexpr (Port < NOutputs) {
                if (port_id == Port) {
                    auto* mutable_this = const_cast<SourceNodeBase*>(this);
                    auto* fn = static_cast<OutputFn<typename std::tuple_element<Port, std::tuple<Outputs...>>::type> *>(mutable_this);
                    result = &fn->GetThreadMetrics();
                } else {
                    GetOutputPortThreadMetricsImpl<Port + 1>(port_id, result);
                }
            }
        }

        template<std::size_t Port>
        void ResetMetricsImpl()
        {
            if constexpr (Port < NOutputs) {
                auto* mutable_this = const_cast<SourceNodeBase*>(this);
                auto* fn = static_cast<OutputFn<typename std::tuple_element<Port, std::tuple<Outputs...>>::type> *>(mutable_this);
                fn->ResetMetrics();
                ResetMetricsImpl<Port + 1>();
            }
        }

        friend class NodeLifecycleMixin<SourceNodeBase<TypeList<Outputs...>>>;

        bool InitPortsImpl()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port InitPortsImpl.");
            return (OutputFn<Outputs>::Init() && ...);
        }

        bool StartPortsImpl()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port StartPortsImpl.");
            // Enable all output queues before starting
            (OutputFn<Outputs>::EnableQueue(), ...);
            return (OutputFn<Outputs>::Start() && ...);
        }

        void StopPortsImpl()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port StopPortsImpl.");
            (OutputFn<Outputs>::DisableQueue(), ...);
            (OutputFn<Outputs>::Stop(), ...);
        }

        void JoinPortsImpl()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port JoinPortsImpl.");
            (OutputFn<Outputs>::Join(), ...);
        }

        bool JoinWithTimeoutPortsImpl(std::chrono::milliseconds timeout_ms)
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "SourceNodeBase joining with timeout " << timeout_ms.count() << "ms.");

            auto per_port_timeout = timeout_ms / std::max(static_cast<size_t>(1), NOutputs);

            bool all_ok = true;
            // Join each output port with its timeout budget
            (([this, per_port_timeout, &all_ok]()
              {
                if (!OutputFn<Outputs>::JoinWithTimeout(per_port_timeout)) {
                    all_ok = false;
                } }()),
             ...);

            return all_ok;
        }
    };

    /**
     * @brief Convenience class for defining source nodes
     * @tparam Outputs Data types for output ports (auto-assigned port IDs)
     *
     * SourceNode automatically creates Port<T, ID> types from a simple
     * type list, assigning sequential IDs starting from 0.
     *
     * Usage: class MySource : public SourceNode<int, double, std::string>
     */
    template <typename... Outputs>
/**
 * @class SourceNode
 * @brief SourceNode class.
 */
    class SourceNode : public SourceNodeBase<typename MakePorts<TypeList<Outputs...>>::type>
    {
    public:
        virtual ~SourceNode() = default;
    };

    template <typename Inputs>
    class SinkNodeBase;

    /**
     * @brief Base class for sink nodes (data consumers)
     * @tparam Inputs Variadic list of Port types for inputs
     *
     * SinkNodeBase provides the foundation for nodes that consume data.
     * These nodes have input ports but no output ports. Examples include:
     * - File writers
     * - Display/visualization
     * - Network data sinks
     * - Data validators
     *
     * Derive from SinkNode<T1, T2, ...> to create a sink with typed inputs.
     * Implement Consume() for each input port to process received data.
     *
     * Example:
     * @code
     *   class MyPrinter : public SinkNode<int, std::string> {
     *     bool Consume(const int& val, std::integral_constant<size_t, 0>) override {
     *       std::cout << "Int: " << val << std::endl;
     *       return true;
     *     }
     *     bool Consume(const std::string& val, std::integral_constant<size_t, 1>) override {
     *       std::cout << "String: " << val << std::endl;
     *       return true;
     *     }
     *   };
     * @endcode
     */
    template <typename... Inputs>
/**
 * @class SinkNodeBase
 * @brief Sink node base implementation for GraphX.
 */
    class SinkNodeBase<TypeList<Inputs...>>
        : public NodeLifecycleMixin<SinkNodeBase<TypeList<Inputs...>>>,
          public InputFn<Inputs>...
    {
    public:
        using InputFn<Inputs>::Consume...;

        static consteval auto build_inputs()
        {
            return build_port_table<PortDirection::Input>(TypeList<Inputs...>{});
        }
        static constexpr auto input_table = build_inputs();

        virtual std::span<const PortInfo> InputPorts() const final { return input_table; }

        template <std::size_t PortID>
        using InputType = typename std::tuple_element<PortID, std::tuple<Inputs...>>::type::type;

        template <std::size_t PortID>
        using InputPortType = typename std::tuple_element<PortID, std::tuple<Inputs...>>::type;

        static constexpr std::size_t NInputs = sizeof...(Inputs);
        virtual ~SinkNodeBase() {
            auto logger = log4cxx::Logger::getLogger("SinkNodeBase");
            LOG4CXX_TRACE(logger, "Destroying SinkNodeBase");
        }

        virtual LifecycleState GetLifecycleState() const override {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port GetLifecycleState`.");
            return this->GetLifecycleStateImpl();
        }
        
        virtual bool Init() override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port Init`.");
            return this->InitImpl();
        }

        virtual bool Start() override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port Start`.");
            return this->StartImpl();
        }

        virtual void Stop() override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port Stop`.");
            this->StopImpl();
        }

        virtual void Join() override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port Join`.");
            this->JoinImpl();
        }

        virtual bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port JoinWithTimeout`.");
            return this->JoinWithTimeoutImpl(timeout_ms);
        }

        int GetInputPortCount() const 
        {
            return NInputs;
        }

        int GetOutputPortCount() const 
        {
            return 0;
        }

        /// Get const reference to queue metrics for a specific input port
        /// Returns nullptr if port_id is invalid
        const core::QueueMetrics* GetInputQueueMetrics(std::size_t port_id) const
        {
            const core::QueueMetrics* result = nullptr;
            GetInputQueueMetricsImpl<0>(port_id, result);
            return result;
        }

        /// Get const reference to thread metrics for a specific input port
        /// Returns nullptr if port_id is invalid
        const ThreadMetrics* GetInputPortThreadMetrics(std::size_t port_id) const
        {
            const ThreadMetrics* result = nullptr;
            GetInputPortThreadMetricsImpl<0>(port_id, result);
            return result;
        }

        /// Enable metrics collection for all input ports
        void EnableMetrics(bool enabled = true)
        {
            (InputFn<Inputs>::EnableMetrics(enabled), ...);
        }

        /// Disable metrics collection for all input ports
        void DisableMetrics()
        {
            EnableMetrics(false);
        }

        /// Reset metrics for all input ports
        void ResetMetrics()
        {
            ResetMetricsImpl<0>();
        }

    private:
        template<std::size_t Port>
        void GetInputQueueMetricsImpl(std::size_t port_id, const core::QueueMetrics*& result) const
        {
            if constexpr (Port < NInputs) {
                if (port_id == Port) {
                    auto* mutable_this = const_cast<SinkNodeBase*>(this);
                    auto* fn = static_cast<InputFn<typename std::tuple_element<Port, std::tuple<Inputs...>>::type> *>(mutable_this);
                    result = &fn->GetQueue().GetMetrics();
                } else {
                    GetInputQueueMetricsImpl<Port + 1>(port_id, result);
                }
            }
        }

        template<std::size_t Port>
        void GetInputPortThreadMetricsImpl(std::size_t port_id, const ThreadMetrics*& result) const
        {
            if constexpr (Port < NInputs) {
                if (port_id == Port) {
                    auto* mutable_this = const_cast<SinkNodeBase*>(this);
                    auto* fn = static_cast<InputFn<typename std::tuple_element<Port, std::tuple<Inputs...>>::type> *>(mutable_this);
                    result = &fn->GetThreadMetrics();
                } else {
                    GetInputPortThreadMetricsImpl<Port + 1>(port_id, result);
                }
            }
        }

        template<std::size_t Port>
        void ResetMetricsImpl()
        {
            if constexpr (Port < NInputs) {
                auto* mutable_this = const_cast<SinkNodeBase*>(this);
                auto* fn = static_cast<InputFn<typename std::tuple_element<Port, std::tuple<Inputs...>>::type> *>(mutable_this);
                fn->ResetMetrics();
                ResetMetricsImpl<Port + 1>();
            }
        }

        friend class NodeLifecycleMixin<SinkNodeBase<TypeList<Inputs...>>>;

        bool InitPortsImpl()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port InitPortsImpl`.");
            return (InputFn<Inputs>::Init() && ...);
        }

        bool StartPortsImpl()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port StartPortsImpl`.");
            // Enable all input queues before starting
            (InputFn<Inputs>::EnableQueue(), ...);
            return (InputFn<Inputs>::Start() && ...);
        }

        void StopPortsImpl()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port StopPortsImpl`.");
            (InputFn<Inputs>::DisableQueue(), ...);
            (InputFn<Inputs>::Stop(), ...);
        }

        void JoinPortsImpl()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port JoinPortsImpl`.");
            (InputFn<Inputs>::Join(), ...);
        }

        bool JoinWithTimeoutPortsImpl(std::chrono::milliseconds timeout_ms)
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "SinkNodeBase joining with timeout " << timeout_ms.count() << "ms.");

            auto per_port_timeout = timeout_ms / std::max(static_cast<size_t>(1), NInputs);

            bool all_ok = true;
            // Join each input port with its timeout budget
            (([this, per_port_timeout, &all_ok]()
              {
                if (!InputFn<Inputs>::JoinWithTimeout(per_port_timeout)) {
                    all_ok = false;
                } }()),
             ...);

            return all_ok;
        }
    };

    /**
     * @brief Convenience class for defining sink nodes
     * @tparam Inputs Data types for input ports (auto-assigned port IDs)
     *
     * SinkNode automatically creates Port<T, ID> types from a simple
     * type list, assigning sequential IDs starting from 0.
     *
     * Usage: class MySink : public SinkNode<int, double, std::string>
     */
    template <typename... Inputs>
/**
 * @class SinkNode
 * @brief SinkNode class.
 */
    class SinkNode : public SinkNodeBase<typename MakePorts<TypeList<Inputs...>>::type>
    {
    public:
        virtual ~SinkNode() = default;
   };

    template <typename Inputs, typename Outputs>
    class InteriorNodeBase;

    /**
     * @brief Base class for interior nodes (transformers)
     * @tparam Inputs Variadic list of Port types for inputs
     * @tparam Outputs Variadic list of Port types for outputs
     *
     * InteriorNodeBase provides the foundation for nodes that transform data.
     * These nodes have both input and output ports. Examples include:
     * - Filters and signal processors
     * - Data format converters
     * - Aggregators and splitters
     * - Protocol translators
     *
     * Interior nodes use TransferFn to connect input ports to output ports.
     * Each Transfer() method processes data from one input and produces output.
     *
     * Example:
     * @code
     *   // Node that squares integers and doubles strings
     *   class MyTransform : public InteriorNode<TypeList<int, std::string>,
     *                                            TypeList<int, std::string>> {
     *     std::optional<int> Transfer(const int& in,
     *                                  std::integral_constant<size_t, 0>,
     *                                  std::integral_constant<size_t, 0>) override {
     *       return in * in; // Square the input
     *     }
     *     // ... implement other Transfer overloads
     *   };
     * @endcode
     */
    template <typename... Inputs, typename... Outputs>
/**
 * @class InteriorNodeBase
 * @brief Interior node base implementation for GraphX.
 */
    class InteriorNodeBase<TypeList<Inputs...>, TypeList<Outputs...>>
        : public NodeLifecycleMixin<InteriorNodeBase<TypeList<Inputs...>, TypeList<Outputs...>>>,
          public TransferFn<Inputs, Outputs>...
    {
    public:
        using TransferFn<Inputs, Outputs>::Transfer...;

        static consteval auto build_inputs() { return build_port_table<PortDirection::Input>(TypeList<Inputs...>{}); }
        static consteval auto build_outputs() { return build_port_table<PortDirection::Output>(TypeList<Outputs...>{}); }

        static constexpr auto input_table = build_inputs();
        static constexpr auto output_table = build_outputs();

        virtual std::span<const PortInfo> InputPorts() const final { return input_table; }
        virtual std::span<const PortInfo> OutputPorts() const final { return output_table; }

        template <std::size_t PortID>
        using InputType = typename std::tuple_element<PortID, std::tuple<Inputs...>>::type::type;
        template <std::size_t PortID>
        using OutputType = typename std::tuple_element<PortID, std::tuple<Outputs...>>::type::type;

        template <std::size_t PortID>
        using InputPortType = typename std::tuple_element<PortID, std::tuple<Inputs...>>::type;
        template <std::size_t PortID>
        using OutputPortType = typename std::tuple_element<PortID, std::tuple<Outputs...>>::type;

        static constexpr std::size_t NInputs = sizeof...(Inputs);
        static constexpr std::size_t NOutputs = sizeof...(Outputs);

        virtual ~InteriorNodeBase() = default;

        virtual LifecycleState GetLifecycleState() const override{
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port GetLifecycleState`.");
            return this->GetLifecycleStateImpl();
        }
  
        virtual bool Init() override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port Init`.");
            return this->InitImpl();
        }

        virtual bool Start() override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port Start`.");
            return this->StartImpl();
        }

        virtual void Stop() override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port Stop`.");
            this->StopImpl();
        }

        virtual void Join() override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port Join`.");
            this->JoinImpl();
        }

        virtual bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port JoinWithTimeout`.");
            return this->JoinWithTimeoutImpl(timeout_ms);
        }

        int GetInputPortCount() const 
        {
            return NInputs;
        }

        int GetOutputPortCount() const 
        {
            return NOutputs;
        }

        /// Get input queue size for a specific port
        /// Returns the current number of items in the input port's queue
        /// @param port_id Port identifier (0 to NInputs-1)
        /// @return Current queue size, or 0 if port_id is invalid
        std::size_t GetInputPortQueueSize(std::size_t port_id) const
        {
            std::size_t size = 0;
            GetInputPortQueueSizeImpl<0>(port_id, size);
            return size;
        }

        /// Get output queue size for a specific port
        std::size_t GetOutputPortQueueSize(std::size_t port_id) const
        {
            std::size_t size = 0;
            GetPortQueueSizeImpl<0>(port_id, size);
            return size;
        }

        /// Get const reference to queue metrics for a specific input port
        /// Returns nullptr if port_id is invalid
        const core::QueueMetrics* GetInputQueueMetrics(std::size_t port_id) const
        {
            const core::QueueMetrics* result = nullptr;
            GetInputQueueMetricsImpl<0>(port_id, result);
            return result;
        }

        /// Get const reference to queue metrics for a specific output port
        /// Returns nullptr if port_id is invalid
        const core::QueueMetrics* GetOutputQueueMetrics(std::size_t port_id) const
        {
            const core::QueueMetrics* result = nullptr;
            GetOutputQueueMetricsImpl<0>(port_id, result);
            return result;
        }

        /// Get const reference to thread metrics for a specific input port
        /// Returns nullptr if port_id is invalid
        const ThreadMetrics* GetInputPortThreadMetrics(std::size_t port_id) const
        {
            const ThreadMetrics* result = nullptr;
            GetInputPortThreadMetricsImpl<0>(port_id, result);
            return result;
        }

        /// Get const reference to thread metrics for a specific output port
        /// Returns nullptr if port_id is invalid
        const ThreadMetrics* GetOutputPortThreadMetrics(std::size_t port_id) const
        {
            const ThreadMetrics* result = nullptr;
            GetOutputPortThreadMetricsImpl<0>(port_id, result);
            return result;
        }

        /// Enable metrics collection for input ports only
        /// @param enabled true to enable, false to disable
        void EnableInputMetrics(bool enabled = true)
        {
            (TransferFn<Inputs, Outputs>::EnableInputMetrics(enabled), ...);
        }

        /// Enable metrics collection for output ports only
        /// @param enabled true to enable, false to disable
        void EnableOutputMetrics(bool enabled = true)
        {
            (TransferFn<Inputs, Outputs>::EnableOutputMetrics(enabled), ...);
        }

        /// Disable metrics collection for input ports
        void DisableInputMetrics()
        {
            EnableInputMetrics(false);
        }

        /// Disable metrics collection for output ports
        void DisableOutputMetrics()
        {
            EnableOutputMetrics(false);
        }

        /// Enable metrics collection for all transfer ports (both input and output)
        void EnableMetrics(bool enabled = true)
        {
            EnableInputMetrics(enabled);
            EnableOutputMetrics(enabled);
        }

        /// Disable metrics collection for all transfer ports
        void DisableMetrics()
        {
            EnableMetrics(false);
        }

        /// Reset metrics for all transfer ports
        void ResetMetrics()
        {
            ResetMetricsImpl<0>();
        }

    private:
        
        template<std::size_t Port>
        void GetInputPortQueueSizeImpl(std::size_t port_id, std::size_t& size) const
        {
            if constexpr (Port < NInputs) {
                if (port_id == Port) {
                    auto* mutable_this = const_cast<InteriorNodeBase*>(this);
                    auto* fn = static_cast<TransferFn<typename std::tuple_element<Port, std::tuple<Inputs...>>::type,
                                                       typename std::tuple_element<Port, std::tuple<Outputs...>>::type> *>(mutable_this);
                    size = fn->GetInputQueue().Size();
                } else {
                    GetInputPortQueueSizeImpl<Port + 1>(port_id, size);
                }
            }
        }

        template<std::size_t Port>
        void GetPortQueueSizeImpl(std::size_t port_id, std::size_t& size) const
        {
            if constexpr (Port < NOutputs) {
                if (port_id == Port) {
                    auto* mutable_this = const_cast<InteriorNodeBase*>(this);
                    size = static_cast<TransferFn<typename std::tuple_element<Port, std::tuple<Inputs...>>::type,
                                                       typename std::tuple_element<Port, std::tuple<Outputs...>>::type> *>(mutable_this)->GetOutputQueue().Size();
                } else {
                    GetPortQueueSizeImpl<Port + 1>(port_id, size);
                }
            }
        }

        template<std::size_t Port>
        void GetInputQueueMetricsImpl(std::size_t port_id, const core::QueueMetrics*& result) const
        {
            if constexpr (Port < NInputs) {
                if (port_id == Port) {
                    auto* mutable_this = const_cast<InteriorNodeBase*>(this);
                    auto* fn = static_cast<TransferFn<typename std::tuple_element<Port, std::tuple<Inputs...>>::type,
                                                       typename std::tuple_element<Port, std::tuple<Outputs...>>::type> *>(mutable_this);
                    result = &fn->GetInputQueue().GetMetrics();
                } else {
                    GetInputQueueMetricsImpl<Port + 1>(port_id, result);
                }
            }
        }

        template<std::size_t Port>
        void GetOutputQueueMetricsImpl(std::size_t port_id, const core::QueueMetrics*& result) const
        {
            if constexpr (Port < NOutputs) {
                if (port_id == Port) {
                    auto* mutable_this = const_cast<InteriorNodeBase*>(this);
                    auto* fn = static_cast<TransferFn<typename std::tuple_element<Port, std::tuple<Inputs...>>::type,
                                                       typename std::tuple_element<Port, std::tuple<Outputs...>>::type> *>(mutable_this);
                    result = &fn->GetOutputQueue().GetMetrics();
                } else {
                    GetOutputQueueMetricsImpl<Port + 1>(port_id, result);
                }
            }
        }

        template<std::size_t Port>
        void GetInputPortThreadMetricsImpl(std::size_t port_id, const ThreadMetrics*& result) const
        {
            if constexpr (Port < NInputs) {
                if (port_id == Port) {
                    auto* mutable_this = const_cast<InteriorNodeBase*>(this);
                    auto* fn = static_cast<TransferFn<typename std::tuple_element<Port, std::tuple<Inputs...>>::type,
                                                       typename std::tuple_element<Port, std::tuple<Outputs...>>::type> *>(mutable_this);
                    result = &fn->GetInputMetrics();
                } else {
                    GetInputPortThreadMetricsImpl<Port + 1>(port_id, result);
                }
            }
        }

        template<std::size_t Port>
        void GetOutputPortThreadMetricsImpl(std::size_t port_id, const ThreadMetrics*& result) const
        {
            if constexpr (Port < NOutputs) {
                if (port_id == Port) {
                    auto* mutable_this = const_cast<InteriorNodeBase*>(this);
                    auto* fn = static_cast<TransferFn<typename std::tuple_element<Port, std::tuple<Inputs...>>::type,
                                                       typename std::tuple_element<Port, std::tuple<Outputs...>>::type> *>(mutable_this);
                    result = &fn->GetOutputMetrics();
                } else {
                    GetOutputPortThreadMetricsImpl<Port + 1>(port_id, result);
                }
            }
        }

        template<std::size_t Port>
        void ResetMetricsImpl()
        {
            if constexpr (Port < NInputs) {
                auto* mutable_this = const_cast<InteriorNodeBase*>(this);
                auto* fn = static_cast<TransferFn<typename std::tuple_element<Port, std::tuple<Inputs...>>::type,
                                                   typename std::tuple_element<Port, std::tuple<Outputs...>>::type> *>(mutable_this);
                fn->ResetMetrics();
                ResetMetricsImpl<Port + 1>();
            }
        }

        friend class NodeLifecycleMixin<InteriorNodeBase<TypeList<Inputs...>, TypeList<Outputs...>>>;

        bool InitPortsImpl()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port InitPortsImpl`.");
            return (TransferFn<Inputs, Outputs>::Init() && ...);
        }

        bool StartPortsImpl()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port StartPortsImpl`.");
            // Enable all input and output queues before starting
            (TransferFn<Inputs, Outputs>::EnableQueues(), ...);
            return (TransferFn<Inputs, Outputs>::Start() && ...);
        }

        void StopPortsImpl()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port StopPortsImpl`.");
            (TransferFn<Inputs, Outputs>::DisableQueues(), ...);
            (TransferFn<Inputs, Outputs>::Stop(), ...);
        }

        void JoinPortsImpl()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port JoinPortsImpl`.");
            (TransferFn<Inputs, Outputs>::Join(), ...);
        }

        bool JoinWithTimeoutPortsImpl(std::chrono::milliseconds timeout_ms)
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "InteriorNodeBase joining with timeout " << timeout_ms.count() << "ms.");

            auto num_transfer_ports = sizeof...(Inputs); // Number of transfer paths
            auto per_port_timeout = timeout_ms / std::max(static_cast<size_t>(1), num_transfer_ports);

            bool all_ok = true;
            // Join each transfer port with its timeout budget
            (([this, per_port_timeout, &all_ok]()
              {
                if (!TransferFn<Inputs, Outputs>::JoinWithTimeout(per_port_timeout)) {
                    all_ok = false;
                } }()),
             ...);

            return all_ok;
        }
    };

    /**
     * @brief Convenience class for defining interior nodes
     * @tparam InputList TypeList of input data types
     * @tparam OutputList TypeList of output data types
     *
     * InteriorNode automatically creates Port types with sequential IDs.
     *
     * Usage:
     * @code
     *   class MyNode : public InteriorNode<TypeList<int, double>,
     *                                       TypeList<int, double>>
     * @endcode
     */
    template <typename InputList, typename OutputList>
/**
 * @class InteriorNode
 * @brief InteriorNode class.
 */
    class InteriorNode
        : public InteriorNodeBase<typename MakePorts<InputList>::type, typename MakePorts<OutputList>::type>
    {
    public:
        virtual ~InteriorNode() = default;
    };

    // ===================================================================================
    // Edge<TSrc,SrcPort,TDst,DstPort>
    // -----------------------------------------------------------------------------------
    // - Holds a deque<Message> as the queue between producer and consumer.
    // - TryProduce asks src->Produce; if value available and queue has room, wraps value in Message and pushes.
    // - TryConsume peeks front Message, checks type, and passes by reference to dst->Consume.
    // - All queue operations are protected by an internal mutex.
    // ===================================================================================

    /**
     * @brief Type-safe edge connecting two nodes
     * @tparam SrcNode Source node type
     * @tparam SrcPort Source output port ID
     * @tparam DstNode Destination node type
     * @tparam DstPort Destination input port ID
     *
     * Edge provides a typed connection between nodes, transferring data from
     * a source output port to a destination input port. The connection is
     * type-checked at compile time to ensure compatibility.
     *
     * Features:
     * - Runs in its own thread to decouple producer and consumer
     * - Validates type compatibility at compile time
     * - Provides Init/Start/Stop/Join lifecycle
     * - Integrated logging for debugging
     *
     * Example:
     * @code
     *   auto src = std::make_shared<MySource>();
     *   auto dst = std::make_shared<MySink>();
     *   Edge<MySource, 0, MySink, 0> edge(src, dst);
     *   edge.Init();
     *   edge.Start();
     * @endcode
     */
    template <typename SrcNode, std::size_t SrcPort, typename DstNode, std::size_t DstPort>
/**
 * @class Edge
 * @brief Edge class.
 */
    class Edge
    {
    public:
        using ValueType = typename SrcNode::template OutputType<SrcPort>; ///< Data type flowing through this edge
        using DstPortType = typename DstNode::template InputPortType<DstPort>;

        // Static assert: DstNode must inherit from either IInputFn or IInputCommonFn for the port type
        // - IInputFn: Used by regular input ports (per-port queues, per-port threads)
        // - IInputCommonFn: Used by MergeNode input ports (shared unified queue, single thread)
        static_assert(
            std::is_base_of_v<IInputFn<DstPortType>, DstNode> ||
            std::is_base_of_v<IInputCommonFn<DstPortType>, DstNode>,
            "DstNode must inherit from IInputFn or IInputCommonFn for the specified port type");
 
        Edge(std::shared_ptr<SrcNode> src, std::shared_ptr<DstNode> dst, std::size_t buffer_size = 8)
            : src_(std::move(src)),
              dst_(std::move(dst)),
              buffer_size_(buffer_size)
        {
        }

        virtual ~Edge() = default;
    
        virtual bool Init()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"), "Edge from port " << SrcPort << " to port " << DstPort << " Init.");
            return true;
        }

        virtual bool Start()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"), "Edge from port " << SrcPort << " to port " << DstPort << " Start.");
            
            // Perform type checking once at startup (not in hot loop)
            // This determines which enqueue path we'll use for all subsequent iterations
            //using InputPortType = typename DstNode::template InputPortType<DstPort>;
            
            // Try IInputFn first (regular input ports with per-port queues)
            enqueue_fn_iinput_ = dynamic_cast<IInputFn<DstPortType> *>(dst_.get());
            if (enqueue_fn_iinput_) {
                LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"),
                             "Edge from port " << SrcPort << " to port " << DstPort << " using IInputFn enqueue path.");
            } else {
                // Try IInputCommonFn (MergeNode with unified queue)
                enqueue_fn_common_ = dynamic_cast<IInputCommonFn<DstPortType> *>(dst_.get());
                if (enqueue_fn_common_) {
                    LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"),
                                 "Edge from port " << SrcPort << " to port " << DstPort << " using IInputCommonFn enqueue path.");
                } else {
                    LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.edge"),
                                 "Edge failed to resolve input interface for port " << DstPort);
                    return false;
                }
            }
            
            // Launch edge thread with type-specific enqueue path already determined
            auto t = [this]() -> void {
                EdgeThreadFunc();
            };
            thread_ = std::thread(t);
            return true;
        }

        virtual void Stop()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"), "Edge from port " << SrcPort << " to port " << DstPort << " Stop.");
            stop_requested_.store(true, std::memory_order_release);
            src_->Stop();
            dst_->Stop();
        }

        virtual void Join()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"), "Edge from port " << SrcPort << " to port " << DstPort << " Join.");
            if (thread_.joinable())
            {
                thread_.join();
            }
        }

        /**
         * @brief Join with timeout
         * @param timeout_ms Maximum milliseconds to wait for edge thread
         * @return true if thread completed within timeout, false if timeout exceeded
         *
         * NOTE: This is a best-effort non-blocking implementation.
         * If the timeout expires and the thread is still running, this function
         * returns false WITHOUT joining the thread (to avoid blocking indefinitely).
         * The thread will eventually complete when the system is idle, or will be
         * detached when the Edge object is destroyed.
         */
        bool JoinWithTimeout(std::chrono::milliseconds timeout_ms)
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"), "Edge from port " << SrcPort << " to port " << DstPort << " JoinWithTimeout.");
            if (!thread_.joinable())
                return true;

            std::unique_lock lock(mtx_);
            if (!cv_.wait_for(lock, timeout_ms, [&] { return stop_requested_.load(std::memory_order_acquire); }))
            {
                LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"), "Thread did not finish within timeout");
                return false;
            }

            thread_.join();
            return true;
        }

        std::shared_ptr<INode> source_ptr() const noexcept { return src_; }
        std::shared_ptr<INode> dest_ptr() const noexcept { return dst_; }
        std::size_t src_port() const noexcept { return SrcPort; }
        std::size_t dst_port() const noexcept { return DstPort; }

        /// Get thread metrics for this edge (transfer operations)
        /// @return Const reference to ThreadMetrics (valid until edge destroyed)
        const ThreadMetrics& GetThreadMetrics() const {
            return thread_metrics_;
        }

        /// Reset all collected metrics
        void ResetMetrics() {
            thread_metrics_.total_iterations.store(0, std::memory_order_release);
            thread_metrics_.produce_calls.store(0, std::memory_order_release);
            thread_metrics_.consume_calls.store(0, std::memory_order_release);
            thread_metrics_.transfer_calls.store(0, std::memory_order_release);
            thread_metrics_.total_produce_time_ns.store(0, std::memory_order_release);
            thread_metrics_.total_consume_time_ns.store(0, std::memory_order_release);
            thread_metrics_.total_transfer_time_ns.store(0, std::memory_order_release);
            thread_metrics_.total_idle_time_ns.store(0, std::memory_order_release);
        }

        //...
    private:
        void EdgeThreadFunc()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"), "Edge from port " << SrcPort << " to port " << DstPort << " running.");
            
            // Mark thread as active
            thread_metrics_.thread_active.store(true, std::memory_order_release);
            
            while (!stop_requested_.load(std::memory_order_acquire))
            {
                // Measure transfer operation time (Dequeue + Enqueue)
                auto start_time = std::chrono::steady_clock::now();
                
                // Try to produce from source - call through IOutputFn interface
                auto maybe_value = static_cast<IOutputFn<typename SrcNode::template OutputPortType<SrcPort>> *>(src_.get())->Dequeue(std::integral_constant<std::size_t, SrcPort>{});
                
                auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                
                if (maybe_value.has_value())
                {
                    // Record transfer operation
                    thread_metrics_.transfer_calls.fetch_add(1, std::memory_order_acq_rel);
                    thread_metrics_.total_transfer_time_ns.fetch_add(duration_ns, std::memory_order_acq_rel);
                    
                    // HOT PATH: No dynamic_cast here - type was resolved in Start()
                    // Simply use the pre-determined interface pointer
                    bool enqueued = false;
                    if (enqueue_fn_iinput_) {
                        // Use IInputFn path (per-port queues)
                        enqueued = enqueue_fn_iinput_->Enqueue(maybe_value.value());
                    } else if (enqueue_fn_common_) {
                        // Use IInputCommonFn path (unified queue)
                        enqueued = enqueue_fn_common_->Enqueue(maybe_value.value());
                    } else {
                        // This should never happen - Start() would have failed
                        LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.edge"),
                                     "Edge from port " << SrcPort << " to port " << DstPort << " no enqueue function available.");
                        stop_requested_.store(true, std::memory_order_release);
                        break;
                    }
                    
                    if (!enqueued) {
                        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"),
                                     "Edge from port " << SrcPort << " to port " << DstPort << " failed to enqueue.");
                        stop_requested_.store(true, std::memory_order_release);
                        break;
                    }
                }
                else
                {
                    LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"),
                                 "Edge from port " << SrcPort << " to port " << DstPort << " Queue disabled.");
                    // No more data to produce
                    stop_requested_.store(true, std::memory_order_release);
                    break;
                }
                
                // Count iterations
                thread_metrics_.total_iterations.fetch_add(1, std::memory_order_acq_rel);
            }
            
            // Mark thread as inactive
            thread_metrics_.thread_active.store(false, std::memory_order_release);
            
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.edge"), "Edge from port " << SrcPort << " to port " << DstPort << " stopped.");
            cv_.notify_all();
       }

        std::shared_ptr<SrcNode> src_;
        std::shared_ptr<DstNode> dst_;
        std::size_t buffer_size_ = 8;                           ///< Queue buffer size from EdgeConfig
        std::atomic<bool> stop_requested_{false};
        std::thread thread_;
        std::mutex mtx_;
        std::condition_variable cv_;
        
        // Interface pointers resolved at startup (avoids dynamic_cast in hot loop)
        using InputPortType = typename DstNode::template InputPortType<DstPort>;
        IInputFn<InputPortType>* enqueue_fn_iinput_ = nullptr;           // For regular input ports
        IInputCommonFn<InputPortType>* enqueue_fn_common_ = nullptr;     // For MergeNode unified queue
        
        // Thread metrics member for recording transfer operations
        mutable ThreadMetrics thread_metrics_;
    };

    // ===================================================================================
    // MergeNodeBase - Multi-input merge node with unified queue
    // -----------------------------------------------------------------------------------
    // Combines N input ports (all CommonInput type) into single output port.
    // Uses unified queue and dedicated merge thread.
    // Follows NodeLifecycleMixin CRTP pattern for lifecycle management.
    // ===================================================================================

    /**
     * @brief Base class for merge nodes with N inputs and single output
     * @tparam N Number of input ports
     * @tparam CommonInput Type shared by all N input ports
     * @tparam OutputType Type of the single output port
     *
     * MergeNodeBase combines:
     * - N input ports of identical type (CommonInput)
     * - Single output port (OutputType)
     * - Unified merge queue receiving from all N input ports
     * - Dedicated merge processing thread
     * - NodeLifecycleMixin CRTP lifecycle management
     * - ExpandInputPorts pack expansion for N input port bases
     */
    template <std::size_t N, typename CommonInput, typename OutputT>
/**
 * @class MergeNodeBase
 * @brief MergeNodeBase class.
 */
    class MergeNodeBase
        : public IMergeFn<CommonInput, OutputT>,
          public ExpandInputPorts<N, CommonInput>::InputBases,
          public NodeLifecycleMixin<MergeNodeBase<N, CommonInput, OutputT>>,
          public IOutputFn<Port<OutputT, 0>>
    {
    public:
        using input_type = CommonInput;
        using output_type = OutputT;
        static constexpr std::size_t NInputs = N;
        static constexpr std::size_t NOutputs = 1;

        template <std::size_t PortID>
        using InputType = CommonInput;
        template <std::size_t PortID>
        using OutputType = OutputT;
    
        /// Constructor that initializes the unified queue and passes it to ExpandInputPorts
        explicit MergeNodeBase()
            : ExpandInputPorts<N, CommonInput>::InputBases(unified_queue_) {
        }

        virtual ~MergeNodeBase() {
            try {
                SetStopRequest(true);
                if (thread_.joinable()) {
                    thread_.join();
                }
            } catch (const std::exception& e) {
                LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.node"),
                              "MergeNodeBase cleanup failed: " << e.what());
            }
        }

        // ====================================================================
        // PUBLIC LIFECYCLE INTERFACE (from INode via NodeLifecycleMixin)
        // ====================================================================

        virtual LifecycleState GetLifecycleState() const override {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "MergeNodeBase GetLifecycleState.");
            return this->GetLifecycleStateImpl();
        }

        virtual bool Init() override {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "MergeNodeBase Init.");
            return this->InitImpl();
        }

        virtual bool Start() override {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "MergeNodeBase Start.");
            return this->StartImpl();
        }

        virtual void Stop() override {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "MergeNodeBase Stop.");
            this->StopImpl();
        }

        virtual void Join() override {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "MergeNodeBase Join.");
            this->JoinImpl();
        }

        virtual bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "MergeNodeBase JoinWithTimeout.");
            return this->JoinWithTimeoutImpl(timeout_ms);
        }

        // ====================================================================
        // PORT METADATA
        // ====================================================================

        static consteval auto build_inputs() {
            return ExpandInputPorts<N, CommonInput>::build_all_metadata();
        }

        static consteval auto build_outputs() {
            return build_port_table<PortDirection::Output>(
                TypeList<Port<OutputT, 0>>{});
        }

        static constexpr auto input_table = build_inputs();
        static constexpr auto output_table = build_outputs();

        virtual std::span<const PortInfo> InputPorts() const final {
            return input_table;
        }

        virtual std::span<const PortInfo> OutputPorts() const final {
            return output_table;
        }

        int GetOutputPortCount() const 
        {
            return 1;
        }

        int GetInputPortCount() const 
        {
            return N;
        }   

       void SetInputComparator(std::function<bool(const CommonInput &, const CommonInput &)> comp)
        {
            // Optional: set a comparator for merging logic
            unified_queue_.SetComparator(comp);
        }

        // void SetOutputComparator(std::function<bool(const OutputT &, const OutputT &)> comp)
        // {
        //     // Optional: set a comparator for merging logic
        //     //output_queue_.SetComparator(comp);
        // }

        // ====================================================================
        // TYPE ACCESSORS (for AddEdge type verification)
        // ====================================================================

        template <std::size_t PortID>
        using InputPortType = typename std::enable_if<
            PortID < N,
            Port<CommonInput, PortID>
        >::type;

        template <std::size_t PortID>
        using OutputPortType = typename std::enable_if<
            PortID == 0,
            Port<OutputT, 0>
        >::type;

    private:
        // ====================================================================
        // PRIVATE STATE
        // ====================================================================

        // Unified queue that all N input ports feed into
        core::ActiveQueue<CommonInput> unified_queue_;

        // Merge processing thread
        std::thread thread_;
        std::atomic<bool> stop_requested_{false};
        std::mutex mtx_;
        std::condition_variable cv_;

        // ====================================================================
        // LIFECYCLE IMPLEMENTATION (via NodeLifecycleMixin CRTP)
        // ====================================================================

        friend class NodeLifecycleMixin<MergeNodeBase<N, CommonInput, OutputT>>;

        bool InitPortsImpl() {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "MergeNodeBase InitPortsImpl: setting up unified queue with "
                          << N << " input ports.");
            
            // Initialize output port
            if (!IOutputFn<Port<OutputT, 0>>::Init()) {
                LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.node"),
                              "MergeNodeBase failed to init output port.");
                return false;
            }

            // Unified queue is managed internally, initialized implicitly
            return true;
        }

        bool StartPortsImpl() {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "MergeNodeBase StartPortsImpl: launching merge thread.");
            
            // Enable output queue
            IOutputFn<Port<OutputT, 0>>::EnableQueue();
            
            // Launch merge thread
            auto t = [this]() -> void {
                MergeThreadFunc();
            };
            thread_ = std::thread(t);
            return true;
        }

        void StopPortsImpl() {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "MergeNodeBase StopPortsImpl: signaling merge thread.");
            
            // Signal merge thread to stop
            SetStopRequest(true);
            unified_queue_.Disable();
            // Disable output queue
            IOutputFn<Port<OutputT, 0>>::DisableQueue();
            IOutputFn<Port<OutputT, 0>>::Stop();
        }

        void JoinPortsImpl() {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "MergeNodeBase JoinPortsImpl: waiting for merge thread.");
            
            // Wait for merge thread to exit
            if (thread_.joinable()) {
                thread_.join();
            }
            
            // Wait for output port
            //IOutputFn<Port<OutputT, 0>>::Join();
        }

        bool JoinWithTimeoutPortsImpl(std::chrono::milliseconds timeout_ms) {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "MergeNodeBase JoinWithTimeoutPortsImpl.");
            
            // Split timeout between merge thread and output port
            auto half = timeout_ms / 2;
            
            // Wait for merge thread with timeout
            if (thread_.joinable()) {
                // Wait with timeout using cv
                std::unique_lock<std::mutex> lock(mtx_);
                if (!cv_.wait_for(lock, half, [this] { return !thread_.joinable(); })) {
                    LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                                 "MergeNodeBase merge thread timeout exceeded.");
                    return false;
                }
            }
            
            // Wait for output port with remaining timeout
            return IOutputFn<Port<OutputT, 0>>::JoinWithTimeout(timeout_ms / 2);
        }

        bool IsStopRequested() const {
            return stop_requested_.load(std::memory_order_acquire);
        }

        void SetStopRequest(bool value) {
            stop_requested_.store(value, std::memory_order_release);
        }

        // ====================================================================
        // MERGE THREAD IMPLEMENTATION
        // ====================================================================

        void MergeThreadFunc() {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "MergeNodeBase MergeThreadFunc started.");
            
            while (!IsStopRequested()) {
                CommonInput event;
                
                // Dequeue from unified queue
                if (!unified_queue_.Dequeue(event)) {
                    // Queue disabled or empty with timeout
                    LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                                  "MergeNodeBase dequeue returned false, checking stop request.");
                    if (IsStopRequested()) {
                        break;
                    }
                    continue;  // Try again
                }

                // Call user's Process() method with integral_constant<0> for output port
                auto maybe_result = this->Process(event, std::integral_constant<std::size_t, 0>{});

                // If Process returned a value, enqueue to output
                if (maybe_result.has_value()) {
                    auto& output_queue = IOutputFn<Port<OutputT, 0>>::GetQueue();
                    if (!output_queue.Enqueue(maybe_result.value())) {
                        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                                     "MergeNodeBase output enqueue failed (queue disabled).");
                    }
                }
            }

            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                          "MergeNodeBase MergeThreadFunc exited.");
        }
    };

    // ===================================================================================
    // MergeNode - Convenience Template for MergeNodeBase
    // -----------------------------------------------------------------------------------
    // Provides a cleaner interface with 4 template parameters:
    // - N: Number of input ports
    // - InputType: Type of input messages
    // - OutputT: Type of output messages
    // - Derived: The derived node class (CRTP pattern)
    // 
    // This is just a type alias that maps to MergeNodeBase<N, InputType, OutputT>
    // The Derived class parameter is for CRTP but not used in the template itself.
    // ===================================================================================
    
    /**
     * @brief Convenience template for merge nodes
     * 
     * Template Parameters:
     * - N: Number of input ports
     * - InputType: Type of all input messages
     * - OutputT: Type of output messages  
     * - Derived: The derived class implementing this node (CRTP pattern)
     *
     * Usage:
     * @code
     *   class MyMerge : public MergeNode<3, Message, Result, MyMerge> {
     *       std::optional<Result> Process(const Message&, 
     *                                      std::integral_constant<0>) override {
     *           // Process and return result
     *       }
     *   };
     * @endcode
     */
    template <std::size_t N, typename InputType, typename OutputT, typename Derived>
/**
 * @class MergeNode
 * @brief MergeNode class.
 */
    class MergeNode : public MergeNodeBase<N, InputType, OutputT>, public NamedType<Derived> {
    public:
        virtual ~MergeNode() = default;

        /**
         * @brief Get input port metadata for visualization
         * 
         * Override in derived classes to provide runtime port information.
         * Used by the NodeSerializer template to export port list to JSON.
         * 
         * @return Vector of PortMetadata for all input ports
         */
        virtual std::vector<PortMetadata> GetInputPortMetadata() const override {
            return MakePortMetadataForDirection<Derived, PortDirection::Input>();
        }    

        /**
         * @brief Get output port metadata for visualization
         * 
         * Override in derived classes to provide runtime port information.
         * Used by the NodeSerializer template to export port list to JSON.
         * 
         * @return Vector of PortMetadata for all output ports
         */
        virtual std::vector<PortMetadata> GetOutputPortMetadata() const override {
            return MakePortMetadataForDirection<Derived, PortDirection::Output>();
        }

    };

    // Forward declaration for trait specialization
    template <typename T>
    struct NodeToINodeConverter {
        // Default: direct conversion
        static std::shared_ptr<INode> Convert(std::shared_ptr<T> node) {
            return std::dynamic_pointer_cast<INode>(node);
        }
    };

} // namespace graph
// Include specialized node implementations (in separate namespace scope)
//#include "MergeNode.hpp"
#include "graph/SplitNode.hpp"
#include "graph/NamedNodes.hpp"

