// MIT License
/// @file FeedbackTestSinkNode.hpp
/// @brief Avionics node implementation for FeedbackTestSinkNode

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

#include <vector>
#include <chrono>
#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>
#include <memory>
#include <optional>
#include <deque>
#include "graph/NamedNodes.hpp"
#include "sensor/SensorDataTypes.hpp"
#include "avionics/estimators/SensorVoter.hpp"
#include "graph/IConfigurable.hpp"
#include "config/JsonView.hpp"
#include <array>
#include "avionics/messages/AvionicsMessages.hpp"

namespace avionics {

class FeedbackTestSinkNode : 
    public graph::NamedSinkNode<FeedbackTestSinkNode,  avionics::PhaseAdaptiveFeedbackMsg> {
public:
        static constexpr char kStatePort[] = "Feedback";
        using Ports = std::tuple<
            graph::PortSpec<0, avionics::PhaseAdaptiveFeedbackMsg, graph::PortDirection::Input, kStatePort,
                graph::PayloadList<avionics::PhaseAdaptiveFeedbackMsg>>
            >;

    FeedbackTestSinkNode() : messages_received_(0) {
    }
    
    bool Consume(const avionics::PhaseAdaptiveFeedbackMsg& msg,
                std::integral_constant<std::size_t, 0>) override {
        messages_.push_back(msg);
        messages_received_++;
        return true;
    }

    size_t GetMessagesReceived() const { return messages_received_.load(); }
    const std::vector<avionics::PhaseAdaptiveFeedbackMsg>& GetMessages() const { 
        return messages_; 
    }

private:
    std::atomic<size_t> messages_received_;
    std::vector<avionics::PhaseAdaptiveFeedbackMsg> messages_;
};


}  // namespace avionics