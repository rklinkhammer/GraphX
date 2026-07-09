// SPDX-License-Identifier: MIT

#include "accelgraph/TransferGraphNodes.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

#include "accelgraph/CpuAcceleratorProvider.hpp"
#include "accelgraph/CudaAcceleratorProvider.hpp"
#include "accelgraph/MetalAcceleratorProvider.hpp"
#include "config/ConfigError.hpp"

namespace accelgraph {

namespace {

struct SessionSelection {
    std::string session_key;
    AcceleratorProviderId provider_id;
    AcceleratorBackend backend{AcceleratorBackend::Cpu};
};

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::expected<SessionSelection, AcceleratorError>
ParseSessionSelection(const graph::JsonView& cfg, const char* operation) {
    const auto& json = cfg.Raw();
    if (!json.is_object()) {
        AcceleratorError error;
        error.category = AcceleratorErrorCategory::InvalidArgument;
        error.operation = operation;
        error.diagnostic = "configuration must be a JSON object";
        return std::unexpected(error);
    }

    if (!json.contains("session_key") || !json["session_key"].is_string()) {
        AcceleratorError error;
        error.category = AcceleratorErrorCategory::InvalidArgument;
        error.operation = operation;
        error.diagnostic = "session_key must be a string";
        return std::unexpected(error);
    }

    if (!json.contains("provider_id") || !json["provider_id"].is_string()) {
        AcceleratorError error;
        error.category = AcceleratorErrorCategory::InvalidArgument;
        error.operation = operation;
        error.diagnostic = "provider_id must be a string";
        return std::unexpected(error);
    }

    if (!json.contains("backend") || !json["backend"].is_string()) {
        AcceleratorError error;
        error.category = AcceleratorErrorCategory::InvalidArgument;
        error.operation = operation;
        error.diagnostic = "backend must be a string";
        return std::unexpected(error);
    }

    const std::string backend = ToLower(json["backend"].get<std::string>());
    AcceleratorBackend parsed_backend = AcceleratorBackend::Cpu;
    if (backend == "cpu") {
        parsed_backend = AcceleratorBackend::Cpu;
    } else if (backend == "metal") {
        parsed_backend = AcceleratorBackend::Metal;
    } else if (backend == "cuda") {
        parsed_backend = AcceleratorBackend::Cuda;
    } else {
        AcceleratorError error;
        error.category = AcceleratorErrorCategory::InvalidArgument;
        error.operation = operation;
        error.diagnostic = "backend must be one of: cpu, metal, cuda";
        return std::unexpected(error);
    }

    SessionSelection selection;
    selection.session_key = json["session_key"].get<std::string>();
    selection.provider_id = AcceleratorProviderId{json["provider_id"].get<std::string>()};
    selection.backend = parsed_backend;
    return selection;
}

std::expected<std::shared_ptr<IAcceleratorProvider>, AcceleratorError>
CreateProvider(const SessionSelection& selection, const char* operation) {
    const auto provider_id = ToLower(selection.provider_id.value);
    if (selection.backend == AcceleratorBackend::Cpu || provider_id == "cpu.default") {
        return std::static_pointer_cast<IAcceleratorProvider>(std::make_shared<CpuAcceleratorProvider>());
    }

    if (selection.backend == AcceleratorBackend::Metal || provider_id == "metal.default") {
        return std::static_pointer_cast<IAcceleratorProvider>(std::make_shared<MetalAcceleratorProvider>());
    }

    if (selection.backend == AcceleratorBackend::Cuda || provider_id == "cuda.default") {
        return std::static_pointer_cast<IAcceleratorProvider>(std::make_shared<CudaAcceleratorProvider>());
    }

    AcceleratorError error;
    error.category = AcceleratorErrorCategory::InvalidArgument;
    error.operation = operation;
    error.diagnostic = "unsupported provider_id/backend combination";
    return std::unexpected(error);
}

std::expected<std::shared_ptr<IAcceleratorSession>, AcceleratorError>
AcquireOrCreateSession(const SessionSelection& selection, const char* operation) {
    static std::mutex cache_mutex;
    static std::unordered_map<std::string, std::weak_ptr<IAcceleratorSession>> cache;

    const std::string cache_key = selection.session_key + "|" +
                                  selection.provider_id.value + "|" +
                                (selection.backend == AcceleratorBackend::Metal
                                    ? "metal"
                                    : (selection.backend == AcceleratorBackend::Cuda ? "cuda" : "cpu"));

    {
        std::scoped_lock<std::mutex> lock(cache_mutex);
        const auto it = cache.find(cache_key);
        if (it != cache.end()) {
            if (auto existing = it->second.lock()) {
                return existing;
            }
        }
    }

    auto provider_result = CreateProvider(selection, operation);
    if (!provider_result.has_value()) {
        return std::unexpected(provider_result.error());
    }

    auto session_result = provider_result.value()->CreateSession(AcceleratorSessionCreateRequest{});
    if (!session_result.has_value()) {
        return std::unexpected(session_result.error());
    }

    {
        std::scoped_lock<std::mutex> lock(cache_mutex);
        cache[cache_key] = session_result.value();
    }

    return session_result.value();
}

}  // namespace

void HostIngressNode::Configure(const graph::JsonView& cfg) {
    auto session_selection = ParseSessionSelection(cfg, "HostIngressNode::Configure");
    if (!session_selection.has_value()) {
        throw graph::ConfigError(session_selection.error().diagnostic);
    }
    auto session_result = AcquireOrCreateSession(session_selection.value(), "HostIngressNode::Configure");
    if (!session_result.has_value()) {
        throw graph::ConfigError(session_result.error().diagnostic);
    }
    session_ = session_result.value();

    const auto& json = cfg.Raw();

    int payload_size = 0;
    int payload_multiplier = 13;
    int payload_offset = 5;

    if (json.contains("payload_size")) {
        if (!json["payload_size"].is_number_integer() || json["payload_size"].get<int>() < 0) {
            throw graph::ConfigError("HostIngressNode payload_size must be a non-negative integer");
        }
        payload_size = json["payload_size"].get<int>();
    }

    if (json.contains("payload_multiplier")) {
        if (!json["payload_multiplier"].is_number_integer()) {
            throw graph::ConfigError("HostIngressNode payload_multiplier must be an integer");
        }
        payload_multiplier = json["payload_multiplier"].get<int>();
    }

    if (json.contains("payload_offset")) {
        if (!json["payload_offset"].is_number_integer()) {
            throw graph::ConfigError("HostIngressNode payload_offset must be an integer");
        }
        payload_offset = json["payload_offset"].get<int>();
    }

    if (json.contains("debug_label")) {
        if (!json["debug_label"].is_string()) {
            throw graph::ConfigError("HostIngressNode debug_label must be a string");
        }
        staged_debug_label_ = json["debug_label"].get<std::string>();
    }

    if (payload_size > 0) {
        staged_input_.resize(static_cast<std::size_t>(payload_size));
        for (int i = 0; i < payload_size; ++i) {
            const auto value = static_cast<std::uint8_t>((i * payload_multiplier + payload_offset) & 0xFF);
            staged_input_[static_cast<std::size_t>(i)] = static_cast<std::byte>(value);
        }
    } else {
        staged_input_.clear();
    }
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

void HostToDeviceNode::Configure(const graph::JsonView& cfg) {
    auto session_selection = ParseSessionSelection(cfg, "HostToDeviceNode::Configure");
    if (!session_selection.has_value()) {
        throw graph::ConfigError(session_selection.error().diagnostic);
    }
    auto session_result = AcquireOrCreateSession(session_selection.value(), "HostToDeviceNode::Configure");
    if (!session_result.has_value()) {
        throw graph::ConfigError(session_result.error().diagnostic);
    }
    session_ = session_result.value();

    const auto& json = cfg.Raw();
    if (json.contains("debug_label")) {
        if (!json["debug_label"].is_string()) {
            throw graph::ConfigError("HostToDeviceNode debug_label must be a string");
        }
        default_debug_label_ = json["debug_label"].get<std::string>();
    }
}

std::optional<HostToDeviceOutput> HostToDeviceNode::Transfer(
    const HostBufferToken& host_buffer,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    auto transferred = ExecuteImpl(host_buffer, default_debug_label_);
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

void DeviceToHostNode::Configure(const graph::JsonView& cfg) {
    auto session_selection = ParseSessionSelection(cfg, "DeviceToHostNode::Configure");
    if (!session_selection.has_value()) {
        throw graph::ConfigError(session_selection.error().diagnostic);
    }
    auto session_result = AcquireOrCreateSession(session_selection.value(), "DeviceToHostNode::Configure");
    if (!session_result.has_value()) {
        throw graph::ConfigError(session_result.error().diagnostic);
    }
    session_ = session_result.value();

    const auto& json = cfg.Raw();
    if (json.contains("debug_label")) {
        if (!json["debug_label"].is_string()) {
            throw graph::ConfigError("DeviceToHostNode debug_label must be a string");
        }
        default_debug_label_ = json["debug_label"].get<std::string>();
    }
}

std::optional<DeviceToHostOutput> DeviceToHostNode::Transfer(
    const HostToDeviceOutput& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    auto transferred = ExecuteImpl(input, default_debug_label_);
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

void HostEgressNode::Configure(const graph::JsonView& cfg) {
    auto session_selection = ParseSessionSelection(cfg, "HostEgressNode::Configure");
    if (!session_selection.has_value()) {
        throw graph::ConfigError(session_selection.error().diagnostic);
    }
    auto session_result = AcquireOrCreateSession(session_selection.value(), "HostEgressNode::Configure");
    if (!session_result.has_value()) {
        throw graph::ConfigError(session_result.error().diagnostic);
    }
    session_ = session_result.value();
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

void ReleaseLeaseNode::Configure(const graph::JsonView& cfg) {
    auto session_selection = ParseSessionSelection(cfg, "ReleaseLeaseNode::Configure");
    if (!session_selection.has_value()) {
        throw graph::ConfigError(session_selection.error().diagnostic);
    }
    auto session_result = AcquireOrCreateSession(session_selection.value(), "ReleaseLeaseNode::Configure");
    if (!session_result.has_value()) {
        throw graph::ConfigError(session_result.error().diagnostic);
    }
    session_ = session_result.value();
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
