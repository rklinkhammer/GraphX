// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "accelgraph/TransferGraphTypes.hpp"
#include "config/Config.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

namespace accelgraph {

class HostIngressNode : public graph::NamedSourceNode<HostIngressNode, HostBufferToken>,
                        public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 7> Fields() {
        return {
            graph::JsonField{.name = "session_key", .type = graph::JsonType::String, .required = true,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "graph.default",
                             .enum_values = std::nullopt, .description = "Accelerator session key"},
            graph::JsonField{.name = "provider_id", .type = graph::JsonType::String, .required = true,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "cpu.default",
                             .enum_values = std::nullopt, .description = "Accelerator provider id"},
            graph::JsonField{.name = "backend", .type = graph::JsonType::String, .required = true,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "cpu",
                             .enum_values = std::nullopt, .description = "Accelerator backend (cpu or metal)"},
            graph::JsonField{.name = "payload_size", .type = graph::JsonType::Integer, .required = false,
                             .min = 0.0, .max = std::nullopt, .default_value = "0",
                             .enum_values = std::nullopt, .description = "Ingress payload byte count"},
            graph::JsonField{.name = "payload_multiplier", .type = graph::JsonType::Integer, .required = false,
                             .min = 0.0, .max = std::nullopt, .default_value = "13",
                             .enum_values = std::nullopt, .description = "Payload byte pattern multiplier"},
            graph::JsonField{.name = "payload_offset", .type = graph::JsonType::Integer, .required = false,
                             .min = 0.0, .max = std::nullopt, .default_value = "5",
                             .enum_values = std::nullopt, .description = "Payload byte pattern offset"},
            graph::JsonField{.name = "debug_label", .type = graph::JsonType::String, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "host-ingress",
                             .enum_values = std::nullopt, .description = "Debug label used for host allocation"},
        };
    }

    void Configure(const graph::JsonView& cfg) override;

    std::optional<HostBufferToken> Produce(std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] std::expected<HostIngressOutput, AcceleratorError>
    Execute(std::span<const std::byte> input_bytes, const std::string& debug_label = "host-ingress");

    void Stop() override;

private:
    std::shared_ptr<IAcceleratorSession> session_;
    std::vector<HostAllocationHandle> leases_;
    std::vector<std::byte> staged_input_;
    std::string staged_debug_label_{"host-ingress"};
};

class HostToDeviceNode
    : public graph::NamedInteriorNode<
          graph::TypeList<HostBufferToken>,
          graph::TypeList<HostToDeviceOutput>,
          HostToDeviceNode>,
      public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 4> Fields() {
        return {
            graph::JsonField{.name = "session_key", .type = graph::JsonType::String, .required = true,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "graph.default",
                             .enum_values = std::nullopt, .description = "Accelerator session key"},
            graph::JsonField{.name = "provider_id", .type = graph::JsonType::String, .required = true,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "cpu.default",
                             .enum_values = std::nullopt, .description = "Accelerator provider id"},
            graph::JsonField{.name = "backend", .type = graph::JsonType::String, .required = true,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "cpu",
                             .enum_values = std::nullopt, .description = "Accelerator backend (cpu or metal)"},
            graph::JsonField{.name = "debug_label", .type = graph::JsonType::String, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "h2d-transfer",
                             .enum_values = std::nullopt, .description = "Debug label prefix for H2D operations"},
        };
    }

    void Configure(const graph::JsonView& cfg) override;

    std::optional<HostToDeviceOutput> Transfer(
        const HostBufferToken& host_buffer,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] std::expected<HostToDeviceOutput, AcceleratorError>
    Execute(const HostBufferToken& host_buffer, const std::string& debug_label = "h2d-transfer");

    void Stop() override;

private:
    [[nodiscard]] std::expected<HostToDeviceOutput, AcceleratorError>
    ExecuteImpl(const HostBufferToken& host_buffer, const std::string& debug_label);

    std::shared_ptr<IAcceleratorSession> session_;
    std::string default_debug_label_{"h2d-transfer"};
    std::vector<DeviceAllocationHandle> device_leases_;
    std::vector<QueueHandle> queue_leases_;
    std::vector<TransferCompletion> completion_leases_;
};

class DeviceToHostNode
    : public graph::NamedInteriorNode<
          graph::TypeList<HostToDeviceOutput>,
          graph::TypeList<DeviceToHostOutput>,
          DeviceToHostNode>,
      public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 4> Fields() {
        return {
            graph::JsonField{.name = "session_key", .type = graph::JsonType::String, .required = true,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "graph.default",
                             .enum_values = std::nullopt, .description = "Accelerator session key"},
            graph::JsonField{.name = "provider_id", .type = graph::JsonType::String, .required = true,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "cpu.default",
                             .enum_values = std::nullopt, .description = "Accelerator provider id"},
            graph::JsonField{.name = "backend", .type = graph::JsonType::String, .required = true,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "cpu",
                             .enum_values = std::nullopt, .description = "Accelerator backend (cpu or metal)"},
            graph::JsonField{.name = "debug_label", .type = graph::JsonType::String, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "d2h-transfer",
                             .enum_values = std::nullopt, .description = "Debug label prefix for D2H operations"},
        };
    }

    void Configure(const graph::JsonView& cfg) override;

    std::optional<DeviceToHostOutput> Transfer(
        const HostToDeviceOutput& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] std::expected<DeviceToHostOutput, AcceleratorError>
    Execute(const HostToDeviceOutput& input, const std::string& debug_label = "d2h-transfer");

    void Stop() override;

private:
    [[nodiscard]] std::expected<DeviceToHostOutput, AcceleratorError>
    ExecuteImpl(const HostToDeviceOutput& input, const std::string& debug_label);

    std::shared_ptr<IAcceleratorSession> session_;
    std::string default_debug_label_{"d2h-transfer"};
    std::vector<HostAllocationHandle> host_leases_;
    std::vector<TransferCompletion> completion_leases_;
};

class HostEgressNode : public graph::NamedSinkNode<HostEgressNode, DeviceToHostOutput>,
                       public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 3> Fields() {
        return {
            graph::JsonField{.name = "session_key", .type = graph::JsonType::String, .required = true,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "graph.default",
                             .enum_values = std::nullopt, .description = "Accelerator session key"},
            graph::JsonField{.name = "provider_id", .type = graph::JsonType::String, .required = true,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "cpu.default",
                             .enum_values = std::nullopt, .description = "Accelerator provider id"},
            graph::JsonField{.name = "backend", .type = graph::JsonType::String, .required = true,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "cpu",
                             .enum_values = std::nullopt, .description = "Accelerator backend (cpu or metal)"},
        };
    }

    void Configure(const graph::JsonView& cfg) override;

    bool Consume(const DeviceToHostOutput& transfer_output,
                 std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] std::expected<std::vector<std::byte>, AcceleratorError>
    Execute(const HostBufferToken& host_buffer) const;

    [[nodiscard]] std::expected<std::vector<std::byte>, AcceleratorError>
    Execute(const DeviceToHostOutput& transfer_output) const;

    [[nodiscard]] const std::vector<std::byte>& LastPayload() const noexcept;

    void Stop() override;

private:
    std::shared_ptr<IAcceleratorSession> session_;
    std::vector<std::byte> last_payload_;
};

class ReleaseLeaseNode : public graph::NamedSinkNode<ReleaseLeaseNode, ReleaseLeaseInput>,
                         public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 3> Fields() {
        return {
            graph::JsonField{.name = "session_key", .type = graph::JsonType::String, .required = true,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "graph.default",
                             .enum_values = std::nullopt, .description = "Accelerator session key"},
            graph::JsonField{.name = "provider_id", .type = graph::JsonType::String, .required = true,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "cpu.default",
                             .enum_values = std::nullopt, .description = "Accelerator provider id"},
            graph::JsonField{.name = "backend", .type = graph::JsonType::String, .required = true,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "cpu",
                             .enum_values = std::nullopt, .description = "Accelerator backend (cpu or metal)"},
        };
    }

    void Configure(const graph::JsonView& cfg) override;

    bool Consume(const ReleaseLeaseInput& release,
                 std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] std::expected<ReleaseResult, AcceleratorError>
    Execute(const HostBufferToken& token, const ReleaseRequest& request = {});

    [[nodiscard]] std::expected<ReleaseResult, AcceleratorError>
    Execute(const DeviceBufferToken& token, const ReleaseRequest& request = {});

    [[nodiscard]] std::expected<ReleaseResult, AcceleratorError>
    Execute(const QueueToken& token, const ReleaseRequest& request = {});

    [[nodiscard]] std::expected<ReleaseResult, AcceleratorError>
    Execute(const TransferCompletionToken& token, const ReleaseRequest& request = {});

    void Stop() override;

private:
    [[nodiscard]] std::expected<ReleaseResult, AcceleratorError>
    ExecuteTokenVariant(const ReleaseLeaseTokenVariant& token, const ReleaseRequest& request);

    std::shared_ptr<IAcceleratorSession> session_;
};

}  // namespace accelgraph
