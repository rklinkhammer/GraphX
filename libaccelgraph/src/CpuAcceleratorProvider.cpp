// SPDX-License-Identifier: MIT

#include "accelgraph/CpuAcceleratorProvider.hpp"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace accelgraph {

namespace {

struct SharedHandleState {
    AcceleratorProviderId provider_id;
    AcceleratorSessionId session_id;
    AcceleratorDeviceId device_id;
    std::uint64_t internal_id{0};
    HandleDebugInfo debug;
    std::atomic<bool> released{false};
};

const AcceleratorProviderId kEmptyProviderId{};
const AcceleratorDeviceId kEmptyDeviceId{};
const HandleDebugInfo kEmptyDebugInfo{};

std::atomic<std::uint64_t> g_next_id{1};

std::uint64_t NextId() {
    return g_next_id.fetch_add(1, std::memory_order_relaxed);
}

AcceleratorError MakeError(const AcceleratorSessionInfo& info,
                           AcceleratorErrorCategory category,
                           std::string operation,
                           std::string diagnostic) {
    AcceleratorError error;
    error.category = category;
    error.backend = info.backend;
    error.execution_mode = info.execution_mode;
    error.provider_id = info.provider_id;
    error.session_id = info.session_id;
    error.device_id = info.device.id;
    error.operation = std::move(operation);
    error.diagnostic = std::move(diagnostic);
    return error;
}

}  // namespace

struct HostAllocationHandle::State : SharedHandleState {};
struct DeviceAllocationHandle::State : SharedHandleState {};
struct QueueHandle::State : SharedHandleState {};
struct EventHandle::State : SharedHandleState {};
struct TransferCompletion::State : SharedHandleState {
    std::uint64_t event_internal_id{0};
};

HostAllocationHandle::HostAllocationHandle(std::shared_ptr<State> state) : state_(std::move(state)) {}

bool HostAllocationHandle::IsValid() const noexcept {
    return static_cast<bool>(state_);
}

bool HostAllocationHandle::IsReleased() const noexcept {
    return state_ ? state_->released.load(std::memory_order_acquire) : false;
}

const AcceleratorProviderId& HostAllocationHandle::ProviderId() const noexcept {
    return state_ ? state_->provider_id : kEmptyProviderId;
}

AcceleratorSessionId HostAllocationHandle::SessionId() const noexcept {
    return state_ ? state_->session_id : AcceleratorSessionId{};
}

const AcceleratorDeviceId& HostAllocationHandle::DeviceId() const noexcept {
    return state_ ? state_->device_id : kEmptyDeviceId;
}

const HandleDebugInfo& HostAllocationHandle::DebugInfo() const noexcept {
    return state_ ? state_->debug : kEmptyDebugInfo;
}

DeviceAllocationHandle::DeviceAllocationHandle(std::shared_ptr<State> state)
    : state_(std::move(state)) {}

bool DeviceAllocationHandle::IsValid() const noexcept {
    return static_cast<bool>(state_);
}

bool DeviceAllocationHandle::IsReleased() const noexcept {
    return state_ ? state_->released.load(std::memory_order_acquire) : false;
}

const AcceleratorProviderId& DeviceAllocationHandle::ProviderId() const noexcept {
    return state_ ? state_->provider_id : kEmptyProviderId;
}

AcceleratorSessionId DeviceAllocationHandle::SessionId() const noexcept {
    return state_ ? state_->session_id : AcceleratorSessionId{};
}

const AcceleratorDeviceId& DeviceAllocationHandle::DeviceId() const noexcept {
    return state_ ? state_->device_id : kEmptyDeviceId;
}

const HandleDebugInfo& DeviceAllocationHandle::DebugInfo() const noexcept {
    return state_ ? state_->debug : kEmptyDebugInfo;
}

QueueHandle::QueueHandle(std::shared_ptr<State> state) : state_(std::move(state)) {}

bool QueueHandle::IsValid() const noexcept {
    return static_cast<bool>(state_);
}

bool QueueHandle::IsReleased() const noexcept {
    return state_ ? state_->released.load(std::memory_order_acquire) : false;
}

const AcceleratorProviderId& QueueHandle::ProviderId() const noexcept {
    return state_ ? state_->provider_id : kEmptyProviderId;
}

AcceleratorSessionId QueueHandle::SessionId() const noexcept {
    return state_ ? state_->session_id : AcceleratorSessionId{};
}

const AcceleratorDeviceId& QueueHandle::DeviceId() const noexcept {
    return state_ ? state_->device_id : kEmptyDeviceId;
}

const HandleDebugInfo& QueueHandle::DebugInfo() const noexcept {
    return state_ ? state_->debug : kEmptyDebugInfo;
}

EventHandle::EventHandle(std::shared_ptr<State> state) : state_(std::move(state)) {}

bool EventHandle::IsValid() const noexcept {
    return static_cast<bool>(state_);
}

bool EventHandle::IsReleased() const noexcept {
    return state_ ? state_->released.load(std::memory_order_acquire) : false;
}

const AcceleratorProviderId& EventHandle::ProviderId() const noexcept {
    return state_ ? state_->provider_id : kEmptyProviderId;
}

AcceleratorSessionId EventHandle::SessionId() const noexcept {
    return state_ ? state_->session_id : AcceleratorSessionId{};
}

const AcceleratorDeviceId& EventHandle::DeviceId() const noexcept {
    return state_ ? state_->device_id : kEmptyDeviceId;
}

const HandleDebugInfo& EventHandle::DebugInfo() const noexcept {
    return state_ ? state_->debug : kEmptyDebugInfo;
}

TransferCompletion::TransferCompletion(std::shared_ptr<State> state, EventHandle event)
    : state_(std::move(state)), event_(std::move(event)) {}

bool TransferCompletion::IsValid() const noexcept {
    return static_cast<bool>(state_);
}

bool TransferCompletion::IsReleased() const noexcept {
    return state_ ? state_->released.load(std::memory_order_acquire) : false;
}

const AcceleratorProviderId& TransferCompletion::ProviderId() const noexcept {
    return state_ ? state_->provider_id : kEmptyProviderId;
}

AcceleratorSessionId TransferCompletion::SessionId() const noexcept {
    return state_ ? state_->session_id : AcceleratorSessionId{};
}

const AcceleratorDeviceId& TransferCompletion::DeviceId() const noexcept {
    return state_ ? state_->device_id : kEmptyDeviceId;
}

const HandleDebugInfo& TransferCompletion::DebugInfo() const noexcept {
    return state_ ? state_->debug : kEmptyDebugInfo;
}

const EventHandle& TransferCompletion::Event() const noexcept {
    return event_;
}

class CpuAcceleratorSession final : public IAcceleratorSession {
public:
    explicit CpuAcceleratorSession(AcceleratorSessionInfo info) : info_(std::move(info)) {}

    ~CpuAcceleratorSession() override {
        std::scoped_lock<std::mutex> lock(mutex_);
        host_allocations_.clear();
        device_allocations_.clear();
        queues_.clear();
        events_.clear();
        completions_.clear();
    }

    [[nodiscard]] AcceleratorSessionInfo Info() const override {
        return info_;
    }

    std::expected<HostAllocationResult, AcceleratorError>
    AllocateHost(const HostAllocationRequest& request) override {
        if (request.byte_size == 0) {
            return std::unexpected(MakeError(info_,
                                             AcceleratorErrorCategory::InvalidArgument,
                                             "AllocateHost",
                                             "host allocation byte_size must be greater than zero"));
        }

        std::vector<std::byte> bytes;
        try {
            bytes.resize(request.byte_size);
        } catch (const std::bad_alloc&) {
            return std::unexpected(MakeError(info_,
                                             AcceleratorErrorCategory::AllocationFailed,
                                             "AllocateHost",
                                             "host allocation failed due to insufficient memory"));
        }

        auto state = std::make_shared<HostAllocationHandle::State>();
        state->provider_id = info_.provider_id;
        state->session_id = info_.session_id;
        state->device_id = info_.device.id;
        state->internal_id = NextId();
        state->debug.resource_type = "HostAllocation";
        state->debug.label = request.debug_label;
        state->debug.byte_size = request.byte_size;

        {
            std::scoped_lock<std::mutex> lock(mutex_);
            host_allocations_.emplace(state->internal_id, std::move(bytes));
        }

        HostAllocationResult result;
        result.handle = HostAllocationHandle{state};
        result.byte_size = request.byte_size;
        return result;
    }

    std::expected<HostWriteResult, AcceleratorError>
    WriteHost(const HostAllocationHandle& handle, const HostWriteRequest& request) override {
        auto valid = ValidateOwnedHandle(handle.state_, "WriteHost");
        if (!valid) {
            return std::unexpected(valid.error());
        }
        if (request.source.empty()) {
            return std::unexpected(MakeError(info_,
                                             AcceleratorErrorCategory::InvalidArgument,
                                             "WriteHost",
                                             "write source must not be empty"));
        }

        {
            std::scoped_lock<std::mutex> lock(mutex_);
            auto it = host_allocations_.find(handle.state_->internal_id);
            if (it == host_allocations_.end()) {
                return std::unexpected(MakeError(info_,
                                                 AcceleratorErrorCategory::TransferFailed,
                                                 "WriteHost",
                                                 "host allocation is not active"));
            }

            if (request.destination_offset > it->second.size()) {
                return std::unexpected(MakeError(info_,
                                                 AcceleratorErrorCategory::InvalidArgument,
                                                 "WriteHost",
                                                 "destination offset is out of range"));
            }

            if (request.source.size() > it->second.size() - request.destination_offset) {
                return std::unexpected(MakeError(info_,
                                                 AcceleratorErrorCategory::TransferFailed,
                                                 "WriteHost",
                                                 "write range exceeds allocation bounds"));
            }

            std::memcpy(it->second.data() + request.destination_offset,
                        request.source.data(),
                        request.source.size());
        }

        HostWriteResult result;
        result.bytes_written = request.source.size();
        return result;
    }

    std::expected<HostReadResult, AcceleratorError>
    ReadHost(const HostAllocationHandle& handle, const HostReadRequest& request) override {
        auto valid = ValidateOwnedHandle(handle.state_, "ReadHost");
        if (!valid) {
            return std::unexpected(valid.error());
        }
        if (request.byte_size == 0) {
            return std::unexpected(MakeError(info_,
                                             AcceleratorErrorCategory::InvalidArgument,
                                             "ReadHost",
                                             "read byte_size must be greater than zero"));
        }

        HostReadResult result;

        {
            std::scoped_lock<std::mutex> lock(mutex_);
            auto it = host_allocations_.find(handle.state_->internal_id);
            if (it == host_allocations_.end()) {
                return std::unexpected(MakeError(info_,
                                                 AcceleratorErrorCategory::TransferFailed,
                                                 "ReadHost",
                                                 "host allocation is not active"));
            }

            if (request.source_offset > it->second.size()) {
                return std::unexpected(MakeError(info_,
                                                 AcceleratorErrorCategory::InvalidArgument,
                                                 "ReadHost",
                                                 "source offset is out of range"));
            }

            if (request.byte_size > it->second.size() - request.source_offset) {
                return std::unexpected(MakeError(info_,
                                                 AcceleratorErrorCategory::TransferFailed,
                                                 "ReadHost",
                                                 "read range exceeds allocation bounds"));
            }

            result.bytes.resize(request.byte_size);
            std::memcpy(result.bytes.data(),
                        it->second.data() + request.source_offset,
                        request.byte_size);
        }

        return result;
    }

    std::expected<DeviceAllocationResult, AcceleratorError>
    AllocateDevice(const DeviceAllocationRequest& request) override {
        if (request.byte_size == 0) {
            return std::unexpected(MakeError(info_,
                                             AcceleratorErrorCategory::InvalidArgument,
                                             "AllocateDevice",
                                             "device allocation byte_size must be greater than zero"));
        }

        std::vector<std::byte> bytes;
        try {
            bytes.resize(request.byte_size);
        } catch (const std::bad_alloc&) {
            return std::unexpected(MakeError(info_,
                                             AcceleratorErrorCategory::AllocationFailed,
                                             "AllocateDevice",
                                             "device allocation failed due to insufficient memory"));
        }

        auto state = std::make_shared<DeviceAllocationHandle::State>();
        state->provider_id = info_.provider_id;
        state->session_id = info_.session_id;
        state->device_id = info_.device.id;
        state->internal_id = NextId();
        state->debug.resource_type = "DeviceAllocation";
        state->debug.label = request.debug_label;
        state->debug.byte_size = request.byte_size;

        {
            std::scoped_lock<std::mutex> lock(mutex_);
            device_allocations_.emplace(state->internal_id, std::move(bytes));
        }

        DeviceAllocationResult result;
        result.handle = DeviceAllocationHandle{state};
        result.byte_size = request.byte_size;
        return result;
    }

    std::expected<ReleaseResult, AcceleratorError>
    Release(const HostAllocationHandle& handle, const ReleaseRequest& request) override {
        return ReleaseSetCommon(handle.state_, request, "ReleaseHost", host_allocations_);
    }

    std::expected<ReleaseResult, AcceleratorError>
    Release(const DeviceAllocationHandle& handle, const ReleaseRequest& request) override {
        return ReleaseSetCommon(handle.state_, request, "ReleaseDevice", device_allocations_);
    }

    std::expected<QueueAcquisitionResult, AcceleratorError>
    AcquireQueue(const QueueRequest& request) override {
        auto state = std::make_shared<QueueHandle::State>();
        state->provider_id = info_.provider_id;
        state->session_id = info_.session_id;
        state->device_id = info_.device.id;
        state->internal_id = NextId();
        state->debug.resource_type = "Queue";
        state->debug.label = request.debug_label;
        state->debug.byte_size = 0;

        {
            std::scoped_lock<std::mutex> lock(mutex_);
            queues_.emplace(state->internal_id);
        }

        QueueAcquisitionResult result;
        result.handle = QueueHandle{state};
        return result;
    }

    std::expected<ReleaseResult, AcceleratorError>
    Release(const QueueHandle& handle, const ReleaseRequest& request) override {
        return ReleaseSetCommon(handle.state_, request, "ReleaseQueue", queues_,
                                AcceleratorErrorCategory::QueueFailed);
    }

    std::expected<TransferResult, AcceleratorError>
    EnqueueHostToDevice(const HostAllocationHandle& source,
                        const DeviceAllocationHandle& destination,
                        const QueueHandle& queue,
                        const TransferRequest& request) override {
        return EnqueueTransfer(source.state_, destination.state_, queue.state_, request,
                               "EnqueueHostToDevice");
    }

    std::expected<TransferResult, AcceleratorError>
    EnqueueDeviceToHost(const DeviceAllocationHandle& source,
                        const HostAllocationHandle& destination,
                        const QueueHandle& queue,
                        const TransferRequest& request) override {
        return EnqueueTransfer(source.state_, destination.state_, queue.state_, request,
                               "EnqueueDeviceToHost");
    }

    std::expected<WaitResult, AcceleratorError>
    Wait(const TransferCompletion& completion, const WaitRequest& request) override {
        auto valid = ValidateOwnedHandle(completion.state_, "WaitCompletion");
        if (!valid) {
            return std::unexpected(valid.error());
        }
        if (request.timeout < std::chrono::milliseconds{0}) {
            return std::unexpected(MakeError(info_,
                                             AcceleratorErrorCategory::InvalidArgument,
                                             "WaitCompletion",
                                             "timeout must be zero or positive"));
        }

        {
            std::scoped_lock<std::mutex> lock(mutex_);
            if (!completions_.contains(completion.state_->internal_id)) {
                return std::unexpected(MakeError(info_,
                                                 AcceleratorErrorCategory::EventFailed,
                                                 "WaitCompletion",
                                                 "transfer completion is not active"));
            }
        }

        WaitResult result;
        result.completed = true;
        return result;
    }

    std::expected<ReleaseResult, AcceleratorError>
    Release(const TransferCompletion& completion, const ReleaseRequest& request) override {
        auto valid = ValidateOwnedHandle(completion.state_, "ReleaseCompletion", request.allow_if_released);
        if (!valid) {
            return std::unexpected(valid.error());
        }

        const std::uint64_t completion_id = completion.state_->internal_id;
        const std::uint64_t event_id = completion.state_->event_internal_id;

        {
            std::scoped_lock<std::mutex> lock(mutex_);
            completions_.erase(completion_id);
            events_.erase(event_id);
        }

        completion.event_.state_->released.store(true, std::memory_order_release);
        completion.state_->released.store(true, std::memory_order_release);

        ReleaseResult result;
        result.released = true;
        return result;
    }

private:
    template <typename StateT>
    std::expected<void, AcceleratorError>
    ValidateOwnedHandle(const std::shared_ptr<StateT>& state,
                        const std::string& operation,
                        bool allow_if_released = false) const {
        if (!state) {
            return std::unexpected(MakeError(info_,
                                             AcceleratorErrorCategory::InvalidArgument,
                                             operation,
                                             "handle is not valid"));
        }
        if (state->provider_id != info_.provider_id || state->session_id != info_.session_id) {
            return std::unexpected(MakeError(info_,
                                             AcceleratorErrorCategory::CrossSessionResource,
                                             operation,
                                             "handle belongs to a different provider/session"));
        }
        if (!allow_if_released && state->released.load(std::memory_order_acquire)) {
            return std::unexpected(MakeError(info_,
                                             AcceleratorErrorCategory::InvalidState,
                                             operation,
                                             "handle was already released"));
        }
        return {};
    }

    template <typename StateT>
    std::expected<ReleaseResult, AcceleratorError>
    ReleaseSetCommon(const std::shared_ptr<StateT>& state,
                     const ReleaseRequest& request,
                     const std::string& operation,
                     std::unordered_map<std::uint64_t, std::vector<std::byte>>& storage) {
        auto valid = ValidateOwnedHandle(state, operation, request.allow_if_released);
        if (!valid) {
            return std::unexpected(valid.error());
        }

        bool removed = false;
        {
            std::scoped_lock<std::mutex> lock(mutex_);
            removed = storage.erase(state->internal_id) > 0;
        }

        if (!removed && !request.allow_if_released) {
            return std::unexpected(MakeError(info_,
                                             AcceleratorErrorCategory::InvalidState,
                                             operation,
                                             "resource was not active"));
        }

        state->released.store(true, std::memory_order_release);

        ReleaseResult result;
        result.released = removed;
        return result;
    }

    template <typename StateT>
    std::expected<ReleaseResult, AcceleratorError>
    ReleaseSetCommon(const std::shared_ptr<StateT>& state,
                     const ReleaseRequest& request,
                     const std::string& operation,
                     std::unordered_set<std::uint64_t>& storage,
                     AcceleratorErrorCategory missing_category) {
        auto valid = ValidateOwnedHandle(state, operation, request.allow_if_released);
        if (!valid) {
            return std::unexpected(valid.error());
        }

        bool removed = false;
        {
            std::scoped_lock<std::mutex> lock(mutex_);
            removed = storage.erase(state->internal_id) > 0;
        }

        if (!removed && !request.allow_if_released) {
            return std::unexpected(MakeError(info_,
                                             missing_category,
                                             operation,
                                             "resource was not active"));
        }

        state->released.store(true, std::memory_order_release);

        ReleaseResult result;
        result.released = removed;
        return result;
    }

    std::expected<TransferResult, AcceleratorError>
    EnqueueTransfer(const std::shared_ptr<SharedHandleState>& source,
                    const std::shared_ptr<SharedHandleState>& destination,
                    const std::shared_ptr<QueueHandle::State>& queue,
                    const TransferRequest& request,
                    const std::string& operation) {
        auto source_valid = ValidateOwnedHandle(source, operation);
        if (!source_valid) {
            return std::unexpected(source_valid.error());
        }
        auto destination_valid = ValidateOwnedHandle(destination, operation);
        if (!destination_valid) {
            return std::unexpected(destination_valid.error());
        }
        auto queue_valid = ValidateOwnedHandle(queue, operation);
        if (!queue_valid) {
            return std::unexpected(queue_valid.error());
        }
        if (request.byte_size == 0) {
            return std::unexpected(MakeError(info_,
                                             AcceleratorErrorCategory::InvalidArgument,
                                             operation,
                                             "transfer byte_size must be greater than zero"));
        }

        {
            std::scoped_lock<std::mutex> lock(mutex_);
            if (!queues_.contains(queue->internal_id)) {
                return std::unexpected(MakeError(info_,
                                                 AcceleratorErrorCategory::QueueFailed,
                                                 operation,
                                                 "queue is not active"));
            }

            auto find_buffer = [this](std::uint64_t id) -> std::vector<std::byte>* {
                if (auto host_it = host_allocations_.find(id); host_it != host_allocations_.end()) {
                    return &host_it->second;
                }
                if (auto device_it = device_allocations_.find(id);
                    device_it != device_allocations_.end()) {
                    return &device_it->second;
                }
                return nullptr;
            };

            std::vector<std::byte>* source_buffer = find_buffer(source->internal_id);
            if (!source_buffer) {
                return std::unexpected(MakeError(info_,
                                                 AcceleratorErrorCategory::TransferFailed,
                                                 operation,
                                                 "source allocation is not active"));
            }

            std::vector<std::byte>* destination_buffer = find_buffer(destination->internal_id);
            if (!destination_buffer) {
                return std::unexpected(MakeError(info_,
                                                 AcceleratorErrorCategory::TransferFailed,
                                                 operation,
                                                 "destination allocation is not active"));
            }

            if (request.source_offset > source_buffer->size() ||
                request.destination_offset > destination_buffer->size()) {
                return std::unexpected(MakeError(info_,
                                                 AcceleratorErrorCategory::InvalidArgument,
                                                 operation,
                                                 "source or destination offset is out of range"));
            }

            if (request.byte_size > source_buffer->size() - request.source_offset ||
                request.byte_size > destination_buffer->size() - request.destination_offset) {
                return std::unexpected(MakeError(info_,
                                                 AcceleratorErrorCategory::TransferFailed,
                                                 operation,
                                                 "transfer range exceeds allocation bounds"));
            }

            std::memcpy(destination_buffer->data() + request.destination_offset,
                        source_buffer->data() + request.source_offset,
                        request.byte_size);
        }

        auto event_state = std::make_shared<EventHandle::State>();
        event_state->provider_id = info_.provider_id;
        event_state->session_id = info_.session_id;
        event_state->device_id = info_.device.id;
        event_state->internal_id = NextId();
        event_state->debug.resource_type = "Event";
        event_state->debug.label = request.debug_label;
        event_state->debug.byte_size = 0;

        auto completion_state = std::make_shared<TransferCompletion::State>();
        completion_state->provider_id = info_.provider_id;
        completion_state->session_id = info_.session_id;
        completion_state->device_id = info_.device.id;
        completion_state->internal_id = NextId();
        completion_state->event_internal_id = event_state->internal_id;
        completion_state->debug.resource_type = "TransferCompletion";
        completion_state->debug.label = request.debug_label;
        completion_state->debug.byte_size = request.byte_size;

        {
            std::scoped_lock<std::mutex> lock(mutex_);
            events_.emplace(event_state->internal_id);
            completions_.emplace(completion_state->internal_id);
        }

        TransferResult result;
        result.completion = TransferCompletion{completion_state, EventHandle{event_state}};
        result.byte_size = request.byte_size;
        return result;
    }

    AcceleratorSessionInfo info_;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::vector<std::byte>> host_allocations_;
    std::unordered_map<std::uint64_t, std::vector<std::byte>> device_allocations_;
    std::unordered_set<std::uint64_t> queues_;
    std::unordered_set<std::uint64_t> events_;
    std::unordered_set<std::uint64_t> completions_;
};

CpuAcceleratorProvider::CpuAcceleratorProvider() {
    info_.provider_id = AcceleratorProviderId{"cpu.default"};
    info_.backend = AcceleratorBackend::Cpu;
    info_.execution_mode = AcceleratorExecutionMode::HostSynchronous;
    info_.devices.push_back(AcceleratorDeviceInfo{
        .id = AcceleratorDeviceId{"cpu:0"},
        .name = "Host CPU",
        .backend = AcceleratorBackend::Cpu,
    });
}

AcceleratorProviderInfo CpuAcceleratorProvider::Info() const {
    return info_;
}

std::expected<std::shared_ptr<IAcceleratorSession>, AcceleratorError>
CpuAcceleratorProvider::CreateSession(const AcceleratorSessionCreateRequest& request) {
    if (request.requested_device && request.requested_device->value != info_.devices.front().id.value) {
        AcceleratorError error;
        error.category = AcceleratorErrorCategory::Unavailable;
        error.backend = info_.backend;
        error.execution_mode = info_.execution_mode;
        error.provider_id = info_.provider_id;
        error.device_id = request.requested_device;
        error.operation = "CreateSession";
        error.diagnostic = "requested CPU device is unavailable";
        return std::unexpected(error);
    }

    AcceleratorSessionInfo session_info;
    session_info.provider_id = info_.provider_id;
    session_info.session_id = AcceleratorSessionId{NextId()};
    session_info.backend = info_.backend;
    session_info.execution_mode = info_.execution_mode;
    session_info.device = info_.devices.front();

    return std::make_shared<CpuAcceleratorSession>(std::move(session_info));
}

}  // namespace accelgraph