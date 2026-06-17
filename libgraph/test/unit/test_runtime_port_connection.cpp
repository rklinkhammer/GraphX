/**
 * @file test_runtime_port_connection.cpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX contributors

#include <gtest/gtest.h>

#include "graph/PortFunction.hpp"
#include "graph/PortTypes.hpp"

namespace {

using OutputIntPort = graph::Port<int, 0>;
using InputIntPort = graph::Port<int, 1>;
using InputFloatPort = graph::Port<float, 2>;
using OutputOtherIntPort = graph::Port<int, 3>;

TEST(RuntimePortConnectionTest, ConnectToReportsQueueTransportType) {
    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);

    EXPECT_FALSE(output.GetTransportTypeName().empty());
    EXPECT_NE(output.GetTransportTypeName().find("ActiveQueue"), std::string_view::npos);
}

TEST(RuntimePortConnectionTest, ConnectToAcceptsMatchingOutputToInput) {
    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputIntPort> input(graph::PortDirection::Input);

    auto result = output.ConnectTo(input, 16);

    ASSERT_TRUE(result);
}

TEST(RuntimePortConnectionTest, ConnectToRejectsDirectionMismatch) {
    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<OutputOtherIntPort> other_output(graph::PortDirection::Output);

    auto result = output.ConnectTo(other_output, 8);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), graph::RuntimePortConnectError::DirectionMismatch);
}

TEST(RuntimePortConnectionTest, ConnectToRejectsPayloadMismatch) {
    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputFloatPort> input(graph::PortDirection::Input);

    auto result = output.ConnectTo(input, 8);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), graph::RuntimePortConnectError::PayloadTypeMismatch);
}

TEST(RuntimePortConnectionTest, TransferToMovesPayloadFromOutputToInput) {
    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputIntPort> input(graph::PortDirection::Input);

    ASSERT_TRUE(output.ConnectTo(input, 8));
    ASSERT_TRUE(output.GetQueue().Enqueue(42));

    auto moved = output.TransferTo(input);

    ASSERT_TRUE(moved);
    EXPECT_TRUE(*moved);

    int value = 0;
    ASSERT_TRUE(input.GetQueue().DequeueNonBlocking(value));
    EXPECT_EQ(value, 42);
}

}  // namespace