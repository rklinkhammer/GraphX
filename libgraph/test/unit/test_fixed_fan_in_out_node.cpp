// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "graph/FixedFanInOutNode.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>

namespace {

class FixedFanInOutSmokeNode
    : public graph::NamedFixedFanInOutNode<
          FixedFanInOutSmokeNode, graph::TypeList<int, std::string>,
          graph::TypeList<double, std::string>> {
public:
  using Base = graph::NamedFixedFanInOutNode<
      FixedFanInOutSmokeNode, graph::TypeList<int, std::string>,
      graph::TypeList<double, std::string>>;

  template <std::size_t Port>
  bool ConsumeInput(const typename Base::template InputType<Port> &input) {
    if constexpr (Port == 0) {
      last_int = input;
      return input > 0;
    } else {
      last_string = input;
      return !input.empty();
    }
  }

  template <std::size_t Port>
  std::optional<typename Base::template OutputType<Port>> ProduceOutput() {
    return Base::template DequeueOutput<Port>();
  }

  template <std::size_t InputPort, std::size_t OutputPort>
  std::optional<typename Base::template OutputType<OutputPort>>
  TransferInputToOutput(
      const typename Base::template InputType<InputPort> &input) {
    if constexpr (InputPort == 0 && OutputPort == 0) {
      return static_cast<double>(input) * 0.5;
    } else if constexpr (InputPort == 1 && OutputPort == 1) {
      return input + ":routed";
    } else {
      return std::nullopt;
    }
  }

  int last_int = 0;
  std::string last_string;
};

static_assert(FixedFanInOutSmokeNode::NInputs == 2);
static_assert(FixedFanInOutSmokeNode::NOutputs == 2);
static_assert(std::is_same_v<FixedFanInOutSmokeNode::InputType<0>, int>);
static_assert(
    std::is_same_v<FixedFanInOutSmokeNode::InputType<1>, std::string>);
static_assert(std::is_same_v<FixedFanInOutSmokeNode::OutputType<0>, double>);
static_assert(
    std::is_same_v<FixedFanInOutSmokeNode::OutputType<1>, std::string>);

TEST(FixedFanInOutNodeTest, RoutesConsumeProduceAndTransferPorts) {
  FixedFanInOutSmokeNode node;

  EXPECT_EQ(node.GetInputPortCount(), 2);
  EXPECT_EQ(node.GetOutputPortCount(), 2);
  ASSERT_EQ(node.InputPorts().size(), 2u);
  ASSERT_EQ(node.OutputPorts().size(), 2u);

  EXPECT_TRUE(node.Consume(8, std::integral_constant<std::size_t, 0>{}));
  EXPECT_EQ(node.last_int, 8);
  EXPECT_TRUE(
      node.Consume(std::string{"alpha"},
                   std::integral_constant<std::size_t, 1>{}));
  EXPECT_EQ(node.last_string, "alpha");

  ASSERT_TRUE(node.EnqueueOutput<0>(3.5));
  ASSERT_TRUE(node.EnqueueOutput<1>(std::string{"queued"}));
  auto produced_double =
      node.Produce(std::integral_constant<std::size_t, 0>{});
  auto produced_string =
      node.Produce(std::integral_constant<std::size_t, 1>{});
  ASSERT_TRUE(produced_double.has_value());
  ASSERT_TRUE(produced_string.has_value());
  EXPECT_DOUBLE_EQ(*produced_double, 3.5);
  EXPECT_EQ(*produced_string, "queued");

  auto transferred_double =
      node.Transfer(10, std::integral_constant<std::size_t, 0>{},
                    std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(transferred_double.has_value());
  EXPECT_DOUBLE_EQ(*transferred_double, 5.0);

  auto transferred_string =
      node.Transfer(std::string{"beta"},
                    std::integral_constant<std::size_t, 1>{},
                    std::integral_constant<std::size_t, 1>{});
  ASSERT_TRUE(transferred_string.has_value());
  EXPECT_EQ(*transferred_string, "beta:routed");
}

TEST(FixedFanInOutNodeTest, TransferNulloptDoesNotQueueOutput) {
  FixedFanInOutSmokeNode node;

  const auto no_output =
      node.Transfer(10, std::integral_constant<std::size_t, 0>{},
                    std::integral_constant<std::size_t, 1>{});
  EXPECT_FALSE(no_output.has_value());
}

} // namespace
