/**
 * @file FixedFanInOutNode.hpp
 * @brief GraphX base for fixed fan-in/fan-out nodes with routed ports.
 */
// SPDX-License-Identifier: MIT

#pragma once

#include "core/ActiveQueue.hpp"
#include "graph/Lifecycle.hpp"
#include "graph/NamedType.hpp"
#include "graph/PortTypes.hpp"
#include "graph/RoutedFunctions.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <tuple>

namespace graph {

template <typename First, typename TypeList> struct PrependTypeList;

template <typename First, typename... Rest>
struct PrependTypeList<First, TypeList<Rest...>> {
  using type = TypeList<First, Rest...>;
};

template <typename First, typename TypeList>
using PrependTypeList_t = typename PrependTypeList<First, TypeList>::type;

template <std::size_t Index, typename TypeList> struct TypeListElement;

template <std::size_t Index, typename... Types>
struct TypeListElement<Index, TypeList<Types...>> {
  using type = typename std::tuple_element<Index, std::tuple<Types...>>::type;
};

template <std::size_t Index, typename TypeList>
using TypeListElement_t = typename TypeListElement<Index, TypeList>::type;

namespace detail {

template <typename Derived, typename InputPorts, typename OutputPorts>
class FixedFanInOutNodeBase;

template <typename Derived, typename... InputPortTs, typename... OutputPortTs>
class FixedFanInOutNodeBase<Derived, TypeList<InputPortTs...>,
                            TypeList<OutputPortTs...>>
    : public NodeLifecycleMixin<FixedFanInOutNodeBase<
          Derived, TypeList<InputPortTs...>, TypeList<OutputPortTs...>>>,
      public RoutedInputFn<InputPortTs, Derived>...,
      public RoutedOutputFn<OutputPortTs, Derived>... {
public:
  using RoutedInputFn<InputPortTs, Derived>::Consume...;
  using RoutedOutputFn<OutputPortTs, Derived>::Produce...;

  using InputPortList = TypeList<InputPortTs...>;
  using OutputPortList = TypeList<OutputPortTs...>;

  template <std::size_t PortID>
  using InputPortType = TypeListElement_t<PortID, InputPortList>;
  template <std::size_t PortID>
  using OutputPortType = TypeListElement_t<PortID, OutputPortList>;
  template <std::size_t PortID>
  using InputType = typename InputPortType<PortID>::type;
  template <std::size_t PortID>
  using OutputType = typename OutputPortType<PortID>::type;

  static constexpr std::size_t NInputs = sizeof...(InputPortTs);
  static constexpr std::size_t NOutputs = sizeof...(OutputPortTs);

  static consteval auto build_inputs() {
    return build_port_table<PortDirection::Input>(InputPortList{});
  }
  static consteval auto build_outputs() {
    return build_port_table<PortDirection::Output>(OutputPortList{});
  }

  static constexpr auto input_table = build_inputs();
  static constexpr auto output_table = build_outputs();

  std::span<const PortInfo> InputPorts() const final { return input_table; }
  std::span<const PortInfo> OutputPorts() const final { return output_table; }

  int GetInputPortCount() const { return static_cast<int>(NInputs); }
  int GetOutputPortCount() const { return static_cast<int>(NOutputs); }

  template <std::size_t PortID>
  bool EnqueueOutput(const OutputType<PortID> &output) {
    return std::get<PortID>(output_queues_).Enqueue(output);
  }

  template <std::size_t PortID>
  std::optional<OutputType<PortID>> DequeueOutput() {
    OutputType<PortID> output{};
    if (std::get<PortID>(output_queues_).Dequeue(output)) {
      return output;
    }
    return std::nullopt;
  }

  template <std::size_t PortID>
  std::optional<OutputType<PortID>> ProduceOutput() {
    return DequeueOutput<PortID>();
  }

  const core::QueueMetrics *GetInputQueueMetrics(std::size_t port_id) const {
    const core::QueueMetrics *result = nullptr;
    GetInputQueueMetricsImpl<0>(port_id, result);
    return result;
  }

  const core::QueueMetrics *GetOutputQueueMetrics(std::size_t port_id) const {
    const core::QueueMetrics *result = nullptr;
    GetOutputQueueMetricsImpl<0>(port_id, result);
    return result;
  }

  const ThreadMetrics *GetInputPortThreadMetrics(std::size_t port_id) const {
    const ThreadMetrics *result = nullptr;
    GetInputPortThreadMetricsImpl<0>(port_id, result);
    return result;
  }

  const ThreadMetrics *GetOutputPortThreadMetrics(std::size_t port_id) const {
    const ThreadMetrics *result = nullptr;
    GetOutputPortThreadMetricsImpl<0>(port_id, result);
    return result;
  }

  void EnableInputMetrics(bool enabled = true) {
    (RoutedInputFn<InputPortTs, Derived>::EnableMetrics(enabled), ...);
  }

  void EnableOutputMetrics(bool enabled = true) {
    (RoutedOutputFn<OutputPortTs, Derived>::EnableMetrics(enabled), ...);
  }

  void EnableMetrics(bool enabled = true) {
    EnableInputMetrics(enabled);
    EnableOutputMetrics(enabled);
  }

  void DisableMetrics() { EnableMetrics(false); }

  void ResetMetrics() {
    (RoutedInputFn<InputPortTs, Derived>::ResetMetrics(), ...);
    (RoutedOutputFn<OutputPortTs, Derived>::ResetMetrics(), ...);
  }

protected:
  ~FixedFanInOutNodeBase() override = default;

public:
  bool InitPortsImpl() {
    bool ok = true;
    ((ok = RoutedInputFn<InputPortTs, Derived>::Init() && ok), ...);
    ((ok = RoutedOutputFn<OutputPortTs, Derived>::Init() && ok), ...);
    return ok;
  }

  bool StartPortsImpl() {
    bool ok = true;
    ((ok = RoutedInputFn<InputPortTs, Derived>::Start() && ok), ...);
    ((ok = RoutedOutputFn<OutputPortTs, Derived>::Start() && ok), ...);
    return ok;
  }

  void StopPortsImpl() {
    (RoutedInputFn<InputPortTs, Derived>::Stop(), ...);
    (RoutedOutputFn<OutputPortTs, Derived>::Stop(), ...);
    DisableOutputQueues();
  }

  void JoinPortsImpl() {
    (RoutedInputFn<InputPortTs, Derived>::Join(), ...);
    (RoutedOutputFn<OutputPortTs, Derived>::Join(), ...);
  }

  bool JoinWithTimeoutPortsImpl(std::chrono::milliseconds timeout_ms) {
    constexpr auto port_count = NInputs + NOutputs;
    const auto per_port_timeout =
        timeout_ms / std::max<std::size_t>(1, port_count);
    bool ok = true;
    ((ok = RoutedInputFn<InputPortTs, Derived>::JoinWithTimeout(
               per_port_timeout) &&
           ok),
     ...);
    ((ok = RoutedOutputFn<OutputPortTs, Derived>::JoinWithTimeout(
               per_port_timeout) &&
           ok),
     ...);
    return ok;
  }

  void DisableOutputQueues() { DisableOutputQueuesImpl<0>(); }

  template <std::size_t Port>
  void DisableOutputQueuesImpl() {
    if constexpr (Port < NOutputs) {
      std::get<Port>(output_queues_).Disable();
      DisableOutputQueuesImpl<Port + 1>();
    }
  }

  template <std::size_t Port>
  void GetInputQueueMetricsImpl(std::size_t port_id,
                                const core::QueueMetrics *&result) const {
    if constexpr (Port < NInputs) {
      if (port_id == Port) {
        auto *mutable_this = const_cast<FixedFanInOutNodeBase *>(this);
        auto *fn =
            static_cast<RoutedInputFn<InputPortType<Port>, Derived> *>(
                mutable_this);
        result = &fn->GetQueue().GetMetrics();
      } else {
        GetInputQueueMetricsImpl<Port + 1>(port_id, result);
      }
    }
  }

  template <std::size_t Port>
  void GetOutputQueueMetricsImpl(std::size_t port_id,
                                 const core::QueueMetrics *&result) const {
    if constexpr (Port < NOutputs) {
      if (port_id == Port) {
        result = &std::get<Port>(output_queues_).GetMetrics();
      } else {
        GetOutputQueueMetricsImpl<Port + 1>(port_id, result);
      }
    }
  }

  template <std::size_t Port>
  void GetInputPortThreadMetricsImpl(std::size_t port_id,
                                     const ThreadMetrics *&result) const {
    if constexpr (Port < NInputs) {
      if (port_id == Port) {
        auto *mutable_this = const_cast<FixedFanInOutNodeBase *>(this);
        auto *fn =
            static_cast<RoutedInputFn<InputPortType<Port>, Derived> *>(
                mutable_this);
        result = &fn->GetThreadMetrics();
      } else {
        GetInputPortThreadMetricsImpl<Port + 1>(port_id, result);
      }
    }
  }

  template <std::size_t Port>
  void GetOutputPortThreadMetricsImpl(std::size_t port_id,
                                      const ThreadMetrics *&result) const {
    if constexpr (Port < NOutputs) {
      if (port_id == Port) {
        auto *mutable_this = const_cast<FixedFanInOutNodeBase *>(this);
        auto *fn =
            static_cast<RoutedOutputFn<OutputPortType<Port>, Derived> *>(
                mutable_this);
        result = &fn->GetThreadMetrics();
      } else {
        GetOutputPortThreadMetricsImpl<Port + 1>(port_id, result);
      }
    }
  }

  std::tuple<core::ActiveQueue<typename OutputPortTs::type>...> output_queues_;
};

} // namespace detail

template <typename Derived, typename InputList, typename OutputList>
class NamedFixedFanInOutNode;

template <typename Derived, typename... Inputs, typename... Outputs>
class NamedFixedFanInOutNode<Derived, TypeList<Inputs...>,
                             TypeList<Outputs...>>
    : public detail::FixedFanInOutNodeBase<
          Derived, typename MakePorts<TypeList<Inputs...>>::type,
          typename MakePorts<TypeList<Outputs...>>::type>,
      public NamedType<Derived> {
public:
  using Base = detail::FixedFanInOutNodeBase<
      Derived, typename MakePorts<TypeList<Inputs...>>::type,
      typename MakePorts<TypeList<Outputs...>>::type>;

  using Base::Base;
  using Base::Consume;
  using Base::Produce;

protected:
  ~NamedFixedFanInOutNode() override = default;
};

} // namespace graph
