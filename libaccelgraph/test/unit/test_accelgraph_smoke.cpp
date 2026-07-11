// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <string>

#include "accelgraph/AccelGraphScaffold.hpp"

TEST(AccelGraphScaffoldTest, ReportsScaffoldState) {
    const std::string summary = accelgraph::BuildScaffoldSummary();
    const std::string expected_metal =
        std::string("metal_enabled=") + (ACCELGRAPH_ENABLE_METAL ? "true" : "false");
    const std::string expected_cuda =
        std::string("cuda_enabled=") + (ACCELGRAPH_ENABLE_CUDA ? "true" : "false");

    EXPECT_NE(summary.find("libaccelgraph"), std::string::npos);
    EXPECT_NE(summary.find("scaffold"), std::string::npos);
    EXPECT_NE(summary.find("node_provider_registered=false"), std::string::npos);
    EXPECT_NE(summary.find(expected_metal), std::string::npos);
    EXPECT_NE(summary.find(expected_cuda), std::string::npos);
}
