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
 * @file FlightLoggerNode.hpp
 * @brief Example sensor node implementation for GraphX
 *
 * Provides a reusable FlightLoggerNode class that demonstrates:
 * - Data generator implementation
 * - Notification signal creation
 * - Named node pattern
 * - Metrics collection and reporting
 *
 * @author Robert Klinkhammer
 * @date 2025-12-31
 */

#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <log4cxx/logger.h>
#include "sensor/SensorDataTypes.hpp"
#include "graph/Nodes.hpp"
#include "graph/Message.hpp"

namespace avionics {

    class FlightLoggerNode : public graph::NamedSinkNode<FlightLoggerNode, ::graph::message::Message> {
    public: 
        static constexpr char kStatePort[] = "State";
        using Ports = std::tuple<
            graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Input, kStatePort,
                graph::PayloadList<sensors::StateVector>>
            >;
            
        FlightLoggerNode() : graph::NamedSinkNode<FlightLoggerNode, ::graph::message::Message>() {
            SetName("FlightLogger");
        } 

        virtual ~FlightLoggerNode() = default;

        bool Consume(const ::graph::message::Message& msg, std::integral_constant<std::size_t, 0>) override {
            // Log the incoming message (for demonstration, just print to console)
            (void)msg;  // Unused in this simple example
            std::cout << "[" << GetName() << "] Received message\n";
            return true;
        }
    };

} // namespace avionics

