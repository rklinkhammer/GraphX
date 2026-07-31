// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include <log4cxx/logger.h>

#include "graph/EdgeFacade.hpp"
#include "graph/INode.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "graph/ThreadPool.hpp"

namespace graph::detail {

inline void StopEdges(std::vector<std::unique_ptr<IEdgeBase>>& edges) {
    for (auto& edge : edges) {
        try {
            edge->Stop();
        } catch (...) {
            // Suppress exceptions during shutdown.
        }
    }
}

inline void StopNodes(std::vector<std::shared_ptr<INode>>& nodes) {
    for (auto& node : nodes) {
        try {
            node->Stop();
        } catch (...) {
            // Suppress exceptions during shutdown.
        }
    }
}

inline void StopThreadPool(std::unique_ptr<ThreadPool>& thread_pool) {
    if (!thread_pool) {
        return;
    }

    try {
        thread_pool->Stop();
    } catch (...) {
        // Suppress exceptions during shutdown.
    }
}

inline void JoinEdgesWithTimeout(std::vector<std::unique_ptr<IEdgeBase>>& edges,
                                 std::chrono::milliseconds timeout) {
    for (auto& edge : edges) {
        try {
            edge->JoinWithTimeout(timeout);
        } catch (...) {
            // Suppress exceptions during shutdown.
        }
    }
}

inline void JoinNodesWithTimeout(std::vector<std::shared_ptr<INode>>& nodes,
                                 std::chrono::milliseconds timeout) {
    for (auto& node : nodes) {
        try {
            node->JoinWithTimeout(timeout);
        } catch (...) {
            // Suppress exceptions during shutdown.
        }
    }
}

inline void JoinThreadPool(std::unique_ptr<ThreadPool>& thread_pool) {
    if (!thread_pool) {
        return;
    }

    try {
        thread_pool->Join();
    } catch (...) {
        // Suppress exceptions during shutdown.
    }
}

inline void CleanupFacadeWrappers(std::vector<std::shared_ptr<INode>>& nodes) {
    LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                  "GraphManager destructor: calling Cleanup() on " << nodes.size() << " nodes");

    for (auto& node : nodes) {
        try {
            auto* wrapper = dynamic_cast<NodeFacadeAdapterWrapper*>(node.get());
            if (wrapper) {
                LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                              "Found NodeFacadeAdapterWrapper, calling Cleanup()");
                wrapper->Cleanup();
            } else {
                LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                              "Node is not NodeFacadeAdapterWrapper, skipping Cleanup()");
            }
        } catch (...) {
            LOG4CXX_WARN(log4cxx::Logger::getLogger("graph.graph"),
                         "Exception during node Cleanup()");
        }
    }
}

inline void ReleaseEdges(std::vector<std::unique_ptr<IEdgeBase>>& edges) {
    try {
        edges.clear();
    } catch (...) {
        // Suppress exceptions during shutdown.
    }
}

inline void ReleaseNodes(std::vector<std::shared_ptr<INode>>& nodes) {
    try {
        nodes.clear();
    } catch (...) {
        // Suppress exceptions during shutdown.
    }
}

} // namespace graph::detail
