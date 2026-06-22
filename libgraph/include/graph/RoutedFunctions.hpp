/**
 * @file RoutedFunctions.hpp
 * @brief CRTP routing wrappers for GraphX input, output, and transfer ports.
 */
// SPDX-License-Identifier: MIT

#pragma once

#include "graph/InputFunction.hpp"
#include "graph/OutputFunction.hpp"
#include "graph/TransferFunction.hpp"

#include <cstddef>
#include <optional>

namespace graph {

template <typename PortT, typename Derived> class RoutedInputFn
    : public InputFn<PortT> {
public:
  using InputType = typename PortT::type;
  static constexpr std::size_t port_id = PortT::id;

  bool Consume(const InputType &input,
               std::integral_constant<std::size_t, port_id>) override {
    return static_cast<Derived *>(this)->template ConsumeInput<port_id>(input);
  }
};

template <typename PortT, typename Derived> class RoutedOutputFn
    : public OutputFn<PortT> {
public:
  using OutputType = typename PortT::type;
  static constexpr std::size_t port_id = PortT::id;

  std::optional<OutputType>
  Produce(std::integral_constant<std::size_t, port_id>) override {
    return static_cast<Derived *>(this)->template ProduceOutput<port_id>();
  }
};

template <typename InputPortT, typename OutputPortT, typename Derived>
class RoutedTransferFn : public TransferFn<InputPortT, OutputPortT> {
public:
  using InputType = typename InputPortT::type;
  using OutputType = typename OutputPortT::type;
  static constexpr std::size_t input_port_id = InputPortT::id;
  static constexpr std::size_t output_port_id = OutputPortT::id;

  std::optional<OutputType> Transfer(
      const InputType &input,
      std::integral_constant<std::size_t, input_port_id>,
      std::integral_constant<std::size_t, output_port_id>) override {
    return static_cast<Derived *>(this)
        ->template TransferInputToOutput<input_port_id, output_port_id>(input);
  }
};

} // namespace graph
