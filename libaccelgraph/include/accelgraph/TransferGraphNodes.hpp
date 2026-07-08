// SPDX-License-Identifier: MIT

#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "accelgraph/AcceleratorSessionRegistry.hpp"
#include "accelgraph/TransferGraphTypes.hpp"
#include "graph/NamedNodes.hpp"

namespace accelgraph {

class HostIngressNode : public graph::NamedSourceNode<HostIngressNode, HostIngressOutput> {
public:
    bool Initialize(AcceleratorSessionRegistry& registry, const std::string& session_key);

    void StageInput(std::span<const std::byte> input_bytes,
                    const std::string& debug_label = "host-ingress");

    std::optional<HostIngressOutput> Produce(std::integral_constant<std::size_t, 0>) override;

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
          HostToDeviceNode> {
public:
    bool Initialize(AcceleratorSessionRegistry& registry, const std::string& session_key);

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
    std::vector<DeviceAllocationHandle> device_leases_;
    std::vector<QueueHandle> queue_leases_;
    std::vector<TransferCompletion> completion_leases_;
};

class DeviceToHostNode
    : public graph::NamedInteriorNode<
          graph::TypeList<HostToDeviceOutput>,
          graph::TypeList<DeviceToHostOutput>,
          DeviceToHostNode> {
public:
    bool Initialize(AcceleratorSessionRegistry& registry, const std::string& session_key);

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
    std::vector<HostAllocationHandle> host_leases_;
    std::vector<TransferCompletion> completion_leases_;
};

class HostEgressNode : public graph::NamedSinkNode<HostEgressNode, HostBufferToken> {
public:
    bool Initialize(AcceleratorSessionRegistry& registry, const std::string& session_key);

    bool Consume(const HostBufferToken& host_buffer,
                 std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] std::expected<std::vector<std::byte>, AcceleratorError>
    Execute(const HostBufferToken& host_buffer) const;

    [[nodiscard]] const std::vector<std::byte>& LastPayload() const noexcept;

    void Stop() override;

private:
    std::shared_ptr<IAcceleratorSession> session_;
    std::vector<std::byte> last_payload_;
};

class ReleaseLeaseNode : public graph::NamedSinkNode<ReleaseLeaseNode, ReleaseLeaseInput> {
public:
    bool Initialize(AcceleratorSessionRegistry& registry, const std::string& session_key);

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
