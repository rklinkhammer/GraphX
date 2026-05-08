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

#include <string>
#include <chrono>
#include <memory>
#include "graph/CapabilityBus.hpp"
#include "graph/GraphManager.hpp"

namespace graph
{
    struct GraphExecutorContext
    {

        std::shared_ptr<GraphManager> GetGraphManager() const
        {
            return graph_manager;
        }

        /// @brief Check if stop has been requested
        /// @return true if stop requested, false otherwise
        bool IsStopped() const
        {
            return is_stopped.load();
        }

        /// @brief Request application stop
        void SetStopped() const
        {
            is_stopped.store(true);
        }

        /// Capability bus for capability discovery and registration
        CapabilityBus capability_bus;
        std::shared_ptr<GraphManager> graph_manager;
        mutable std::atomic<bool> is_stopped{false};
    };

} // namespace graph