/**
 * @file NamedNodes.hpp
 * @brief Named Nodes Graph runtime support.
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
/**
 * @class NamedSourceNode
 * @brief Named Source Node graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
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
    virtual ~NamedSourceNode() = default;

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
        return MakePortMetadataForDirection<NodeType, PortDirection::Output>();
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
/**
 * @class NamedSinkNode
 * @brief Named Sink Node graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
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
        return MakePortMetadataForDirection<NodeType, PortDirection::Input>();
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
/**
 * @class NamedInteriorNode
 * @brief Named Interior Node graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
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
        return MakePortMetadataForDirection<NodeType, PortDirection::Input>();
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
        return MakePortMetadataForDirection<NodeType, PortDirection::Output>();
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
/**
 * @class NamedMergeNode
 * @brief Named Merge Node graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
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
        /**
         * @brief Executes the Constexpr operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
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
        /**
         * @brief Executes the Constexpr operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
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

