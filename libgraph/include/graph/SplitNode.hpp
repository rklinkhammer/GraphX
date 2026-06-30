/**
 * @file SplitNode.hpp
 * @brief Split Node Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2025 Robert Klinkhammer
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/sell
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

#include "graph/NodeShapes.hpp"
#include "core/ActiveQueue.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <memory>

namespace graph
{ 

/**
 * @brief SplitNode: Routes single input to N identical output ports
 * 
 * Takes a single input stream and replicates it to N output ports.
 * Each output port receives independent copies of input data.
 * 
 * DESIGN: Template metaprogramming with TypeList expansion
 * - Primary template: SplitNode<T, N> for N output ports
 * - Helper: ExpandSourceNode expands TypeList<T, T, ..., T> into SourceNode<T, T, ...>
 * - Specializations: SplitNode1 through SplitNode8 for convenience
 * 
 * SUPPORTED OUTPUT PORTS: 1-8
 * Maximum N is 8. For higher port counts, either:
 * 1. Add SplitNode9, SplitNode10, etc. specializations
 * 2. Use composition: Chain multiple SplitNode8 instances
 * 3. Refactor using fold expressions for dynamic N (C++17+)
 * 
 * PERFORMANCE CHARACTERISTICS:
 * - Stack-allocated queue array: cache-friendly, no dynamic allocation
 * - Copy-per-output: Each output gets independent data
 * - No atomic operations: Synchronization via base class lifecycle
 * 
 * EXAMPLE:
 * @code
 *   SplitNode3<double> splitter;  // 1 input, 3 identical outputs
 *   
 *   auto src = graph.AddNode<MySource>();
 *   auto process1 = graph.AddNode<Process1>();
 *   auto process2 = graph.AddNode<Process2>();
 *   auto process3 = graph.AddNode<Process3>();
 *   
 *   graph.AddEdge<MySource, 0, SplitNode3<double>, 0>(src, splitter);
 *   graph.AddEdge<SplitNode3<double>, 0, Process1, 0>(splitter, process1);
 *   graph.AddEdge<SplitNode3<double>, 1, Process2, 0>(splitter, process2);
 *   graph.AddEdge<SplitNode3<double>, 2, Process3, 0>(splitter, process3);
 * @endcode
 */

// Helper to expand TypeList into template parameters
/**
 * @struct ExpandSourceNode
 * @brief Expand Source Node data record.
 *
 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
 */
template<typename TypeList>
struct ExpandSourceNode;

/**

 * @struct ExpandSourceNode

 * @brief Expand Source Node data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

template<typename... Ts>
struct ExpandSourceNode<TypeList<Ts...>> {
    using type = SourceNode<Ts...>;
};

template <typename T, std::size_t N, typename Derived>
/**
 * @class SplitNode
 * @brief Split Node graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class SplitNode : public SinkNode<T>, public ExpandSourceNode<RepeatType_t<T, N>>::type {
public:
    /**
     * @brief Releases resources owned by Split Node.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~SplitNode() = default;

    static constexpr std::size_t NInputs = 1;
    static constexpr std::size_t NOutputs = N;

    /**
     * @brief Processes data through the Consume operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param value Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool Consume(const T& value, std::integral_constant<std::size_t, 0>) override {
        bool success = true;
        for (std::size_t i = 0; i < N; ++i) {
            success &= input_queue_[i].Enqueue(value);
        }
        return success;
    }

    using SourceBase = typename ExpandSourceNode<RepeatType_t<T, N>>::type;

    /**
     * @brief Creates or builds the object described by Make Directional Port Name.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param prefix Input or configuration value consumed by the method.
     * @param index Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    static std::string MakeDirectionalPortName(const char* prefix, std::size_t index) {
        return std::string(prefix) + std::to_string(index);
    }
    
    /**
     * @brief Performs the Init lifecycle step.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool Init() override {
        return SinkNode<T>::Init() && SourceBase::Init();
    }

    /**
     * @brief Get node type name for metadata
     * Identifies this as a split/fan-out node with N outputs
     */
    std::string GetNodeTypeName() const {
        return "SplitNode<" + std::to_string(N) + ">";
    }

    /**
     * @brief Performs the Start lifecycle step.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool Start() override {
        return SinkNode<T>::Start() && SourceBase::Start();
    }

    /**
     * @brief Performs the Stop lifecycle step.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void Stop() override {
        SinkNode<T>::Stop();
        SourceBase::Stop();
        for (std::size_t i = 0; i < N; ++i) {
            input_queue_[i].Disable();
        }
    }

    /**
     * @brief Performs the Join lifecycle step.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void Join() override {
        SinkNode<T>::Join();
        SourceBase::Join();
    }

    int GetOutputPortCount() const { return N; }

    int GetInputPortCount() const { return 1; }

    /**
     * @brief Returns the Lifecycle State.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    LifecycleState GetLifecycleState() const override {
        return SinkNode<T>::GetLifecycleState();
    }

    /**
     * @brief Executes the Join With Timeout operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param timeout_ms Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override {
        return SinkNode<T>::JoinWithTimeout(timeout_ms);
    }   

    
    /**
     * @brief Get input port metadata for visualization
     *
     * Override in derived classes to provide runtime port information.
     * Used by the NodeSerializer template to export port list to JSON.
     *
     * @return Vector of PortMetadata for all input ports
     */
    virtual std::vector<PortMetadata> GetInputPortMetadata() const override
    {
        return {
            PortMetadata{
                .port_index = 0,
                .payload_type = std::string(TypeName<T>()),
                .direction = "input",
                .port_name = MakeDirectionalPortName("Input", 0)
            }
        };
    }

    /**
     * @brief Get output port metadata for visualization
     *
     * Override in derived classes to provide runtime port information.
     * Used by the NodeSerializer template to export port list to JSON.
     *
     * @return Vector of PortMetadata for all output ports
     */
    virtual std::vector<PortMetadata> GetOutputPortMetadata() const override
    {
        std::vector<PortMetadata> out;
        out.reserve(N);
        const auto payload_type = std::string(TypeName<T>());

        for (std::size_t i = 0; i < N; ++i) {
            out.push_back(PortMetadata{
                .port_index = i,
                .payload_type = payload_type,
                .direction = "output",
                .port_name = MakeDirectionalPortName("Output", i)
            });
        }

        return out;
    }

protected:
    template<std::size_t Index>
    /**
     * @brief Processes data through the Produce From Queue operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::optional<T> ProduceFromQueue() {
        /**
         * @brief Executes the Static Assert operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        static_assert(Index < N, "SplitNode output index out of range");
        T value;
        if (input_queue_[Index].Dequeue(value)) {
            return value;
        }
        return std::nullopt;
    }

    core::ActiveQueue<T> input_queue_[N];
};

// Compile-time generated produce override chain for SplitNodeN (N=1..8)

/**

 * @class SplitProduceOverrideChain

 * @brief Split Produce Override Chain type.

 *

 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.

 */

template<typename T, std::size_t N, typename Derived, std::size_t PortIndex, bool Done = (PortIndex == N)>
class SplitProduceOverrideChain;

template<typename T, std::size_t N, typename Derived, std::size_t PortIndex>
/**
 * @class SplitProduceOverrideChain
 * @brief Split produce override chain implementation for GraphX.
 */
/**
 * @class SplitProduceOverrideChain
 * @brief Split Produce Override Chain type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
class SplitProduceOverrideChain<T, N, Derived, PortIndex, false>
    : public SplitProduceOverrideChain<T, N, Derived, PortIndex + 1, (PortIndex + 1 == N)> {
public:
    using Next = SplitProduceOverrideChain<T, N, Derived, PortIndex + 1, (PortIndex + 1 == N)>;
    using Next::Produce;

    /**
     * @brief Processes data through the Produce operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::optional<T> Produce(std::integral_constant<std::size_t, PortIndex>) override {
        return this->template ProduceFromQueue<PortIndex>();
    }
};

/**

 * @class SplitProduceOverrideChain

 * @brief Split Produce Override Chain type.

 *

 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.

 */

template<typename T, std::size_t N, typename Derived, std::size_t PortIndex>
class SplitProduceOverrideChain<T, N, Derived, PortIndex, true>
    : public SplitNode<T, N, Derived> {
public:
    using SplitNode<T, N, Derived>::ProduceFromQueue;
};

template<typename T, std::size_t N>
/**
 * @class SplitNodeN
 * @brief Split Node N graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class SplitNodeN
    : public SplitProduceOverrideChain<T, N, SplitNodeN<T, N>, 0, (0 == N)> {
public:
    /**
     * @brief Executes the Static Assert operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    static_assert(N >= 1 && N <= 8, "SplitNodeN supports 1-8 outputs");

    using SplitProduceOverrideChain<T, N, SplitNodeN<T, N>, 0, (0 == N)>::Produce;

    /**
     * @brief Releases resources owned by Split Node N.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~SplitNodeN() = default;

    std::string GetNodeTypeName() const { return "SplitNode" + std::to_string(N); }
};

template<typename T>
using SplitNode1 = SplitNodeN<T, 1>;

template<typename T>
using SplitNode2 = SplitNodeN<T, 2>;

template<typename T>
using SplitNode3 = SplitNodeN<T, 3>;

template<typename T>
using SplitNode4 = SplitNodeN<T, 4>;

template<typename T>
using SplitNode5 = SplitNodeN<T, 5>;

template<typename T>
using SplitNode6 = SplitNodeN<T, 6>;

template<typename T>
using SplitNode7 = SplitNodeN<T, 7>;

template<typename T>
using SplitNode8 = SplitNodeN<T, 8>;

} // namespace graph

