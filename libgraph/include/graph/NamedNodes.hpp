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
 * @file NamedNodes.hpp
 * @brief Named node types and data producers for GraphX
 *
 * This header provides specialized node implementations that combine GraphX node types
 * with the NamedType mixin for runtime identification. It includes:
 *
 * - **Named Node Types**: NamedSourceNode, NamedSinkNode, NamedMergeNode5
 *   - Inherit from both their node type and NamedType mixin
 *   - Enable runtime node identification via GetName() and SetName()
 *   - Support lifecycle methods: Init(), Start(), Stop(), Join()
 *
 * - **Data Producer Pattern**: DataGeneratorBase and DataProducerWithNotification
 *   - Implements the producer-consumer pattern with typed data generation
 *   - Supports precise timing control and interval-based sampling
 *   - Emits completion signals when data is exhausted
 *   - Integrates with metrics collection (Phase 5-6)
 *   - Two-port design: Port 0 (data samples), Port 1 (completion signal)
 *
 * ## Usage Pattern
 *
 * Create a custom data producer by:
 * 1. Implement DataGeneratorBase<DataType> with Produce() method
 * 2. Subclass DataProducerWithNotification with your generator and notification types
 * 3. Override CreateNotification() to specify completion signal format
 * 4. Instantiate with sampling parameters (interval, ignore count, max samples)
 *
 * Example:
 * @code
 *   class MyGenerator : public DataGeneratorBase<int> {
 *       std::optional<int> Produce(size_t index) override { \/\/ implementation }
 *   };
 *
 *   class MyProducer : public DataProducerWithNotification<
 *       MyProducer, MyGenerator, int, CompletionSignal> {
 *       CompletionSignal CreateNotification() const override { \/\/ implementation }
 *   };
 *
 *   auto producer = std::make_unique<MyProducer>(
 *       std::make_unique<MyGenerator>(),
 *       std::chrono::microseconds(10000),  // 10ms interval
 *       5);  // ignore first 5 samples
 * @endcode
 *
 * ## Metrics Integration
 *
 * All producers support Phases 5-6 metrics:
 * - Thread metrics tracking: iteration count, operation timings
 * - Queue metrics tracking: enqueue/dequeue counts, peak sizes
 * - Enable via EnableMetrics(true) after Init()
 * - Query via GetOutputQueueMetrics(port), GetOutputPortThreadMetrics(port)
 * - Export via MetricsFormatter with text/JSON/CSV formats
 *
 * ## Port Architecture
 *
 * DataProducerWithNotification uses a two-port design:
 * - **Port 0**: Data samples of type Message(DataType)
 *   - Produced on every successful Produce() call
 *   - Respects sample_ignore skip count
 *   - Produced every sample_interval microseconds
 * - **Port 1**: Completion signals of type Message(NotificationType)
 *   - Single emission when generator is exhausted
 *   - Enables graph-level coordination
 *   - Contains metadata (reason, timestamp, sample count, custom message)
 *
 * ## Thread Safety
 *
 * - notification_queue_ is thread-safe (ActiveQueue)
 * - generator_ is only accessed from the producer thread
 * - total_samples_generated_ is atomic-safe for read-only access from other threads
 * - Metrics operations are thread-safe via atomic operations
 *
 * @author Robert Klinkhammer
 * @date 2025-12-31
 * @version 1.0
 *
 * @see Nodes.hpp for base node types and NamedType mixin
 * @see nodes/Message.hpp for Message wrapper type
 * @see core/ActiveQueue.hpp for thread-safe queue implementation
 */

// MIT License
//
// Copyright (c) 2025 Robert Klinkhammer
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

#include "graph/Nodes.hpp"
#include "core/ActiveQueue.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <memory>
#include "graph/Message.hpp"
#include <log4cxx/logger.h>

namespace graph {


/**
 * @brief Named source node with runtime identification
 *
 * Combines SourceNode functionality with NamedType mixin to provide
 * both compile-time type safety and runtime node identification.
 * Source nodes produce data on their output ports without consuming input.
 *
 * @tparam NodeType The derived node class (for NamedType identification)
 * @tparam Outputs Output port message types
 *
 * @see NamedType for GetName(), SetName(), GetNodeTypeName() methods
 * @see SourceNode for port and lifecycle methods
 */
template <typename NodeType, typename... Outputs>
class NamedSourceNode : public SourceNode<Outputs...>, public NamedType<NodeType> {
public:
    /**
     * @brief Default constructor
     * Initialize source node with NamedType mixin support
     */
    NamedSourceNode() {}
    
    /**
     * @brief Virtual destructor for safe polymorphism
     */
    virtual ~NamedSourceNode() {
        auto logger = log4cxx::Logger::getLogger("NamedSourceNode");
        LOG4CXX_TRACE(logger, "Destroying NamedSourceNode");
    }

    /**
     * @brief Stop all producer threads and cleanup resources
     * Delegates to SourceNode::Stop() after any custom cleanup
     */
    void Stop() override {
        SourceNode<Outputs...>::Stop();
    }   
    
    /**
     * @brief Get metadata for all output ports
     * @return Vector of PortMetadata for each output port
     */
    virtual std::vector<PortMetadata> GetOutputPortMetadata() const override {
        std::vector<PortMetadata> out;
        if constexpr (HasPorts<NodeType>::value) {
            std::apply([&](auto... p) {
                ((p.direction == PortDirection::Output
                    ? out.push_back(MakePortMetadata<decltype(p)>())
                    : void()), ...);
            }, typename NodeType::Ports{});
        }

        return out;
    }
};

/**
 * @brief Named sink node with runtime identification
 *
 * Combines SinkNode functionality with NamedType mixin to provide
 * both compile-time type safety and runtime node identification.
 * Sink nodes consume data from their input ports without producing output.
 *
 * ## Typical Usage
 *
 * Create a sink that receives data on multiple input ports:
 * @code
 *   class ResultCollector : public NamedSinkNode<ResultCollector, Message, Message> {
 *   public:
 *       void Consume(std::integral_constant<std::size_t, 0>, const Message& msg) override {
 *           results_.push_back(std::get<int>(msg.data()));
 *       }
 *       
 *       void Consume(std::integral_constant<std::size_t, 1>, const Message& msg) override {
 *           auto* signal = std::get_if<CompletionSignal>(&msg.data());
 *           if (signal && signal->GetReason() == CompletionSignal::Reason::CSV_DATA_EXHAUSTED) {
 *               std::cout << "Data exhausted!" << std::endl;
 *           }
 *       }
 *   };
 * @endcode
 *
 * @tparam NodeType The derived node class (for NamedType identification and CRTP)
 * @tparam Inputs Input port message types (must match output of producers)
 *
 * @see NamedType for GetName(), SetName(), GetNodeTypeName() methods
 * @see SinkNode for Consume() interface and lifecycle methods
 * @see NamedSourceNode for the producer counterpart
 *
 * Thread Safety:
 * - Each Consume() port method is called exclusively from a dedicated consumer thread
 * - Input ordering per port is preserved (FIFO)
 * - No synchronization between ports
 */
template <typename NodeType, typename... Inputs>
class NamedSinkNode : public SinkNode<Inputs...>, public NamedType<NodeType> {
public:
    /**
     * @brief Default constructor
     * Initialize sink node with NamedType mixin support
     */
    NamedSinkNode() {}
    
    /**
     * @brief Virtual destructor for safe polymorphism
     */
    virtual ~NamedSinkNode() = default;
   
    /**
     * @brief Get input port metadata for visualization
     * 
     * Override in derived classes to provide runtime port information.
     * Used by the NodeSerializer template to export port list to JSON.
     * 
     * @return Vector of PortMetadata for all input ports
     */
    virtual std::vector<PortMetadata> GetInputPortMetadata() const override {
        std::vector<PortMetadata> out;
        if constexpr (HasPorts<NodeType>::value) {
            std::apply([&](auto... p) {
                ((p.direction == PortDirection::Input
                    ? out.push_back(MakePortMetadata<decltype(p)>())
                    : void()), ...);
            }, typename NodeType::Ports{});
        }

        return out;
    }
};

/**
 * @brief Named interior node with runtime identification
 *
 * Combines InteriorNode functionality with NamedType mixin to provide
 * both compile-time type safety and runtime node identification.
 * Interior nodes both consume data from input ports and produce data on output ports.
 *
 * @tparam InputList PayloadList of input port message types
 * @tparam OutputList PayloadList of output port message types
 * @tparam NodeType The derived node class (for NamedType identification)
 *
 * @see NamedType for GetName(), SetName(), GetNodeTypeName() methods
 * @see InteriorNode for port and lifecycle methods
 */

template <typename InputList, typename OutputList, typename NodeType>
class NamedInteriorNode : public InteriorNode<InputList, OutputList>, public NamedType<NodeType> {
public:
    /**
     * @brief Default constructor
     * Initialize interior node with NamedType mixin support
     */
    NamedInteriorNode() {}
    
    /**
     * @brief Virtual destructor for safe polymorphism
     */
    virtual ~NamedInteriorNode() = default;

    /**
     * @brief Get input port metadata for visualization
     * 
     * Override in derived classes to provide runtime port information.
     * Used by the NodeSerializer template to export port list to JSON.
     * 
     * @return Vector of PortMetadata for all input ports
     */
    virtual std::vector<PortMetadata> GetInputPortMetadata() const override {
        std::vector<PortMetadata> out;
        if constexpr (HasPorts<NodeType>::value) {
            std::apply([&](auto... p) {
                ((p.direction == PortDirection::Input
                    ? out.push_back(MakePortMetadata<decltype(p)>())
                    : void()), ...);
            }, typename NodeType::Ports{});
        }

        return out;
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
        std::vector<PortMetadata> out;
        if constexpr (HasPorts<NodeType>::value) {
            std::apply([&](auto... p) {
                ((p.direction == PortDirection::Output
                    ? out.push_back(MakePortMetadata<decltype(p)>())
                    : void()), ...);
            }, typename NodeType::Ports{});
        }

        return out;
    }


};

/**
 * @brief Named merge node with runtime identification
 *
 * Combines MergeNode functionality with NamedType mixin to provide
 * both compile-time type safety and runtime node identification.
 * Merge nodes combine data from multiple input ports into a single output stream.
 *
 * @tparam N Number of input ports
 * @tparam CommonInput Common input message type for all ports
 * @tparam OutputType Output message type
 * @tparam NodeType The derived node class (for NamedType identification)
 *
 * @see NamedType for GetName(), SetName(), GetNodeTypeName() methods
 * @see MergeNode for port and lifecycle methods
 */
#if 0
template <std::size_t N, typename CommonInput, typename OutputType, typename NodeType>
class NamedMergeNode : public MergeNode<N, CommonInput, OutputType>, public NamedType<NodeType> {
public:
    /**
     * @brief Default constructor
     * Initialize merge node with NamedType mixin support
     */
    NamedMergeNode() {}
    /**
     * @brief Virtual destructor for safe polymorphism
     */
    virtual ~NamedMergeNode() = default;

    /**
     * @brief Get input port metadata for visualization
     * 
     * Override in derived classes to provide runtime port information.
     * Used by the NodeSerializer template to export port list to JSON.
     * 
     * @return Vector of PortMetadata for all input ports
     */
    virtual std::vector<PortMetadata> GetInputPortMetadata() const override {
        std::vector<PortMetadata> out;
        if constexpr (HasPorts<NodeType>::value) {
            std::apply([&](auto... p) {
                ((p.direction == PortDirection::Input
                    ? out.push_back(MakePortMetadata<decltype(p)>())
                    : void()), ...);
            }, typename NodeType::Ports{});
        }

        return out;
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
        std::vector<PortMetadata> out;
        if constexpr (HasPorts<NodeType>::value) {
            std::apply([&](auto... p) {
                ((p.direction == PortDirection::Output
                    ? out.push_back(MakePortMetadata<decltype(p)>())
                    : void()), ...);
            }, typename NodeType::Ports{});
        }

        return out;
    }

};
#endif

} // namespace graph

