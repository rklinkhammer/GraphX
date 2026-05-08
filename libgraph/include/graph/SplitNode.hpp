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

template <typename T, std::size_t N>
class SplitNode : public SinkNode<T>, public ExpandSourceNode<RepeatType_t<T, N>>::type {
public:
    virtual ~SplitNode() = default;

    bool Consume(const T& value, std::integral_constant<std::size_t, 0>) override {
        bool success = true;
        for (std::size_t i = 0; i < N; ++i) {
            success &= input_queue_[i].Enqueue(value);
        }
        return success;
    }

    using SourceBase = typename ExpandSourceNode<RepeatType_t<T, N>>::type;
    
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

    int GetOutputCount() const { return N; }

protected:
    core::ActiveQueue<T> input_queue_[N];
};

// Explicit specializations for SplitNode1-SplitNode8

template<typename T>
class SplitNode1 : public SplitNode<T, 1> {
public:
     
    virtual ~SplitNode1() = default;
    
    std::string GetNodeTypeName() const { return "SplitNode1"; }
    
    std::optional<T> Produce(std::integral_constant<std::size_t, 0>) override {
        T v;
        if (this->input_queue_[0].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }
};

template<typename T>
class SplitNode2 : public SplitNode<T, 2> {
public:
     
    virtual ~SplitNode2() = default;
    
    std::string GetNodeTypeName() const { return "SplitNode2"; }
    
    std::optional<T> Produce(std::integral_constant<std::size_t, 0>) override {
        T v;
        if (this->input_queue_[0].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 1>) override {
        T v;
        if (this->input_queue_[1].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }
};

template<typename T>
class SplitNode3 : public SplitNode<T, 3> {
public:
      
    virtual ~SplitNode3() = default;
    
    std::string GetNodeTypeName() const { return "SplitNode3"; }
    
    std::optional<T> Produce(std::integral_constant<std::size_t, 0>) override {
        T v;
        if (this->input_queue_[0].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 1>) override {
        T v;
        if (this->input_queue_[1].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 2>) override {
        T v;
        if (this->input_queue_[2].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }
};

template<typename T>
class SplitNode4 : public SplitNode<T, 4> {
public:
     
    virtual ~SplitNode4() = default;
    
    std::string GetNodeTypeName() const { return "SplitNode4"; }
    
    std::optional<T> Produce(std::integral_constant<std::size_t, 0>) override {
        T v;
        if (this->input_queue_[0].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 1>) override {
        T v;
        if (this->input_queue_[1].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 2>) override {
        T v;
        if (this->input_queue_[2].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 3>) override {
        T v;
        if (this->input_queue_[3].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }
};

template<typename T>
class SplitNode5 : public SplitNode<T, 5> {
public:
     
    virtual ~SplitNode5() = default;
    
    std::string GetNodeTypeName() const { return "SplitNode5"; }
    
    std::optional<T> Produce(std::integral_constant<std::size_t, 0>) override {
        T v;
        if (this->input_queue_[0].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 1>) override {
        T v;
        if (this->input_queue_[1].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 2>) override {
        T v;
        if (this->input_queue_[2].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 3>) override {
        T v;
        if (this->input_queue_[3].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 4>) override {
        T v;
        if (this->input_queue_[4].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }
};

template<typename T>
class SplitNode6 : public SplitNode<T, 6> {
public:

virtual ~SplitNode6() = default;
    std::optional<T> Produce(std::integral_constant<std::size_t, 0>) override {
        T v;
        if (this->input_queue_[0].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 1>) override {
        T v;
        if (this->input_queue_[1].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 2>) override {
        T v;
        if (this->input_queue_[2].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 3>) override {
        T v;
        if (this->input_queue_[3].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 4>) override {
        T v;
        if (this->input_queue_[4].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 5>) override {
        T v;
        if (this->input_queue_[5].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }
};

template<typename T>
class SplitNode7 : public SplitNode<T, 7> {
public:
     
    virtual ~SplitNode7() = default;
    
    std::string GetNodeTypeName() const override { return "SplitNode7"; }
    
    std::optional<T> Produce(std::integral_constant<std::size_t, 0>) override {
        T v;
        if (this->input_queue_[0].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 1>) override {
        T v;
        if (this->input_queue_[1].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 2>) override {
        T v;
        if (this->input_queue_[2].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 3>) override {
        T v;
        if (this->input_queue_[3].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 4>) override {
        T v;
        if (this->input_queue_[4].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 5>) override {
        T v;
        if (this->input_queue_[5].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 6>) override {
        T v;
        if (this->input_queue_[6].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }
};

template<typename T>
class SplitNode8 : public SplitNode<T, 8> {
public:
     
    virtual ~SplitNode8() = default;
    
    std::string GetNodeTypeName() const override { return "SplitNode8"; }
    
    std::optional<T> Produce(std::integral_constant<std::size_t, 0>) override {
        T v;
        if (this->input_queue_[0].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 1>) override {
        T v;
        if (this->input_queue_[1].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 2>) override {
        T v;
        if (this->input_queue_[2].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 3>) override {
        T v;
        if (this->input_queue_[3].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 4>) override {
        T v;
        if (this->input_queue_[4].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 5>) override {
        T v;
        if (this->input_queue_[5].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 6>) override {
        T v;
        if (this->input_queue_[6].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }

    std::optional<T> Produce(std::integral_constant<std::size_t, 7>) override {
        T v;
        if (this->input_queue_[7].Dequeue(v)) {
            return std::optional<T>(v);
        } else {
            return std::nullopt;
        }
    }
};

} // namespace graph

