// SPDX-License-Identifier: MIT

#include "accelgraph/CudaAcceleratorProvider.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <new>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#if ACCELGRAPH_ENABLE_CUDA && ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE && __has_include(<cuda_runtime_api.h>)
#define ACCELGRAPH_CUDA_RUNTIME_HEADER_AVAILABLE 1
#include <cuda_runtime_api.h>
#else
#define ACCELGRAPH_CUDA_RUNTIME_HEADER_AVAILABLE 0
#endif

#include <optional>

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

#if ACCELGRAPH_ENABLE_CUDA && ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE && ACCELGRAPH_CUDA_RUNTIME_HEADER_AVAILABLE
std::atomic<std::uint64_t> g_next_cuda_id{1};

std::uint64_t NextCudaId() {
    return g_next_cuda_id.fetch_add(1, std::memory_order_relaxed);
}

constexpr const char* kCudaDriverUnavailableDiagnostic =
    "CUDA driver unavailable on this host.";
constexpr const char* kCudaDeviceUnavailableDiagnostic =
    "No compatible CUDA device detected.";
constexpr const char* kCudaSessionCreationFailureDiagnostic =
    "CUDA session creation failure.";
constexpr const char* kCudaAllocationFailureDiagnostic =
    "CUDA allocation failure.";
constexpr const char* kCudaTransferFailureDiagnostic =
    "CUDA transfer failure.";
constexpr const char* kCudaSynchronizationFailureDiagnostic =
    "CUDA synchronization failure.";

std::string BuildCudaDiagnostic(const char* prefix, cudaError_t error) {
    std::ostringstream out;
    out << prefix << " " << cudaGetErrorName(error) << ": " << cudaGetErrorString(error);
    return out.str();
}

AcceleratorError MakeSessionError(const AcceleratorSessionInfo& info,
                                  AcceleratorErrorCategory category,
                                  const char* operation,
                                  std::string diagnostic,
                                  std::optional<int> native_error_code = std::nullopt) {
    AcceleratorError error;
    error.category = category;
    error.backend = info.backend;
    error.execution_mode = info.execution_mode;
    error.provider_id = info.provider_id;
    error.session_id = info.session_id;
    error.device_id = info.device.id;
    error.operation = operation;
    error.native_error_code = native_error_code;
    error.diagnostic = std::move(diagnostic);
    return error;
}
#endif

AcceleratorError MakeProviderError(const AcceleratorProviderInfo& info,
                                   AcceleratorErrorCategory category,
                                   const char* operation,
                                   const char* diagnostic,
                                   std::optional<AcceleratorDeviceId> device_id = std::nullopt) {
    AcceleratorError error;
    error.category = category;
    error.backend = info.backend;
    error.execution_mode = info.execution_mode;
    error.provider_id = info.provider_id;
    error.device_id = std::move(device_id);
    error.operation = operation;
    error.diagnostic = diagnostic;
    return error;
}

#if ACCELGRAPH_ENABLE_CUDA && ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE && ACCELGRAPH_CUDA_RUNTIME_HEADER_AVAILABLE
struct HostAllocationHandle::State : SharedHandleState {};
struct DeviceAllocationHandle::State : SharedHandleState {};
struct QueueHandle::State : SharedHandleState {};
struct EventHandle::State : SharedHandleState {};
struct TransferCompletion::State : SharedHandleState {
    std::uint64_t event_internal_id{0};
};

class CudaAcceleratorSession final : public IAcceleratorSession {
public:
    CudaAcceleratorSession(AcceleratorSessionInfo info, int device_ordinal)
        : info_(std::move(info)), device_ordinal_(device_ordinal) {}

    ~CudaAcceleratorSession() override {
        SetDevice();
        std::scoped_lock<std::mutex> lock(mutex_);
        for (auto& [_, ptr] : device_allocations_) {
            if (ptr != nullptr) {
                cudaFree(ptr);
            }
        }
        for (auto& [_, stream] : queues_) {
            if (stream != nullptr) {
                cudaStreamDestroy(stream);
            }
        }
        for (auto& [_, event] : events_) {
            if (event != nullptr) {
                cudaEventDestroy(event);
            }
        }
    }

    [[nodiscard]] AcceleratorSessionInfo Info() const override {
        return info_;
    }

    std::expected<HostAllocationResult, AcceleratorError>
    AllocateHost(const HostAllocationRequest& request) override {
        if (request.byte_size == 0) {
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::InvalidArgument,
                                                    "AllocateHost",
                                                    "host allocation byte_size must be greater than zero"));
        }

        std::vector<std::byte> bytes;
        try {
            bytes.resize(request.byte_size);
        } catch (const std::bad_alloc&) {
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::AllocationFailed,
                                                    "AllocateHost",
                                                    "host allocation failed due to insufficient memory"));
        }

        auto state = std::make_shared<HostAllocationHandle::State>();
        state->provider_id = info_.provider_id;
        state->session_id = info_.session_id;
        state->device_id = info_.device.id;
        state->internal_id = NextCudaId();
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
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::InvalidArgument,
                                                    "WriteHost",
                                                    "write source must not be empty"));
        }

        {
            std::scoped_lock<std::mutex> lock(mutex_);
            auto it = host_allocations_.find(handle.state_->internal_id);
            if (it == host_allocations_.end()) {
                return std::unexpected(MakeSessionError(info_,
                                                        AcceleratorErrorCategory::TransferFailed,
                                                        "WriteHost",
                                                        "host allocation is not active"));
            }
            if (request.destination_offset > it->second.size()) {
                return std::unexpected(MakeSessionError(info_,
                                                        AcceleratorErrorCategory::InvalidArgument,
                                                        "WriteHost",
                                                        "destination offset is out of range"));
            }
            if (request.source.size() > it->second.size() - request.destination_offset) {
                return std::unexpected(MakeSessionError(info_,
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
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::InvalidArgument,
                                                    "ReadHost",
                                                    "read byte_size must be greater than zero"));
        }

        HostReadResult result;
        {
            std::scoped_lock<std::mutex> lock(mutex_);
            auto it = host_allocations_.find(handle.state_->internal_id);
            if (it == host_allocations_.end()) {
                return std::unexpected(MakeSessionError(info_,
                                                        AcceleratorErrorCategory::TransferFailed,
                                                        "ReadHost",
                                                        "host allocation is not active"));
            }
            if (request.source_offset > it->second.size()) {
                return std::unexpected(MakeSessionError(info_,
                                                        AcceleratorErrorCategory::InvalidArgument,
                                                        "ReadHost",
                                                        "source offset is out of range"));
            }
            if (request.byte_size > it->second.size() - request.source_offset) {
                return std::unexpected(MakeSessionError(info_,
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
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::InvalidArgument,
                                                    "AllocateDevice",
                                                    "device allocation byte_size must be greater than zero"));
        }
        auto set_device = SetDevice();
        if (!set_device) {
            return std::unexpected(set_device.error());
        }

        void* ptr = nullptr;
        const auto alloc_error = cudaMalloc(&ptr, request.byte_size);
        if (alloc_error != cudaSuccess) {
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::AllocationFailed,
                                                    "AllocateDevice",
                                                    BuildCudaDiagnostic(kCudaAllocationFailureDiagnostic,
                                                                        alloc_error),
                                                    static_cast<int>(alloc_error)));
        }

        auto state = std::make_shared<DeviceAllocationHandle::State>();
        state->provider_id = info_.provider_id;
        state->session_id = info_.session_id;
        state->device_id = info_.device.id;
        state->internal_id = NextCudaId();
        state->debug.resource_type = "DeviceAllocation";
        state->debug.label = request.debug_label;
        state->debug.byte_size = request.byte_size;

        {
            std::scoped_lock<std::mutex> lock(mutex_);
            device_allocations_.emplace(state->internal_id, ptr);
        }

        DeviceAllocationResult result;
        result.handle = DeviceAllocationHandle{state};
        result.byte_size = request.byte_size;
        return result;
    }

    std::expected<ReleaseResult, AcceleratorError>
    Release(const HostAllocationHandle& handle, const ReleaseRequest& request = {}) override {
        return ReleaseSetCommon(handle.state_, request, "ReleaseHost", host_allocations_);
    }

    std::expected<ReleaseResult, AcceleratorError>
    Release(const DeviceAllocationHandle& handle, const ReleaseRequest& request = {}) override {
        auto valid = ValidateOwnedHandle(handle.state_, "ReleaseDevice", request.allow_if_released);
        if (!valid) {
            return std::unexpected(valid.error());
        }

        auto set_device = SetDevice();
        if (!set_device) {
            return std::unexpected(set_device.error());
        }

        void* ptr = nullptr;
        bool removed = false;
        {
            std::scoped_lock<std::mutex> lock(mutex_);
            auto it = device_allocations_.find(handle.state_->internal_id);
            if (it != device_allocations_.end()) {
                ptr = it->second;
                device_allocations_.erase(it);
                removed = true;
            }
        }

        if (!removed && !request.allow_if_released) {
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::InvalidState,
                                                    "ReleaseDevice",
                                                    "device allocation was not active"));
        }

        if (ptr != nullptr) {
            const auto free_error = cudaFree(ptr);
            if (free_error != cudaSuccess) {
                return std::unexpected(MakeSessionError(info_,
                                                        AcceleratorErrorCategory::AllocationFailed,
                                                        "ReleaseDevice",
                                                        BuildCudaDiagnostic(kCudaAllocationFailureDiagnostic,
                                                                            free_error),
                                                        static_cast<int>(free_error)));
            }
        }

        handle.state_->released.store(true, std::memory_order_release);

        ReleaseResult result;
        result.released = removed;
        return result;
    }

    std::expected<QueueAcquisitionResult, AcceleratorError>
    AcquireQueue(const QueueRequest& request) override {
        auto set_device = SetDevice();
        if (!set_device) {
            return std::unexpected(set_device.error());
        }

        cudaStream_t stream = nullptr;
        const auto stream_error = cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking);
        if (stream_error != cudaSuccess) {
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::QueueFailed,
                                                    "AcquireQueue",
                                                    BuildCudaDiagnostic(kCudaSessionCreationFailureDiagnostic,
                                                                        stream_error),
                                                    static_cast<int>(stream_error)));
        }

        auto state = std::make_shared<QueueHandle::State>();
        state->provider_id = info_.provider_id;
        state->session_id = info_.session_id;
        state->device_id = info_.device.id;
        state->internal_id = NextCudaId();
        state->debug.resource_type = "Queue";
        state->debug.label = request.debug_label;

        {
            std::scoped_lock<std::mutex> lock(mutex_);
            queues_.emplace(state->internal_id, stream);
        }

        QueueAcquisitionResult result;
        result.handle = QueueHandle{state};
        return result;
    }

    std::expected<ReleaseResult, AcceleratorError>
    Release(const QueueHandle& handle, const ReleaseRequest& request = {}) override {
        auto valid = ValidateOwnedHandle(handle.state_, "ReleaseQueue", request.allow_if_released);
        if (!valid) {
            return std::unexpected(valid.error());
        }

        cudaStream_t stream = nullptr;
        bool removed = false;
        {
            std::scoped_lock<std::mutex> lock(mutex_);
            auto it = queues_.find(handle.state_->internal_id);
            if (it != queues_.end()) {
                stream = it->second;
                queues_.erase(it);
                removed = true;
            }
        }

        if (!removed && !request.allow_if_released) {
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::QueueFailed,
                                                    "ReleaseQueue",
                                                    "queue was not active"));
        }

        if (stream != nullptr) {
            const auto stream_error = cudaStreamDestroy(stream);
            if (stream_error != cudaSuccess) {
                return std::unexpected(MakeSessionError(info_,
                                                        AcceleratorErrorCategory::QueueFailed,
                                                        "ReleaseQueue",
                                                        BuildCudaDiagnostic(kCudaSessionCreationFailureDiagnostic,
                                                                            stream_error),
                                                        static_cast<int>(stream_error)));
            }
        }

        handle.state_->released.store(true, std::memory_order_release);

        ReleaseResult result;
        result.released = removed;
        return result;
    }

    std::expected<TransferResult, AcceleratorError>
    EnqueueHostToDevice(const HostAllocationHandle& source,
                        const DeviceAllocationHandle& destination,
                        const QueueHandle& queue,
                        const TransferRequest& request) override {
        return EnqueueTransfer(source.state_, destination.state_, queue.state_, request,
                               cudaMemcpyHostToDevice,
                               "EnqueueHostToDevice");
    }

    std::expected<TransferResult, AcceleratorError>
    EnqueueDeviceToHost(const DeviceAllocationHandle& source,
                        const HostAllocationHandle& destination,
                        const QueueHandle& queue,
                        const TransferRequest& request) override {
        return EnqueueTransfer(source.state_, destination.state_, queue.state_, request,
                               cudaMemcpyDeviceToHost,
                               "EnqueueDeviceToHost");
    }

    std::expected<WaitResult, AcceleratorError>
    Wait(const TransferCompletion& completion, const WaitRequest& request) override {
        auto valid = ValidateOwnedHandle(completion.state_, "WaitCompletion");
        if (!valid) {
            return std::unexpected(valid.error());
        }

        cudaEvent_t event = nullptr;
        {
            std::scoped_lock<std::mutex> lock(mutex_);
            auto event_it = events_.find(completion.state_->event_internal_id);
            if (event_it == events_.end()) {
                return std::unexpected(MakeSessionError(info_,
                                                        AcceleratorErrorCategory::EventFailed,
                                                        "WaitCompletion",
                                                        "completion event was not active"));
            }
            event = event_it->second;
        }

        auto set_device = SetDevice();
        if (!set_device) {
            return std::unexpected(set_device.error());
        }

        if (request.timeout == std::chrono::milliseconds::max()) {
            const auto sync_error = cudaEventSynchronize(event);
            if (sync_error != cudaSuccess) {
                return std::unexpected(MakeSessionError(info_,
                                                        AcceleratorErrorCategory::EventFailed,
                                                        "WaitCompletion",
                                                        BuildCudaDiagnostic(kCudaSynchronizationFailureDiagnostic,
                                                                            sync_error),
                                                        static_cast<int>(sync_error)));
            }
        } else {
            if (request.timeout < std::chrono::milliseconds{0}) {
                return std::unexpected(MakeSessionError(info_,
                                                        AcceleratorErrorCategory::InvalidArgument,
                                                        "WaitCompletion",
                                                        "timeout must be zero or positive"));
            }

            const auto deadline = std::chrono::steady_clock::now() + request.timeout;
            while (true) {
                const auto query_error = cudaEventQuery(event);
                if (query_error == cudaSuccess) {
                    break;
                }
                if (query_error != cudaErrorNotReady) {
                    return std::unexpected(MakeSessionError(info_,
                                                            AcceleratorErrorCategory::EventFailed,
                                                            "WaitCompletion",
                                                            BuildCudaDiagnostic(kCudaSynchronizationFailureDiagnostic,
                                                                                query_error),
                                                            static_cast<int>(query_error)));
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    return std::unexpected(MakeSessionError(info_,
                                                            AcceleratorErrorCategory::Timeout,
                                                            "WaitCompletion",
                                                            "CUDA wait timed out before transfer completion"));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
        }

        WaitResult result;
        result.completed = true;
        return result;
    }

    std::expected<ReleaseResult, AcceleratorError>
    Release(const TransferCompletion& completion, const ReleaseRequest& request = {}) override {
        auto valid = ValidateOwnedHandle(completion.state_, "ReleaseCompletion", request.allow_if_released);
        if (!valid) {
            return std::unexpected(valid.error());
        }

        auto set_device = SetDevice();
        if (!set_device) {
            return std::unexpected(set_device.error());
        }

        bool removed = false;
        cudaEvent_t event = nullptr;
        {
            std::scoped_lock<std::mutex> lock(mutex_);
            removed = completions_.erase(completion.state_->internal_id) > 0;

            auto event_it = events_.find(completion.state_->event_internal_id);
            if (event_it != events_.end()) {
                event = event_it->second;
                events_.erase(event_it);
            }
        }

        if (!removed && !request.allow_if_released) {
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::EventFailed,
                                                    "ReleaseCompletion",
                                                    "transfer completion was not active"));
        }

        if (event != nullptr) {
            const auto destroy_error = cudaEventDestroy(event);
            if (destroy_error != cudaSuccess) {
                return std::unexpected(MakeSessionError(info_,
                                                        AcceleratorErrorCategory::EventFailed,
                                                        "ReleaseCompletion",
                                                        BuildCudaDiagnostic(kCudaSynchronizationFailureDiagnostic,
                                                                            destroy_error),
                                                        static_cast<int>(destroy_error)));
            }
        }

        completion.event_.state_->released.store(true, std::memory_order_release);
        completion.state_->released.store(true, std::memory_order_release);

        ReleaseResult result;
        result.released = removed;
        return result;
    }

private:
    template <typename StateT>
    std::expected<void, AcceleratorError>
    ValidateOwnedHandle(const std::shared_ptr<StateT>& state,
                        const char* operation,
                        bool allow_if_released = false) const {
        if (!state) {
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::InvalidArgument,
                                                    operation,
                                                    "handle is not valid"));
        }
        if (state->provider_id != info_.provider_id || state->session_id != info_.session_id) {
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::CrossSessionResource,
                                                    operation,
                                                    "handle belongs to a different provider/session"));
        }
        if (!allow_if_released && state->released.load(std::memory_order_acquire)) {
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::InvalidState,
                                                    operation,
                                                    "handle was already released"));
        }
        return {};
    }

    std::expected<void, AcceleratorError> SetDevice() const {
        const auto set_error = cudaSetDevice(device_ordinal_);
        if (set_error != cudaSuccess) {
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::Unavailable,
                                                    "SetDevice",
                                                    BuildCudaDiagnostic(kCudaSessionCreationFailureDiagnostic,
                                                                        set_error),
                                                    static_cast<int>(set_error)));
        }
        return {};
    }

    template <typename StateT>
    std::expected<ReleaseResult, AcceleratorError>
    ReleaseSetCommon(const std::shared_ptr<StateT>& state,
                     const ReleaseRequest& request,
                     const char* operation,
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
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::InvalidState,
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
                    cudaMemcpyKind copy_kind,
                    const char* operation) {
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
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::InvalidArgument,
                                                    operation,
                                                    "transfer byte_size must be greater than zero"));
        }

        void* source_ptr = nullptr;
        void* destination_ptr = nullptr;
        std::size_t source_size = 0;
        std::size_t destination_size = 0;
        cudaStream_t stream = nullptr;

        {
            std::scoped_lock<std::mutex> lock(mutex_);
            auto queue_it = queues_.find(queue->internal_id);
            if (queue_it == queues_.end()) {
                return std::unexpected(MakeSessionError(info_,
                                                        AcceleratorErrorCategory::QueueFailed,
                                                        operation,
                                                        "queue is not active"));
            }
            stream = queue_it->second;

            if (copy_kind == cudaMemcpyHostToDevice) {
                auto host_it = host_allocations_.find(source->internal_id);
                auto device_it = device_allocations_.find(destination->internal_id);
                if (host_it == host_allocations_.end() || device_it == device_allocations_.end()) {
                    return std::unexpected(MakeSessionError(info_,
                                                            AcceleratorErrorCategory::TransferFailed,
                                                            operation,
                                                            "source or destination allocation is not active"));
                }
                source_ptr = host_it->second.data();
                source_size = host_it->second.size();
                destination_ptr = device_it->second;
                destination_size = destination->debug.byte_size;
            } else {
                auto device_it = device_allocations_.find(source->internal_id);
                auto host_it = host_allocations_.find(destination->internal_id);
                if (device_it == device_allocations_.end() || host_it == host_allocations_.end()) {
                    return std::unexpected(MakeSessionError(info_,
                                                            AcceleratorErrorCategory::TransferFailed,
                                                            operation,
                                                            "source or destination allocation is not active"));
                }
                source_ptr = device_it->second;
                source_size = source->debug.byte_size;
                destination_ptr = host_it->second.data();
                destination_size = host_it->second.size();
            }
        }

        if (request.source_offset > source_size || request.destination_offset > destination_size) {
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::InvalidArgument,
                                                    operation,
                                                    "source or destination offset is out of range"));
        }
        if (request.byte_size > source_size - request.source_offset ||
            request.byte_size > destination_size - request.destination_offset) {
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::TransferFailed,
                                                    operation,
                                                    "transfer range exceeds allocation bounds"));
        }

        auto set_device = SetDevice();
        if (!set_device) {
            return std::unexpected(set_device.error());
        }

        auto* source_bytes = static_cast<std::byte*>(source_ptr);
        auto* destination_bytes = static_cast<std::byte*>(destination_ptr);
        const auto copy_error = cudaMemcpyAsync(destination_bytes + request.destination_offset,
                                                source_bytes + request.source_offset,
                                                request.byte_size,
                                                copy_kind,
                                                stream);
        if (copy_error != cudaSuccess) {
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::TransferFailed,
                                                    operation,
                                                    BuildCudaDiagnostic(kCudaTransferFailureDiagnostic,
                                                                        copy_error),
                                                    static_cast<int>(copy_error)));
        }

        cudaEvent_t event = nullptr;
        const auto event_create_error = cudaEventCreateWithFlags(&event, cudaEventDisableTiming);
        if (event_create_error != cudaSuccess) {
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::EventFailed,
                                                    operation,
                                                    BuildCudaDiagnostic(kCudaSynchronizationFailureDiagnostic,
                                                                        event_create_error),
                                                    static_cast<int>(event_create_error)));
        }

        const auto event_record_error = cudaEventRecord(event, stream);
        if (event_record_error != cudaSuccess) {
            cudaEventDestroy(event);
            return std::unexpected(MakeSessionError(info_,
                                                    AcceleratorErrorCategory::EventFailed,
                                                    operation,
                                                    BuildCudaDiagnostic(kCudaSynchronizationFailureDiagnostic,
                                                                        event_record_error),
                                                    static_cast<int>(event_record_error)));
        }

        auto event_state = std::make_shared<EventHandle::State>();
        event_state->provider_id = info_.provider_id;
        event_state->session_id = info_.session_id;
        event_state->device_id = info_.device.id;
        event_state->internal_id = NextCudaId();
        event_state->debug.resource_type = "Event";
        event_state->debug.label = request.debug_label;

        auto completion_state = std::make_shared<TransferCompletion::State>();
        completion_state->provider_id = info_.provider_id;
        completion_state->session_id = info_.session_id;
        completion_state->device_id = info_.device.id;
        completion_state->internal_id = NextCudaId();
        completion_state->event_internal_id = event_state->internal_id;
        completion_state->debug.resource_type = "TransferCompletion";
        completion_state->debug.label = request.debug_label;
        completion_state->debug.byte_size = request.byte_size;

        {
            std::scoped_lock<std::mutex> lock(mutex_);
            events_.emplace(event_state->internal_id, event);
            completions_.emplace(completion_state->internal_id, completion_state->event_internal_id);
        }

        TransferResult result;
        result.completion = TransferCompletion{completion_state, EventHandle{event_state}};
        result.byte_size = request.byte_size;
        return result;
    }

    AcceleratorSessionInfo info_;
    int device_ordinal_{0};
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::vector<std::byte>> host_allocations_;
    std::unordered_map<std::uint64_t, void*> device_allocations_;
    std::unordered_map<std::uint64_t, cudaStream_t> queues_;
    std::unordered_map<std::uint64_t, cudaEvent_t> events_;
    std::unordered_map<std::uint64_t, std::uint64_t> completions_;
};
#endif

}  // namespace

CudaAcceleratorProvider::CudaAcceleratorProvider() {
    info_.provider_id = AcceleratorProviderId{"cuda.default"};
    info_.backend = AcceleratorBackend::Cuda;
    info_.execution_mode = AcceleratorExecutionMode::HostAsynchronous;

#if ACCELGRAPH_ENABLE_CUDA && ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE && ACCELGRAPH_CUDA_RUNTIME_HEADER_AVAILABLE
    int device_count = 0;
    const auto count_error = cudaGetDeviceCount(&device_count);
    if (count_error == cudaSuccess && device_count > 0) {
        for (int device_index = 0; device_index < device_count; ++device_index) {
            cudaDeviceProp props{};
            if (cudaGetDeviceProperties(&props, device_index) == cudaSuccess) {
                info_.devices.push_back(AcceleratorDeviceInfo{
                    .id = AcceleratorDeviceId{"cuda:" + std::to_string(device_index)},
                    .name = props.name,
                    .backend = AcceleratorBackend::Cuda,
                });
            }
        }
    }
#endif
}

AcceleratorProviderInfo CudaAcceleratorProvider::Info() const {
    return info_;
}

std::expected<std::shared_ptr<IAcceleratorSession>, AcceleratorError>
CudaAcceleratorProvider::CreateSession(const AcceleratorSessionCreateRequest& request) {
#if !ACCELGRAPH_ENABLE_CUDA
    return std::unexpected(MakeProviderError(info_,
                                             AcceleratorErrorCategory::Unsupported,
                                             "CreateSession",
                                             kCudaSupportNotCompiledDiagnostic,
                                             request.requested_device));
#elif !ACCELGRAPH_CUDA_TOOLKIT_AVAILABLE || !ACCELGRAPH_CUDA_RUNTIME_HEADER_AVAILABLE
    return std::unexpected(MakeProviderError(info_,
                                             AcceleratorErrorCategory::Unavailable,
                                             "CreateSession",
                                             kCudaToolkitUnavailableDiagnostic,
                                             request.requested_device));
#else
    int device_count = 0;
    const auto count_error = cudaGetDeviceCount(&device_count);
    if (count_error == cudaErrorInsufficientDriver) {
        return std::unexpected(MakeProviderError(info_,
                                                 AcceleratorErrorCategory::Unavailable,
                                                 "CreateSession",
                                                 kCudaDriverUnavailableDiagnostic,
                                                 request.requested_device));
    }
    if (count_error != cudaSuccess) {
        auto diagnostic = BuildCudaDiagnostic(kCudaSessionCreationFailureDiagnostic, count_error);
        return std::unexpected(MakeProviderError(info_,
                                                 AcceleratorErrorCategory::Unavailable,
                                                 "CreateSession",
                                                 diagnostic.c_str(),
                                                 request.requested_device));
    }
    if (device_count <= 0) {
        return std::unexpected(MakeProviderError(info_,
                                                 AcceleratorErrorCategory::Unavailable,
                                                 "CreateSession",
                                                 kCudaDeviceUnavailableDiagnostic,
                                                 request.requested_device));
    }

    int selected_device = 0;
    if (request.requested_device.has_value()) {
        const std::string& requested = request.requested_device->value;
        if (requested.rfind("cuda:", 0) != 0) {
            return std::unexpected(MakeProviderError(info_,
                                                     AcceleratorErrorCategory::InvalidArgument,
                                                     "CreateSession",
                                                     "requested CUDA device id must use 'cuda:<ordinal>' format",
                                                     request.requested_device));
        }
        try {
            selected_device = std::stoi(requested.substr(5));
        } catch (...) {
            return std::unexpected(MakeProviderError(info_,
                                                     AcceleratorErrorCategory::InvalidArgument,
                                                     "CreateSession",
                                                     "requested CUDA device ordinal is invalid",
                                                     request.requested_device));
        }
        if (selected_device < 0 || selected_device >= device_count) {
            return std::unexpected(MakeProviderError(info_,
                                                     AcceleratorErrorCategory::Unavailable,
                                                     "CreateSession",
                                                     kCudaDeviceUnavailableDiagnostic,
                                                     request.requested_device));
        }
    }

    const auto set_device_error = cudaSetDevice(selected_device);
    if (set_device_error != cudaSuccess) {
        auto diagnostic = BuildCudaDiagnostic(kCudaSessionCreationFailureDiagnostic, set_device_error);
        return std::unexpected(MakeProviderError(info_,
                                                 AcceleratorErrorCategory::Unavailable,
                                                 "CreateSession",
                                                 diagnostic.c_str(),
                                                 request.requested_device));
    }

    cudaDeviceProp props{};
    const auto prop_error = cudaGetDeviceProperties(&props, selected_device);
    if (prop_error != cudaSuccess) {
        auto diagnostic = BuildCudaDiagnostic(kCudaSessionCreationFailureDiagnostic, prop_error);
        return std::unexpected(MakeProviderError(info_,
                                                 AcceleratorErrorCategory::Unavailable,
                                                 "CreateSession",
                                                 diagnostic.c_str(),
                                                 request.requested_device));
    }

    AcceleratorSessionInfo session_info;
    session_info.provider_id = info_.provider_id;
    session_info.session_id = AcceleratorSessionId{NextCudaId()};
    session_info.backend = info_.backend;
    session_info.execution_mode = info_.execution_mode;
    session_info.device = AcceleratorDeviceInfo{
        .id = AcceleratorDeviceId{"cuda:" + std::to_string(selected_device)},
        .name = props.name,
        .backend = AcceleratorBackend::Cuda,
    };

    return std::make_shared<CudaAcceleratorSession>(std::move(session_info), selected_device);
#endif
}

}  // namespace accelgraph