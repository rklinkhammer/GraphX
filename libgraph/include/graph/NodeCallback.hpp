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

/**
 * @brief Base class for nodes that support callbacks
 *
 * Provides unified interface for installing and querying callbacks on wrapped nodes.
 * This is an optional interface - nodes without callback support don't inherit from it.
 *
 * Wrappers inherit from both NodeCallback and INode:
 *   class SensorNodeWrapper : public NodeCallback, public INode { ... };
 *
 * Implementation Notes:
 * - Callback installation is idempotent (calling again replaces previous)
 * - All callbacks are optional (node can work without any callbacks installed)
 * - Callbacks are invoked asynchronously during node Run()
 * - Callback handlers are expected to be thread-safe
 */

namespace graph {
 class NodeCallback {
public:
    virtual ~NodeCallback() = default;

protected:
    /**
     * @brief Helper for derived classes to log installation
     * @param logger Log4cxx logger instance
     * @param node_name Name of node
     * @param callback_type Type of callback (metrics, completion, error)
     */
    static void LogCallbackInstallation(
        log4cxx::LoggerPtr logger,
        const std::string& node_name,
        const std::string& callback_type) {
        if (logger && logger->isDebugEnabled()) {
            LOG4CXX_TRACE(logger, "Callback installed - Node: " << node_name
                                  << ", Type: " << callback_type);
        }
    }
};

}  // namespace graph