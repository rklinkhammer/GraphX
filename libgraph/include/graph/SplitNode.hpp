/**
 * @file SplitNode.hpp
 * @brief GraphX source file.
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

#include "graph/Nodes.hpp"
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
template<typename TypeList>
struct ExpandSourceNode;

template<typename... Ts>
struct ExpandSourceNode<TypeList<Ts...>> {
    using type = SourceNode<Ts...>;
};

template <typename T, std::size_t N, typename Derived>
/**
 * @class SplitNode
 * @brief SplitNode class.
 */
class SplitNode : public SinkNode<T>, public ExpandSourceNode<RepeatType_t<T, N>>::type {
public:
    virtual ~SplitNode() = default;

    static constexpr std::size_t NInputs = 1;
    static constexpr std::size_t NOutputs = N;

    bool Consume(const T& value, std::integral_constant<std::size_t, 0>) override {
        bool success = true;
        for (std::size_t i = 0; i < N; ++i) {
            success &= input_queue_[i].Enqueue(value);
        }
        return success;
    }

    using SourceBase = typename ExpandSourceNode<RepeatType_t<T, N>>::type;

    static std::string MakeDirectionalPortName(const char* prefix, std::size_t index) {
        return std::string(prefix) + std::to_string(index);
    }
    
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

    bool Start() override {
        return SinkNode<T>::Start() && SourceBase::Start();
    }

    void Stop() override {
        SinkNode<T>::Stop();
        SourceBase::Stop();
        for (std::size_t i = 0; i < N; ++i) {
            input_queue_[i].Disable();
        }
    }

    void Join() override {
        SinkNode<T>::Join();
        SourceBase::Join();
    }

    int GetOutputPortCount() const { return N; }

    int GetInputPortCount() const { return 1; }

    LifecycleState GetLifecycleState() const override {
        return SinkNode<T>::GetLifecycleState();
    }

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
    std::optional<T> ProduceFromQueue() {
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

template<typename T, std::size_t N, typename Derived, std::size_t PortIndex, bool Done = (PortIndex == N)>
class SplitProduceOverrideChain;

template<typename T, std::size_t N, typename Derived, std::size_t PortIndex>
/**
 * @class SplitProduceOverrideChain
 * @brief Split produce override chain implementation for GraphX.
 */
class SplitProduceOverrideChain<T, N, Derived, PortIndex, false>
    : public SplitProduceOverrideChain<T, N, Derived, PortIndex + 1, (PortIndex + 1 == N)> {
public:
    using Next = SplitProduceOverrideChain<T, N, Derived, PortIndex + 1, (PortIndex + 1 == N)>;
    using Next::Produce;

    std::optional<T> Produce(std::integral_constant<std::size_t, PortIndex>) override {
        return this->template ProduceFromQueue<PortIndex>();
    }
};

template<typename T, std::size_t N, typename Derived, std::size_t PortIndex>
class SplitProduceOverrideChain<T, N, Derived, PortIndex, true>
    : public SplitNode<T, N, Derived> {
public:
    using SplitNode<T, N, Derived>::ProduceFromQueue;
};

template<typename T, std::size_t N>
/**
 * @class SplitNodeN
 * @brief SplitNodeN class.
 */
class SplitNodeN
    : public SplitProduceOverrideChain<T, N, SplitNodeN<T, N>, 0, (0 == N)> {
public:
    static_assert(N >= 1 && N <= 8, "SplitNodeN supports 1-8 outputs");

    using SplitProduceOverrideChain<T, N, SplitNodeN<T, N>, 0, (0 == N)>::Produce;

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

