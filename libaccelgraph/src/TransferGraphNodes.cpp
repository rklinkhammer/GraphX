// SPDX-License-Identifier: MIT

#include "accelgraph/TransferGraphNodes.hpp"

#include <type_traits>
#include <utility>
#include <variant>

namespace accelgraph {

namespace {

bool ResolveSession(AcceleratorSessionRegistry& registry,
                    const std::string& session_key,
                    std::shared_ptr<IAcceleratorSession>& session_out) {
    auto session_result = registry.ResolveExactlyOne(session_key);
    if (!session_result.has_value()) {
        return false;
    }

    session_out = std::move(session_result.value());
    return true;
}

}  // namespace

bool HostIngressNode::Initialize(AcceleratorSessionRegistry& registry, const std::string& session_key) {
    return ResolveSession(registry, session_key, session_);
}

void HostIngressNode::StageInput(std::span<const std::byte> input_bytes,
                                 const std::string& debug_label) {
    staged_input_.assign(input_bytes.begin(), input_bytes.end());
    staged_debug_label_ = debug_label;
}

std::optional<HostBufferToken> HostIngressNode::Produce(std::integral_constant<std::size_t, 0>) {
    if (staged_input_.empty()) {
        return std::nullopt;
    }

    auto produced = Execute(staged_input_, staged_debug_label_);
    staged_input_.clear();
    if (!produced.has_value()) {
        return std::nullopt;
    }

    return produced->host_buffer;
}

std::expected<HostIngressOutput, AcceleratorError>
HostIngressNode::Execute(std::span<const std::byte> input_bytes, const std::string& debug_label) {
    if (!session_) {
        AcceleratorError error;
        error.category = AcceleratorErrorCategory::InvalidState;
        error.operation = "HostIngressNode::Execute";
        error.diagnostic = "node was not initialized";
        return std::unexpected(error);
    }

    auto allocation = session_->AllocateHost(
        HostAllocationRequest{.byte_size = input_bytes.size(), .debug_label = debug_label});
    if (!allocation.has_value()) {
        return std::unexpected(allocation.error());
    }

    auto write_result = session_->WriteHost(
        allocation->handle,
        HostWriteRequest{.source = input_bytes, .destination_offset = 0});
    if (!write_result.has_value()) {
        return std::unexpected(write_result.error());
    }

    leases_.push_back(allocation->handle);

    HostIngressOutput output;
    output.host_buffer = HostBufferToken{
        .handle = allocation->handle,
        .byte_size = allocation->byte_size,
    };
    return output;
}

void HostIngressNode::Stop() {
    if (!session_) {
        graph::NamedSourceNode<HostIngressNode, HostBufferToken>::Stop();
        return;
    }

    for (const auto& lease : leases_) {
        session_->Release(lease, ReleaseRequest{.allow_if_released = true});
    }
    leases_.clear();
    graph::NamedSourceNode<HostIngressNode, HostBufferToken>::Stop();
}

bool HostToDeviceNode::Initialize(AcceleratorSessionRegistry& registry,
                                  const std::string& session_key) {
    return ResolveSession(registry, session_key, session_);
}

std::optional<HostToDeviceOutput> HostToDeviceNode::Transfer(
    const HostBufferToken& host_buffer,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    auto transferred = ExecuteImpl(host_buffer, "h2d-transfer");
    if (!transferred.has_value()) {
        return std::nullopt;
    }

    return transferred.value();
}

std::expected<HostToDeviceOutput, AcceleratorError>
HostToDeviceNode::Execute(const HostBufferToken& host_buffer, const std::string& debug_label) {
    return ExecuteImpl(host_buffer, debug_label);
}

std::expected<HostToDeviceOutput, AcceleratorError>
HostToDeviceNode::ExecuteImpl(const HostBufferToken& host_buffer, const std::string& debug_label) {
    if (!session_) {
        AcceleratorError error;
        error.category = AcceleratorErrorCategory::InvalidState;
        error.operation = "HostToDeviceNode::Execute";
        error.diagnostic = "node was not initialized";
        return std::unexpected(error);
    }

    if (!host_buffer.handle.IsValid() || host_buffer.byte_size == 0) {
        AcceleratorError error;
        error.category = AcceleratorErrorCategory::InvalidArgument;
        error.operation = "HostToDeviceNode::Execute";
        error.diagnostic = "host buffer token is invalid";
        return std::unexpected(error);
    }

    auto device_allocation = session_->AllocateDevice(
        DeviceAllocationRequest{.byte_size = host_buffer.byte_size, .debug_label = debug_label + ".device"});
    if (!device_allocation.has_value()) {
        return std::unexpected(device_allocation.error());
    }

    auto queue = session_->AcquireQueue(QueueRequest{.debug_label = debug_label + ".queue"});
    if (!queue.has_value()) {
        return std::unexpected(queue.error());
    }

    auto transfer = session_->EnqueueHostToDevice(
        host_buffer.handle,
        device_allocation->handle,
        queue->handle,
        TransferRequest{.byte_size = host_buffer.byte_size, .debug_label = debug_label});
    if (!transfer.has_value()) {
        return std::unexpected(transfer.error());
    }

    auto wait_result = session_->Wait(transfer->completion, WaitRequest{});
    if (!wait_result.has_value()) {
        return std::unexpected(wait_result.error());
    }

    device_leases_.push_back(device_allocation->handle);
    queue_leases_.push_back(queue->handle);
    completion_leases_.push_back(transfer->completion);

    HostToDeviceOutput output;
    output.source_host_buffer = host_buffer;
    output.device_buffer = DeviceBufferToken{
        .handle = device_allocation->handle,
        .byte_size = device_allocation->byte_size,
    };
    output.queue = QueueToken{.handle = queue->handle};
    output.transfer_completion = TransferCompletionToken{
        .completion = transfer->completion,
        .byte_size = transfer->byte_size,
    };
    return output;
}

void HostToDeviceNode::Stop() {
    if (!session_) {
        graph::NamedInteriorNode<
            graph::TypeList<HostBufferToken>,
            graph::TypeList<HostToDeviceOutput>,
            HostToDeviceNode>::Stop();
        return;
    }

    for (const auto& lease : completion_leases_) {
        session_->Release(lease, ReleaseRequest{.allow_if_released = true});
    }
    for (const auto& lease : queue_leases_) {
        session_->Release(lease, ReleaseRequest{.allow_if_released = true});
    }
    for (const auto& lease : device_leases_) {
        session_->Release(lease, ReleaseRequest{.allow_if_released = true});
    }

    completion_leases_.clear();
    queue_leases_.clear();
    device_leases_.clear();
    graph::NamedInteriorNode<
        graph::TypeList<HostBufferToken>,
        graph::TypeList<HostToDeviceOutput>,
        HostToDeviceNode>::Stop();
}

bool DeviceToHostNode::Initialize(AcceleratorSessionRegistry& registry,
                                  const std::string& session_key) {
    return ResolveSession(registry, session_key, session_);
}

std::optional<DeviceToHostOutput> DeviceToHostNode::Transfer(
    const HostToDeviceOutput& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    auto transferred = ExecuteImpl(input, "d2h-transfer");
    if (!transferred.has_value()) {
        return std::nullopt;
    }

    return transferred.value();
}

std::expected<DeviceToHostOutput, AcceleratorError>
DeviceToHostNode::Execute(const HostToDeviceOutput& input, const std::string& debug_label) {
    return ExecuteImpl(input, debug_label);
}

std::expected<DeviceToHostOutput, AcceleratorError>
DeviceToHostNode::ExecuteImpl(const HostToDeviceOutput& input, const std::string& debug_label) {
    if (!session_) {
        AcceleratorError error;
        error.category = AcceleratorErrorCategory::InvalidState;
        error.operation = "DeviceToHostNode::Execute";
        error.diagnostic = "node was not initialized";
        return std::unexpected(error);
    }

    if (!input.device_buffer.handle.IsValid() ||
        !input.queue.handle.IsValid() ||
        input.device_buffer.byte_size == 0) {
        AcceleratorError error;
        error.category = AcceleratorErrorCategory::InvalidArgument;
        error.operation = "DeviceToHostNode::Execute";
        error.diagnostic = "device transfer input is invalid";
        return std::unexpected(error);
    }

    auto host_allocation = session_->AllocateHost(
        HostAllocationRequest{.byte_size = input.device_buffer.byte_size,
                              .debug_label = debug_label + ".host"});
    if (!host_allocation.has_value()) {
        return std::unexpected(host_allocation.error());
    }

    auto transfer = session_->EnqueueDeviceToHost(
        input.device_buffer.handle,
        host_allocation->handle,
        input.queue.handle,
        TransferRequest{.byte_size = input.device_buffer.byte_size, .debug_label = debug_label});
    if (!transfer.has_value()) {
        return std::unexpected(transfer.error());
    }

    auto wait_result = session_->Wait(transfer->completion, WaitRequest{});
    if (!wait_result.has_value()) {
        return std::unexpected(wait_result.error());
    }

    host_leases_.push_back(host_allocation->handle);
    completion_leases_.push_back(transfer->completion);

    DeviceToHostOutput output;
    output.source_host_buffer = input.source_host_buffer;
    output.source_device_buffer = input.device_buffer;
    output.output_host_buffer = HostBufferToken{
        .handle = host_allocation->handle,
        .byte_size = host_allocation->byte_size,
    };
    output.queue = input.queue;
    output.transfer_completion = TransferCompletionToken{
        .completion = transfer->completion,
        .byte_size = transfer->byte_size,
    };
    return output;
}

void DeviceToHostNode::Stop() {
    if (!session_) {
        graph::NamedInteriorNode<
            graph::TypeList<HostToDeviceOutput>,
            graph::TypeList<DeviceToHostOutput>,
            DeviceToHostNode>::Stop();
        return;
    }

    for (const auto& lease : completion_leases_) {
        session_->Release(lease, ReleaseRequest{.allow_if_released = true});
    }
    for (const auto& lease : host_leases_) {
        session_->Release(lease, ReleaseRequest{.allow_if_released = true});
    }

    completion_leases_.clear();
    host_leases_.clear();
    graph::NamedInteriorNode<
        graph::TypeList<HostToDeviceOutput>,
        graph::TypeList<DeviceToHostOutput>,
        DeviceToHostNode>::Stop();
}

bool HostEgressNode::Initialize(AcceleratorSessionRegistry& registry, const std::string& session_key) {
    return ResolveSession(registry, session_key, session_);
}

bool HostEgressNode::Consume(const DeviceToHostOutput& transfer_output,
                             std::integral_constant<std::size_t, 0>) {
    auto payload = Execute(transfer_output);
    if (!payload.has_value()) {
        return false;
    }

    last_payload_ = std::move(payload.value());
    return true;
}

std::expected<std::vector<std::byte>, AcceleratorError>
HostEgressNode::Execute(const DeviceToHostOutput& transfer_output) const {
    return Execute(transfer_output.output_host_buffer);
}

std::expected<std::vector<std::byte>, AcceleratorError>
HostEgressNode::Execute(const HostBufferToken& host_buffer) const {
    if (!session_) {
        AcceleratorError error;
        error.category = AcceleratorErrorCategory::InvalidState;
        error.operation = "HostEgressNode::Execute";
        error.diagnostic = "node was not initialized";
        return std::unexpected(error);
    }

    if (!host_buffer.handle.IsValid() || host_buffer.byte_size == 0) {
        AcceleratorError error;
        error.category = AcceleratorErrorCategory::InvalidArgument;
        error.operation = "HostEgressNode::Execute";
        error.diagnostic = "host buffer token is invalid";
        return std::unexpected(error);
    }

    auto read_result = session_->ReadHost(
        host_buffer.handle,
        HostReadRequest{.byte_size = host_buffer.byte_size, .source_offset = 0});
    if (!read_result.has_value()) {
        return std::unexpected(read_result.error());
    }

    return read_result->bytes;
}

const std::vector<std::byte>& HostEgressNode::LastPayload() const noexcept {
    return last_payload_;
}

void HostEgressNode::Stop() {
    graph::NamedSinkNode<HostEgressNode, DeviceToHostOutput>::Stop();
}

bool ReleaseLeaseNode::Initialize(AcceleratorSessionRegistry& registry, const std::string& session_key) {
    return ResolveSession(registry, session_key, session_);
}

bool ReleaseLeaseNode::Consume(const ReleaseLeaseInput& release,
                               std::integral_constant<std::size_t, 0>) {
    auto result = ExecuteTokenVariant(release.token, release.request);
    return result.has_value() && result->released;
}

std::expected<ReleaseResult, AcceleratorError>
ReleaseLeaseNode::Execute(const HostBufferToken& token, const ReleaseRequest& request) {
    return ExecuteTokenVariant(token, request);
}

std::expected<ReleaseResult, AcceleratorError>
ReleaseLeaseNode::ExecuteTokenVariant(const ReleaseLeaseTokenVariant& token,
                                      const ReleaseRequest& request) {
    if (!session_) {
        AcceleratorError error;
        error.category = AcceleratorErrorCategory::InvalidState;
        error.operation = "ReleaseLeaseNode::Execute";
        error.diagnostic = "node was not initialized";
        return std::unexpected(error);
    }

    return std::visit(
        [this, &request](const auto& typed_token) -> std::expected<ReleaseResult, AcceleratorError> {
            using TokenType = std::decay_t<decltype(typed_token)>;
            if constexpr (std::is_same_v<TokenType, HostBufferToken>) {
                return session_->Release(typed_token.handle, request);
            } else if constexpr (std::is_same_v<TokenType, DeviceBufferToken>) {
                return session_->Release(typed_token.handle, request);
            } else if constexpr (std::is_same_v<TokenType, QueueToken>) {
                return session_->Release(typed_token.handle, request);
            } else {
                return session_->Release(typed_token.completion, request);
            }
        },
        token);
}

std::expected<ReleaseResult, AcceleratorError>
ReleaseLeaseNode::Execute(const DeviceBufferToken& token, const ReleaseRequest& request) {
    return ExecuteTokenVariant(token, request);
}

std::expected<ReleaseResult, AcceleratorError>
ReleaseLeaseNode::Execute(const QueueToken& token, const ReleaseRequest& request) {
    return ExecuteTokenVariant(token, request);
}

std::expected<ReleaseResult, AcceleratorError>
ReleaseLeaseNode::Execute(const TransferCompletionToken& token, const ReleaseRequest& request) {
    return ExecuteTokenVariant(token, request);
}

void ReleaseLeaseNode::Stop() {
    graph::NamedSinkNode<ReleaseLeaseNode, ReleaseLeaseInput>::Stop();
}

}  // namespace accelgraph
