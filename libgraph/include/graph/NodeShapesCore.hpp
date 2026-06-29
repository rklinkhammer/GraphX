/**
 * @file Nodes.hpp
 * @brief Nodes Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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

    /**

     * @class SourceNodeBase

     * @brief Source Node Base graph node.

     *

     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.

     */

    template <typename Outputs>
    class SourceNodeBase;

    template <typename... Outputs>
/**
 * @class SourceNodeBase
 * @brief Source node base implementation for GraphX.
 */
    /**
     * @class SourceNodeBase
     * @brief Source Node Base graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
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
        /**
         * @brief Releases resources owned by Source Node Base.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         */
        virtual ~SourceNodeBase() {
            auto logger = log4cxx::Logger::getLogger("SourceNodeBase");
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param logger Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(logger, "Destroying SourceNodeBase");
        }

        /**
         * @brief Returns the Lifecycle State.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        virtual LifecycleState GetLifecycleState() const override {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port GetLifecycleState`.");
            return this->GetLifecycleStateImpl();
        }
  
        virtual bool Init() override
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port Init.");
            return this->InitImpl();
        }

        virtual bool Start() override
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port Start.");
            return this->StartImpl();
        }

        virtual void Stop() override
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port Stop.");
            this->StopImpl();
        }

        virtual void Join() override
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port Join.");
            this->JoinImpl();
        }

        virtual bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Enable Metrics operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param false Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param NOutputs Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param NOutputs Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param NOutputs Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param NOutputs Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port InitPortsImpl.");
            return (OutputFn<Outputs>::Init() && ...);
        }

        bool StartPortsImpl()
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port StartPortsImpl.");
            // Enable all output queues before starting
            (OutputFn<Outputs>::EnableQueue(), ...);
            return (OutputFn<Outputs>::Start() && ...);
        }

        void StopPortsImpl()
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SourceNodeBase port StopPortsImpl.");
            (OutputFn<Outputs>::DisableQueue(), ...);
            (OutputFn<Outputs>::Stop(), ...);
        }

        void JoinPortsImpl()
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
 * @brief Source Node graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
    /**
     * @class SourceNode
     * @brief Source Node graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
     */
    class SourceNode : public SourceNodeBase<typename MakePorts<TypeList<Outputs...>>::type>
    {
    public:
        /**
         * @brief Releases resources owned by Source Node.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         */
        virtual ~SourceNode() = default;
    };

    /**

     * @class SinkNodeBase

     * @brief Sink Node Base graph node.

     *

     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.

     */

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
    /**
     * @class SinkNodeBase
     * @brief Sink Node Base graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
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
        /**
         * @brief Releases resources owned by Sink Node Base.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         */
        virtual ~SinkNodeBase() {
            auto logger = log4cxx::Logger::getLogger("SinkNodeBase");
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param logger Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(logger, "Destroying SinkNodeBase");
        }

        /**
         * @brief Returns the Lifecycle State.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        virtual LifecycleState GetLifecycleState() const override {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port GetLifecycleState`.");
            return this->GetLifecycleStateImpl();
        }
        
        virtual bool Init() override
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port Init`.");
            return this->InitImpl();
        }

        virtual bool Start() override
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port Start`.");
            return this->StartImpl();
        }

        virtual void Stop() override
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port Stop`.");
            this->StopImpl();
        }

        virtual void Join() override
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port Join`.");
            this->JoinImpl();
        }

        virtual bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Enable Metrics operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param false Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param NInputs Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param NInputs Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param NInputs Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port InitPortsImpl`.");
            return (InputFn<Inputs>::Init() && ...);
        }

        bool StartPortsImpl()
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port StartPortsImpl`.");
            // Enable all input queues before starting
            (InputFn<Inputs>::EnableQueue(), ...);
            return (InputFn<Inputs>::Start() && ...);
        }

        void StopPortsImpl()
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "SinkNodeBase port StopPortsImpl`.");
            (InputFn<Inputs>::DisableQueue(), ...);
            (InputFn<Inputs>::Stop(), ...);
        }

        void JoinPortsImpl()
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
 * @brief Sink Node graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
    /**
     * @class SinkNode
     * @brief Sink Node graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
     */
    class SinkNode : public SinkNodeBase<typename MakePorts<TypeList<Inputs...>>::type>
    {
    public:
        /**
         * @brief Releases resources owned by Sink Node.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         */
        virtual ~SinkNode() = default;
   };

    /**

     * @class InteriorNodeBase

     * @brief Interior Node Base graph node.

     *

     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.

     */

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
    /**
     * @class InteriorNodeBase
     * @brief Interior Node Base graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
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

        /**
         * @brief Releases resources owned by Interior Node Base.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         */
        virtual ~InteriorNodeBase() = default;

        /**
         * @brief Returns the Lifecycle State.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        virtual LifecycleState GetLifecycleState() const override{
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port GetLifecycleState`.");
            return this->GetLifecycleStateImpl();
        }
  
        virtual bool Init() override
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port Init`.");
            return this->InitImpl();
        }

        virtual bool Start() override
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port Start`.");
            return this->StartImpl();
        }

        virtual void Stop() override
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port Stop`.");
            this->StopImpl();
        }

        virtual void Join() override
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port Join`.");
            this->JoinImpl();
        }

        virtual bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Enable Input Metrics operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param false Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            EnableInputMetrics(false);
        }

        /// Disable metrics collection for output ports
        void DisableOutputMetrics()
        {
            /**
             * @brief Executes the Enable Output Metrics operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param false Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            EnableOutputMetrics(false);
        }

        /// Enable metrics collection for all transfer ports (both input and output)
        void EnableMetrics(bool enabled = true)
        {
            /**
             * @brief Executes the Enable Input Metrics operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param enabled Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            EnableInputMetrics(enabled);
            /**
             * @brief Executes the Enable Output Metrics operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param enabled Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            EnableOutputMetrics(enabled);
        }

        /// Disable metrics collection for all transfer ports
        void DisableMetrics()
        {
            /**
             * @brief Executes the Enable Metrics operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param false Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param NInputs Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param NOutputs Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param NInputs Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param NOutputs Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param NInputs Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param NOutputs Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param NInputs Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port InitPortsImpl`.");
            return (TransferFn<Inputs, Outputs>::Init() && ...);
        }

        bool StartPortsImpl()
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port StartPortsImpl`.");
            // Enable all input and output queues before starting
            (TransferFn<Inputs, Outputs>::EnableQueues(), ...);
            return (TransferFn<Inputs, Outputs>::Start() && ...);
        }

        void StopPortsImpl()
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InteriorNodeBase port StopPortsImpl`.");
            (TransferFn<Inputs, Outputs>::DisableQueues(), ...);
            (TransferFn<Inputs, Outputs>::Stop(), ...);
        }

        void JoinPortsImpl()
        {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
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
 * @brief Interior Node graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
    /**
     * @class InteriorNode
     * @brief Interior Node graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
     */
    class InteriorNode
        : public InteriorNodeBase<typename MakePorts<InputList>::type, typename MakePorts<OutputList>::type>
    {
    public:
        /**
         * @brief Releases resources owned by Interior Node.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         */
        virtual ~InteriorNode() = default;
    };


} // namespace graph
