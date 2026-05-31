/**
 * @file PluginInfrastructure.hpp
 * @brief Plugin loading and management infrastructure for test topologies
 *
 * Provides centralized plugin factory management and edge creation helpers
 * for building test topologies with dynamically loaded nodes.
 *
 * @author Test Suite
 * @date May 29, 2026
 */

#pragma once

#include <memory>
#include <log4cxx/logger.h>
#include "graph/GraphManager.hpp"
#include "graph/NodeFactory.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "plugins/PluginRegistry.hpp"
#include "plugins/PluginLoader.hpp"

// Ensure PLUGIN_OUTPUT_DIRECTORY is defined
#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace test {

/**
 * @class PluginInfrastructure
 * @brief Manages plugin loading and provides helpers for topology builders
 *
 * Singleton pattern for factory initialization and templated edge creation
 * utilities for connecting dynamically loaded nodes.
 */
class PluginInfrastructure {
public:
    /**
     * @brief Get or initialize the NodeFactory with all plugins loaded
     * @return Shared pointer to initialized NodeFactory
     */
    static std::shared_ptr<graph::NodeFactory> GetFactory() {
        static auto* factory = new std::shared_ptr<graph::NodeFactory>();
        static auto* loader = new std::shared_ptr<graph::PluginLoader>();
        static bool initialized = false;
        
        if (!initialized) {
            auto registry = std::make_shared<graph::PluginRegistry>();
            // Load plugins from build directory
            *loader = std::make_shared<graph::PluginLoader>(PLUGIN_OUTPUT_DIRECTORY, registry);
            
            try {
                (*loader)->LoadAllPlugins();
            } catch (...) {
                // Plugins may not be available in test environment
            }
            
            *factory = std::make_shared<graph::NodeFactory>(registry);
            initialized = true;
        }
        
        return *factory;
    }

    /**
     * @brief Add an edge between two wrapped nodes with type checking
     * @tparam SrcNode Source node type
     * @tparam SrcPort Source port index
     * @tparam DstNode Destination node type
     * @tparam DstPort Destination port index
     * @param g Graph manager to add edge to
     * @param src_wrapper Source node wrapper
     * @param dst_wrapper Destination node wrapper
     * @param buffer_size Queue buffer size (default 10)
     * @return True if edge was added successfully, false on type mismatch
     */
    template <typename SrcNode, std::size_t SrcPort, typename DstNode, std::size_t DstPort>
    static bool AddEdge(std::shared_ptr<graph::GraphManager> g,
                        std::shared_ptr<graph::NodeFacadeAdapterWrapper> src_wrapper,
                        std::shared_ptr<graph::NodeFacadeAdapterWrapper> dst_wrapper,
                        size_t buffer_size = 10)
    {
        auto src = src_wrapper->GetNode<SrcNode>();
        auto dst = dst_wrapper->GetNode<DstNode>();
        if (!src)
        {
            return false;
        }
        if (!dst)
        {
            return false;
        }
        g->AddEdge<SrcNode, SrcPort, DstNode, DstPort>(src, dst, buffer_size);
        return true;
    }
};

} // namespace test
