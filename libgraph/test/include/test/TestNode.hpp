/**
 * @file TestNode.hpp
 * @brief Test Node Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <log4cxx/logger.h>
#include "config/DataTypes.hpp"
#include "graph/Nodes.hpp"
#include "graph/Message.hpp"

namespace test {

/**
 * @class TestNode
 * @brief Test Node graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
    /**
     * @class TestNode
     * @brief Test Node graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
     */
    class TestNode : public graph::NamedSinkNode<TestNode, ::graph::message::Message> {
    public: 
        static constexpr char kStatePort[] = "State";
        using Ports = std::tuple<
            graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Input, kStatePort,
                graph::PayloadList<sensors::StateVector>>
            >;
            
        TestNode() : graph::NamedSinkNode<TestNode, ::graph::message::Message>() {
            SetName("FlightLogger");
        } 

        virtual ~TestNode() = default;

        bool Consume(const ::graph::message::Message& msg, std::integral_constant<std::size_t, 0>) override {
            // Log the incoming message (for demonstration, just print to console)
            (void)msg;  // Unused in this simple example
            std::cout << "[" << GetName() << "] Received message\n";
            return true;
        }
    };

} // namespace avionics

