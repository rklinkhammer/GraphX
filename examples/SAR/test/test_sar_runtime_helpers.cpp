#include <gtest/gtest.h>

#include "sar/SarRuntimeHelpers.hpp"

namespace {

TEST(SarRuntimeHelpersTest, ElapsedUsReturnsAtLeastOneMicrosecond) {
    const auto start = sar::runtime::SteadyClock::now();
    const auto elapsed_us = sar::runtime::ElapsedUs(start);
    EXPECT_GE(elapsed_us, 1u);
}

TEST(SarRuntimeHelpersTest, ResolveDiagnosticsSinkReturnsNullForNullGraphManager) {
    const std::shared_ptr<graph::GraphManager> graph_manager;
    auto sink = sar::runtime::ResolveDiagnosticsSink(graph_manager);
    EXPECT_EQ(sink, nullptr);
}

} // namespace