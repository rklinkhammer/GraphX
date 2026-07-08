// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <string>

#include "accelgraph/AccelGraphScaffold.hpp"

TEST(AccelGraphSmokeTest, ReportsPhase0ScaffoldState) {
    const std::string summary = accelgraph::BuildScaffoldSummary();

    EXPECT_NE(summary.find("libaccelgraph"), std::string::npos);
    EXPECT_NE(summary.find("phase0 scaffold"), std::string::npos);
    EXPECT_NE(summary.find("node_provider_registered=false"), std::string::npos);
    EXPECT_NE(summary.find("metal_enabled=false"), std::string::npos);
    EXPECT_NE(summary.find("cuda_enabled=false"), std::string::npos);
}