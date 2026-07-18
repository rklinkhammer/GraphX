// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace accelgraph {

enum class AcceleratorBackend {
    Cpu,
    Metal,
    Cuda
};

enum class AcceleratorExecutionMode {
    HostSynchronous,
    HostAsynchronous
};

enum class AcceleratorErrorCategory {
    Unsupported,
    Unavailable,
    InvalidArgument,
    InvalidState,
    AllocationFailed,
    TransferFailed,
    QueueFailed,
    EventFailed,
    CrossSessionResource,
    Timeout,
    BackendFailure
};

struct AcceleratorProviderId {
    std::string value;

    [[nodiscard]] bool empty() const noexcept { return value.empty(); }
    bool operator==(const AcceleratorProviderId& other) const noexcept {
        return value == other.value;
    }
};

struct AcceleratorSessionId {
    std::uint64_t value{0};

    [[nodiscard]] bool IsValid() const noexcept { return value != 0; }
    bool operator==(const AcceleratorSessionId& other) const noexcept {
        return value == other.value;
    }
};

struct AcceleratorDeviceId {
    std::string value;

    [[nodiscard]] bool empty() const noexcept { return value.empty(); }
    bool operator==(const AcceleratorDeviceId& other) const noexcept {
        return value == other.value;
    }
};

struct AcceleratorDeviceInfo {
    AcceleratorDeviceId id;
    std::string name;
    AcceleratorBackend backend{AcceleratorBackend::Cpu};
};

struct AcceleratorProviderInfo {
    AcceleratorProviderId provider_id;
    AcceleratorBackend backend{AcceleratorBackend::Cpu};
    AcceleratorExecutionMode execution_mode{AcceleratorExecutionMode::HostSynchronous};
    std::vector<AcceleratorDeviceInfo> devices;
};

struct AcceleratorSessionInfo {
    AcceleratorProviderId provider_id;
    AcceleratorSessionId session_id;
    AcceleratorBackend backend{AcceleratorBackend::Cpu};
    AcceleratorExecutionMode execution_mode{AcceleratorExecutionMode::HostSynchronous};
    AcceleratorDeviceInfo device;
};

struct AcceleratorError {
    AcceleratorErrorCategory category{AcceleratorErrorCategory::BackendFailure};
    AcceleratorBackend backend{AcceleratorBackend::Cpu};
    AcceleratorExecutionMode execution_mode{AcceleratorExecutionMode::HostSynchronous};
    AcceleratorProviderId provider_id;
    std::optional<AcceleratorSessionId> session_id;
    std::optional<AcceleratorDeviceId> device_id;
    std::string operation;
    std::optional<int> native_error_code;
    std::string diagnostic;
};

struct HandleDebugInfo {
    std::string resource_type;
    std::string label;
    std::size_t byte_size{0};
};

class HostAllocationHandle {
public:
    HostAllocationHandle() = default;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool IsReleased() const noexcept;
    [[nodiscard]] const AcceleratorProviderId& ProviderId() const noexcept;
    [[nodiscard]] AcceleratorSessionId SessionId() const noexcept;
    [[nodiscard]] const AcceleratorDeviceId& DeviceId() const noexcept;
    [[nodiscard]] const HandleDebugInfo& DebugInfo() const noexcept;

private:
    struct State;
    std::shared_ptr<State> state_;

    explicit HostAllocationHandle(std::shared_ptr<State> state);

    friend class CpuAcceleratorSession;
    friend class MetalAcceleratorSession;
    friend class CudaAcceleratorSession;
};

class DeviceAllocationHandle {
public:
    DeviceAllocationHandle() = default;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool IsReleased() const noexcept;
    [[nodiscard]] const AcceleratorProviderId& ProviderId() const noexcept;
    [[nodiscard]] AcceleratorSessionId SessionId() const noexcept;
    [[nodiscard]] const AcceleratorDeviceId& DeviceId() const noexcept;
    [[nodiscard]] const HandleDebugInfo& DebugInfo() const noexcept;

private:
    struct State;
    std::shared_ptr<State> state_;

    explicit DeviceAllocationHandle(std::shared_ptr<State> state);

    friend class CpuAcceleratorSession;
    friend class MetalAcceleratorSession;
    friend class CudaAcceleratorSession;
};

class QueueHandle {
public:
    QueueHandle() = default;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool IsReleased() const noexcept;
    [[nodiscard]] const AcceleratorProviderId& ProviderId() const noexcept;
    [[nodiscard]] AcceleratorSessionId SessionId() const noexcept;
    [[nodiscard]] const AcceleratorDeviceId& DeviceId() const noexcept;
    [[nodiscard]] const HandleDebugInfo& DebugInfo() const noexcept;

private:
    struct State;
    std::shared_ptr<State> state_;

    explicit QueueHandle(std::shared_ptr<State> state);

    friend class CpuAcceleratorSession;
    friend class MetalAcceleratorSession;
    friend class CudaAcceleratorSession;
};

class EventHandle {
public:
    EventHandle() = default;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool IsReleased() const noexcept;
    [[nodiscard]] const AcceleratorProviderId& ProviderId() const noexcept;
    [[nodiscard]] AcceleratorSessionId SessionId() const noexcept;
    [[nodiscard]] const AcceleratorDeviceId& DeviceId() const noexcept;
    [[nodiscard]] const HandleDebugInfo& DebugInfo() const noexcept;

private:
    struct State;
    std::shared_ptr<State> state_;

    explicit EventHandle(std::shared_ptr<State> state);

    friend class CpuAcceleratorSession;
    friend class MetalAcceleratorSession;
    friend class CudaAcceleratorSession;
};

class TransferCompletion {
public:
    TransferCompletion() = default;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool IsReleased() const noexcept;
    [[nodiscard]] const AcceleratorProviderId& ProviderId() const noexcept;
    [[nodiscard]] AcceleratorSessionId SessionId() const noexcept;
    [[nodiscard]] const AcceleratorDeviceId& DeviceId() const noexcept;
    [[nodiscard]] const HandleDebugInfo& DebugInfo() const noexcept;
    [[nodiscard]] const EventHandle& Event() const noexcept;

private:
    struct State;
    std::shared_ptr<State> state_;
    EventHandle event_;

    TransferCompletion(std::shared_ptr<State> state, EventHandle event);

    friend class CpuAcceleratorSession;
    friend class MetalAcceleratorSession;
    friend class CudaAcceleratorSession;
};

struct HostAllocationRequest {
    std::size_t byte_size{0};
    std::string debug_label;
};

struct DeviceAllocationRequest {
    std::size_t byte_size{0};
    std::string debug_label;
};

struct QueueRequest {
    std::string debug_label;
};

struct TransferRequest {
    std::size_t byte_size{0};
    std::size_t source_offset{0};
    std::size_t destination_offset{0};
    std::string debug_label;
};

struct WaitRequest {
    std::chrono::milliseconds timeout{std::chrono::milliseconds::max()};
};

struct ReleaseRequest {
    bool allow_if_released{false};
};

struct HostAllocationResult {
    HostAllocationHandle handle;
    std::size_t byte_size{0};
};

struct DeviceAllocationResult {
    DeviceAllocationHandle handle;
    std::size_t byte_size{0};
};

struct QueueAcquisitionResult {
    QueueHandle handle;
};

struct TransferResult {
    TransferCompletion completion;
    std::size_t byte_size{0};
};

struct WaitResult {
    bool completed{false};
};

struct ReleaseResult {
    bool released{false};
};

struct HostWriteRequest {
    std::span<const std::byte> source{};
    std::size_t destination_offset{0};
};

struct HostReadRequest {
    std::size_t byte_size{0};
    std::size_t source_offset{0};
};

struct HostWriteResult {
    std::size_t bytes_written{0};
};

struct HostReadResult {
    std::vector<std::byte> bytes;
};

struct AcceleratorSessionCreateRequest {
    std::optional<AcceleratorDeviceId> requested_device;
    std::string debug_label;
};

class IAcceleratorSession {
public:
    virtual ~IAcceleratorSession() = default;

    [[nodiscard]] virtual AcceleratorSessionInfo Info() const = 0;

    virtual std::expected<HostAllocationResult, AcceleratorError>
    AllocateHost(const HostAllocationRequest& request) = 0;

    virtual std::expected<HostWriteResult, AcceleratorError>
    WriteHost(const HostAllocationHandle& handle, const HostWriteRequest& request) = 0;

    virtual std::expected<HostReadResult, AcceleratorError>
    ReadHost(const HostAllocationHandle& handle, const HostReadRequest& request) = 0;

    virtual std::expected<DeviceAllocationResult, AcceleratorError>
    AllocateDevice(const DeviceAllocationRequest& request) = 0;

    virtual std::expected<ReleaseResult, AcceleratorError>
    Release(const HostAllocationHandle& handle, const ReleaseRequest& request = {}) = 0;

    virtual std::expected<ReleaseResult, AcceleratorError>
    Release(const DeviceAllocationHandle& handle, const ReleaseRequest& request = {}) = 0;

    virtual std::expected<QueueAcquisitionResult, AcceleratorError>
    AcquireQueue(const QueueRequest& request) = 0;

    virtual std::expected<ReleaseResult, AcceleratorError>
    Release(const QueueHandle& handle, const ReleaseRequest& request = {}) = 0;

    virtual std::expected<TransferResult, AcceleratorError>
    EnqueueHostToDevice(const HostAllocationHandle& source,
                        const DeviceAllocationHandle& destination,
                        const QueueHandle& queue,
                        const TransferRequest& request) = 0;

    virtual std::expected<TransferResult, AcceleratorError>
    EnqueueDeviceToHost(const DeviceAllocationHandle& source,
                        const HostAllocationHandle& destination,
                        const QueueHandle& queue,
                        const TransferRequest& request) = 0;

    virtual std::expected<WaitResult, AcceleratorError>
    Wait(const TransferCompletion& completion, const WaitRequest& request) = 0;

    virtual std::expected<ReleaseResult, AcceleratorError>
    Release(const TransferCompletion& completion, const ReleaseRequest& request = {}) = 0;
};

class IAcceleratorProvider {
public:
    virtual ~IAcceleratorProvider() = default;

    [[nodiscard]] virtual AcceleratorProviderInfo Info() const = 0;

    virtual std::expected<std::shared_ptr<IAcceleratorSession>, AcceleratorError>
    CreateSession(const AcceleratorSessionCreateRequest& request) = 0;
};

}  // namespace accelgraph