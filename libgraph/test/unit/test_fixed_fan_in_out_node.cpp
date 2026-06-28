// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "graph/FixedFanInOutNode.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

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

TEST(FixedFanInOutNodeTest, OutOfOrderEndOfStreamCompletesOnlyAfterAllInputs) {
  FixedFanInOutSmokeNode node;

  ASSERT_TRUE(node.ObserveInputControl<1>(graph::EdgeEndOfStream{}));
  auto status = node.InputCompletionStatus();
  EXPECT_EQ(status.outcome, graph::RequiredInputOutcome::Open);
  EXPECT_EQ(status.terminal_inputs, 1u);
  EXPECT_EQ(status.missing_inputs, std::vector<std::size_t>({0u}));

  ASSERT_TRUE(node.ObserveInputControl<0>(graph::EdgeEndOfStream{}));
  status = node.InputCompletionStatus();
  EXPECT_TRUE(status.IsComplete());
  EXPECT_EQ(status.terminal_inputs, 2u);
}

TEST(FixedFanInOutNodeTest, WatermarksAreMonotonicAndOnlyFinalIsTerminal) {
  FixedFanInOutSmokeNode node;

  EXPECT_TRUE(node.ObserveInputControl<0>(graph::EdgeWatermark{10u, false}));
  EXPECT_FALSE(node.ObserveInputControl<0>(graph::EdgeWatermark{9u, false}));
  EXPECT_TRUE(node.ObserveInputControl<0>(graph::EdgeWatermark{20u, true}));
  EXPECT_EQ(node.InputCompletionStatus().outcome,
            graph::RequiredInputOutcome::Open);

  EXPECT_TRUE(node.ObserveInputControl<1>(graph::EdgeWatermark{15u, true}));
  EXPECT_TRUE(node.InputCompletionStatus().IsComplete());
}

TEST(FixedFanInOutNodeTest, FinalizeReportsExactMissingRequiredInput) {
  FixedFanInOutSmokeNode node;

  ASSERT_TRUE(node.ObserveInputControl<0>(graph::EdgeEndOfStream{}));
  const auto status = node.FinalizeInputCompletion();
  EXPECT_EQ(status.outcome, graph::RequiredInputOutcome::Incomplete);
  EXPECT_EQ(status.required_inputs, 2u);
  EXPECT_EQ(status.terminal_inputs, 1u);
  EXPECT_EQ(status.missing_inputs, std::vector<std::size_t>({1u}));
  EXPECT_EQ(status.detail,
            "required input did not reach a terminal state");
}

TEST(FixedFanInOutNodeTest, CancellationIsTypedAndNeverSuccessful) {
  FixedFanInOutSmokeNode node;

  ASSERT_TRUE(node.ObserveInputControl<1>(
      graph::EdgeCancellation{"operator requested stop"}));
  const auto status = node.InputCompletionStatus();
  EXPECT_EQ(status.outcome, graph::RequiredInputOutcome::Cancelled);
  EXPECT_EQ(status.problem_input, 1u);
  EXPECT_EQ(status.detail, "operator requested stop");
  EXPECT_FALSE(status.IsComplete());
}

TEST(FixedFanInOutNodeTest, FailurePreservesDetailAndNeverSucceeds) {
  FixedFanInOutSmokeNode node;

  ASSERT_TRUE(
      node.ObserveInputControl<0>(graph::EdgeFailure{"detector failed"}));
  const auto status = node.InputCompletionStatus();
  EXPECT_EQ(status.outcome, graph::RequiredInputOutcome::Failed);
  EXPECT_EQ(status.problem_input, 0u);
  EXPECT_EQ(status.detail, "detector failed");
  EXPECT_FALSE(status.IsComplete());
}

TEST(FixedFanInOutNodeTest,
     DuplicateTerminalIsIdempotentAndDataAfterTerminalIsRejected) {
  FixedFanInOutSmokeNode node;

  const graph::EdgeControl eos = graph::EdgeEndOfStream{};
  EXPECT_TRUE(node.ObserveInputControl<0>(eos));
  EXPECT_TRUE(node.ObserveInputControl<0>(eos));
  EXPECT_FALSE(node.ObserveInputControl<0>(graph::EdgeControl{}));
  EXPECT_FALSE(node.ObserveInputControl<0>(
      graph::EdgeFailure{"conflicting terminal"}));
}

} // namespace
