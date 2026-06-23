// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "graph/PortTypes.hpp"
#include "graph/RoutedFunctions.hpp"

#include <cstddef>
#include <optional>

namespace {

class RoutedInputTestNode
    : public graph::RoutedInputFn<graph::Port<int, 3>, RoutedInputTestNode> {
public:
  template <std::size_t Port>
  bool ConsumeInput(const int &value) {
    static_assert(Port == 3);
    consumed_value = value;
    return value > 0;
  }

  int consumed_value = 0;
};

class RoutedOutputTestNode
    : public graph::RoutedOutputFn<graph::Port<int, 4>, RoutedOutputTestNode> {
public:
  template <std::size_t Port>
  std::optional<int> ProduceOutput() {
    static_assert(Port == 4);
    return produced_value;
  }

  int produced_value = 0;
};

class RoutedTransferTestNode
    : public graph::RoutedTransferFn<graph::Port<int, 1>,
                                     graph::Port<double, 2>,
                                     RoutedTransferTestNode> {
public:
  template <std::size_t InputPort, std::size_t OutputPort>
  std::optional<double> TransferInputToOutput(const int &value) {
    static_assert(InputPort == 1);
    static_assert(OutputPort == 2);
    return static_cast<double>(value) * scale;
  }

  double scale = 0.5;
};

class RoutedTransferNoOutputTestNode
    : public graph::RoutedTransferFn<graph::Port<int, 0>,
                                     graph::Port<int, 1>,
                                     RoutedTransferNoOutputTestNode> {
public:
  template <std::size_t InputPort, std::size_t OutputPort>
  std::optional<int> TransferInputToOutput(const int &value) {
    static_assert(InputPort == 0);
    static_assert(OutputPort == 1);
    observed_value = value;
    return std::nullopt;
  }

  int observed_value = 0;
};

TEST(RoutedFunctionsTest, RoutedInputForwardsToTemplatedConsumeInput) {
  RoutedInputTestNode node;

  EXPECT_TRUE(
      node.Consume(7, std::integral_constant<std::size_t, 3>{}));
  EXPECT_EQ(node.consumed_value, 7);

  EXPECT_FALSE(
      node.Consume(-1, std::integral_constant<std::size_t, 3>{}));
  EXPECT_EQ(node.consumed_value, -1);
}

TEST(RoutedFunctionsTest, RoutedOutputForwardsToTemplatedProduceOutput) {
  RoutedOutputTestNode node;
  node.produced_value = 42;

  const auto output =
      node.Produce(std::integral_constant<std::size_t, 4>{});

  ASSERT_TRUE(output.has_value());
  EXPECT_EQ(*output, 42);
}

TEST(RoutedFunctionsTest, RoutedTransferForwardsToTemplatedTransfer) {
  RoutedTransferTestNode node;
  node.scale = 1.25;

  const auto output =
      node.Transfer(8, std::integral_constant<std::size_t, 1>{},
                    std::integral_constant<std::size_t, 2>{});

  ASSERT_TRUE(output.has_value());
  EXPECT_DOUBLE_EQ(*output, 10.0);
}

TEST(RoutedFunctionsTest, RoutedTransferMayReturnNoOutput) {
  RoutedTransferNoOutputTestNode node;

  const auto output =
      node.Transfer(99, std::integral_constant<std::size_t, 0>{},
                    std::integral_constant<std::size_t, 1>{});

  EXPECT_FALSE(output.has_value());
  EXPECT_EQ(node.observed_value, 99);
}

} // namespace
