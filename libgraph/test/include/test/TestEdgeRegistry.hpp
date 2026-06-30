// SPDX-License-Identifier: MIT

/**
 * @file TestEdgeRegistry.hpp
 * @brief Test Edge Registry Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#pragma once

#include "graph/EdgeRegistry.hpp"
#include "graph/GraphManagerCore.hpp"
#include "test/AdvancedTestNodes.hpp"
#include <memory>

namespace test {

/**
 * @brief Initialize the edge registry with all test node edge combinations
 *
 * Registers edges for:
 * - SourceTestNode output (port 0) → SinkTestNode input (port 0)
 * - SourceTestNode output (port 0) → InteriorTestNode input (port 0)
 * - InteriorTestNode output (port 0) → SinkTestNode input (port 0)
 * - InteriorTestNode output (port 0) → MergeTestNode inputs (ports 0, 1)
 * - SplitTestNode outputs (ports 0, 1) → SinkTestNode input (port 0)
 * - SplitTestNode outputs (ports 0, 1) → InteriorTestNode input (port 0)
 * - MergeTestNode output (port 0) → SinkTestNode input (port 0)
 * - SourceTestNode output (port 0) → MergeTestNode inputs (ports 0, 1)
 * - SourceTestNode output (port 0) → SplitTestNode input (port 0)
 *
 * This ensures all topology combinations can be built dynamically.
 */
inline void InitializeTestEdgeRegistry() {
    using EdgeReg = graph::config::EdgeRegistry;
    
    // Source -> Sink (MinimalGraph)
    EdgeReg::Register<SourceTestNode, 0, SinkTestNode, 0>(
        "SourceTestNode", "SinkTestNode",
        [](graph::GraphManager& g, std::size_t src_idx, std::size_t dst_idx, std::size_t buf) {
            auto src = std::dynamic_pointer_cast<SourceTestNode>(g.GetNodes()[src_idx]);
            auto dst = std::dynamic_pointer_cast<SinkTestNode>(g.GetNodes()[dst_idx]);
            if (!src || !dst) return false;
            g.AddEdge<SourceTestNode, 0, SinkTestNode, 0>(src, dst, buf);
            return true;
        });
    
    // Source -> Interior (LinearSequential)
    EdgeReg::Register<SourceTestNode, 0, InteriorTestNode, 0>(
        "SourceTestNode", "InteriorTestNode",
        [](graph::GraphManager& g, std::size_t src_idx, std::size_t dst_idx, std::size_t buf) {
            auto src = std::dynamic_pointer_cast<SourceTestNode>(g.GetNodes()[src_idx]);
            auto dst = std::dynamic_pointer_cast<InteriorTestNode>(g.GetNodes()[dst_idx]);
            if (!src || !dst) return false;
            g.AddEdge<SourceTestNode, 0, InteriorTestNode, 0>(src, dst, buf);
            return true;
        });
    
    // Interior -> Sink
    EdgeReg::Register<InteriorTestNode, 0, SinkTestNode, 0>(
        "InteriorTestNode", "SinkTestNode",
        [](graph::GraphManager& g, std::size_t src_idx, std::size_t dst_idx, std::size_t buf) {
            auto src = std::dynamic_pointer_cast<InteriorTestNode>(g.GetNodes()[src_idx]);
            auto dst = std::dynamic_pointer_cast<SinkTestNode>(g.GetNodes()[dst_idx]);
            if (!src || !dst) return false;
            g.AddEdge<InteriorTestNode, 0, SinkTestNode, 0>(src, dst, buf);
            return true;
        });
    
    // Interior -> Interior (MultiPathSequential)
    EdgeReg::Register<InteriorTestNode, 0, InteriorTestNode, 0>(
        "InteriorTestNode", "InteriorTestNode",
        [](graph::GraphManager& g, std::size_t src_idx, std::size_t dst_idx, std::size_t buf) {
            auto src = std::dynamic_pointer_cast<InteriorTestNode>(g.GetNodes()[src_idx]);
            auto dst = std::dynamic_pointer_cast<InteriorTestNode>(g.GetNodes()[dst_idx]);
            if (!src || !dst) return false;
            g.AddEdge<InteriorTestNode, 0, InteriorTestNode, 0>(src, dst, buf);
            return true;
        });
    
    // Source -> Merge (port 0)
    EdgeReg::Register<SourceTestNode, 0, MergeTestNode, 0>(
        "SourceTestNode", "MergeTestNode",
        [](graph::GraphManager& g, std::size_t src_idx, std::size_t dst_idx, std::size_t buf) {
            auto src = std::dynamic_pointer_cast<SourceTestNode>(g.GetNodes()[src_idx]);
            auto dst = std::dynamic_pointer_cast<MergeTestNode>(g.GetNodes()[dst_idx]);
            if (!src || !dst) return false;
            g.AddEdge<SourceTestNode, 0, MergeTestNode, 0>(src, dst, buf);
            return true;
        });
    
    // Source -> Merge (port 1)
    EdgeReg::Register<SourceTestNode, 0, MergeTestNode, 1>(
        "SourceTestNode", "MergeTestNode",
        [](graph::GraphManager& g, std::size_t src_idx, std::size_t dst_idx, std::size_t buf) {
            auto src = std::dynamic_pointer_cast<SourceTestNode>(g.GetNodes()[src_idx]);
            auto dst = std::dynamic_pointer_cast<MergeTestNode>(g.GetNodes()[dst_idx]);
            if (!src || !dst) return false;
            g.AddEdge<SourceTestNode, 0, MergeTestNode, 1>(src, dst, buf);
            return true;
        });
    
    // Interior -> Merge (port 0)
    EdgeReg::Register<InteriorTestNode, 0, MergeTestNode, 0>(
        "InteriorTestNode", "MergeTestNode",
        [](graph::GraphManager& g, std::size_t src_idx, std::size_t dst_idx, std::size_t buf) {
            auto src = std::dynamic_pointer_cast<InteriorTestNode>(g.GetNodes()[src_idx]);
            auto dst = std::dynamic_pointer_cast<MergeTestNode>(g.GetNodes()[dst_idx]);
            if (!src || !dst) return false;
            g.AddEdge<InteriorTestNode, 0, MergeTestNode, 0>(src, dst, buf);
            return true;
        });
    
    // Interior -> Merge (port 1)
    EdgeReg::Register<InteriorTestNode, 0, MergeTestNode, 1>(
        "InteriorTestNode", "MergeTestNode",
        [](graph::GraphManager& g, std::size_t src_idx, std::size_t dst_idx, std::size_t buf) {
            auto src = std::dynamic_pointer_cast<InteriorTestNode>(g.GetNodes()[src_idx]);
            auto dst = std::dynamic_pointer_cast<MergeTestNode>(g.GetNodes()[dst_idx]);
            if (!src || !dst) return false;
            g.AddEdge<InteriorTestNode, 0, MergeTestNode, 1>(src, dst, buf);
            return true;
        });
    
    // Merge -> Sink
    EdgeReg::Register<MergeTestNode, 0, SinkTestNode, 0>(
        "MergeTestNode", "SinkTestNode",
        [](graph::GraphManager& g, std::size_t src_idx, std::size_t dst_idx, std::size_t buf) {
            auto src = std::dynamic_pointer_cast<MergeTestNode>(g.GetNodes()[src_idx]);
            auto dst = std::dynamic_pointer_cast<SinkTestNode>(g.GetNodes()[dst_idx]);
            if (!src || !dst) return false;
            g.AddEdge<MergeTestNode, 0, SinkTestNode, 0>(src, dst, buf);
            return true;
        });
    
    // Source -> Split
    EdgeReg::Register<SourceTestNode, 0, SplitTestNode, 0>(
        "SourceTestNode", "SplitTestNode",
        [](graph::GraphManager& g, std::size_t src_idx, std::size_t dst_idx, std::size_t buf) {
            auto src = std::dynamic_pointer_cast<SourceTestNode>(g.GetNodes()[src_idx]);
            auto dst = std::dynamic_pointer_cast<SplitTestNode>(g.GetNodes()[dst_idx]);
            if (!src || !dst) return false;
            g.AddEdge<SourceTestNode, 0, SplitTestNode, 0>(src, dst, buf);
            return true;
        });
    
    // Split (port 0) -> Sink
    EdgeReg::Register<SplitTestNode, 0, SinkTestNode, 0>(
        "SplitTestNode", "SinkTestNode",
        [](graph::GraphManager& g, std::size_t src_idx, std::size_t dst_idx, std::size_t buf) {
            auto src = std::dynamic_pointer_cast<SplitTestNode>(g.GetNodes()[src_idx]);
            auto dst = std::dynamic_pointer_cast<SinkTestNode>(g.GetNodes()[dst_idx]);
            if (!src || !dst) return false;
            g.AddEdge<SplitTestNode, 0, SinkTestNode, 0>(src, dst, buf);
            return true;
        });
    
    // Split (port 1) -> Sink
    EdgeReg::Register<SplitTestNode, 1, SinkTestNode, 0>(
        "SplitTestNode", "SinkTestNode",
        [](graph::GraphManager& g, std::size_t src_idx, std::size_t dst_idx, std::size_t buf) {
            auto src = std::dynamic_pointer_cast<SplitTestNode>(g.GetNodes()[src_idx]);
            auto dst = std::dynamic_pointer_cast<SinkTestNode>(g.GetNodes()[dst_idx]);
            if (!src || !dst) return false;
            g.AddEdge<SplitTestNode, 1, SinkTestNode, 0>(src, dst, buf);
            return true;
        });
    
    // Split (port 0) -> Interior
    EdgeReg::Register<SplitTestNode, 0, InteriorTestNode, 0>(
        "SplitTestNode", "InteriorTestNode",
        [](graph::GraphManager& g, std::size_t src_idx, std::size_t dst_idx, std::size_t buf) {
            auto src = std::dynamic_pointer_cast<SplitTestNode>(g.GetNodes()[src_idx]);
            auto dst = std::dynamic_pointer_cast<InteriorTestNode>(g.GetNodes()[dst_idx]);
            if (!src || !dst) return false;
            g.AddEdge<SplitTestNode, 0, InteriorTestNode, 0>(src, dst, buf);
            return true;
        });
    
    // Split (port 1) -> Interior
    EdgeReg::Register<SplitTestNode, 1, InteriorTestNode, 0>(
        "SplitTestNode", "InteriorTestNode",
        [](graph::GraphManager& g, std::size_t src_idx, std::size_t dst_idx, std::size_t buf) {
            auto src = std::dynamic_pointer_cast<SplitTestNode>(g.GetNodes()[src_idx]);
            auto dst = std::dynamic_pointer_cast<InteriorTestNode>(g.GetNodes()[dst_idx]);
            if (!src || !dst) return false;
            g.AddEdge<SplitTestNode, 1, InteriorTestNode, 0>(src, dst, buf);
            return true;
        });
}

}  // namespace test
