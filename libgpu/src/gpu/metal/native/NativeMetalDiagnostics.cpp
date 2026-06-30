#include "gpu/metal/native/NativeMetalRuntimeInternal.hpp"

namespace graph::gpu::metal::capabilities {

void NativeMetalTelemetryCapability::RecordTransfer(const accel::TransferTicket& ticket,
                                                    std::uint64_t duration_ns) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    if (!accel::IsValidTransferTicket(ticket)) {
        IncrementErrorCounter("invalid-transfer-ticket");
        return;
    }

    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    ++telemetry.transfer_samples;
    telemetry.transfer_total_duration_ns += duration_ns;
    telemetry.last_transfer_duration_ns = duration_ns;
    if (accel::IsValidView(ticket.src_host) && accel::IsValidView(ticket.dst_device)) {
        ++telemetry.h2d_transfer_samples;
    } else if (accel::IsValidView(ticket.src_device) && accel::IsValidView(ticket.dst_host)) {
        ++telemetry.d2h_transfer_samples;
    } else if (accel::IsValidView(ticket.src_device) && accel::IsValidView(ticket.dst_device)) {
        ++telemetry.d2d_transfer_samples;
    }
}

void NativeMetalTelemetryCapability::RecordKernel(const accel::KernelTicket& ticket,
                                                  std::uint64_t duration_ns) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    if (!accel::IsValidKernelTicket(ticket)) {
        IncrementErrorCounter("invalid-kernel-ticket");
        return;
    }

    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    ++telemetry.kernel_samples;
    telemetry.kernel_total_duration_ns += duration_ns;
    telemetry.last_kernel_duration_ns = duration_ns;
}

/**
 * @brief Increment error counter.
 * @param error_code Parameter for increment error counter.
 */
void NativeMetalTelemetryCapability::IncrementErrorCounter(std::string_view error_code) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    ++telemetry.error_count;
    telemetry.error_code_counts[std::string(error_code)]++;
}

/**
 * @brief Transfer samples.
 */
std::uint64_t NativeMetalTelemetryCapability::TransferSamples() const {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    return telemetry.transfer_samples;
}

/**
 * @brief Kernel samples.
 */
std::uint64_t NativeMetalTelemetryCapability::KernelSamples() const {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    return telemetry.kernel_samples;
}

/**
 * @brief Error count.
 */
std::uint64_t NativeMetalTelemetryCapability::ErrorCount() const {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    return telemetry.error_count;
}

/**
 * @brief Snapshot.
 */
IMetalTelemetryCapability::TelemetrySnapshot NativeMetalTelemetryCapability::Snapshot() const {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);

    TelemetrySnapshot out{};
    out.transfer_samples = telemetry.transfer_samples;
    out.kernel_samples = telemetry.kernel_samples;
    out.error_count = telemetry.error_count;
    out.transfer_total_duration_ns = telemetry.transfer_total_duration_ns;
    out.kernel_total_duration_ns = telemetry.kernel_total_duration_ns;
    out.last_transfer_duration_ns = telemetry.last_transfer_duration_ns;
    out.last_kernel_duration_ns = telemetry.last_kernel_duration_ns;
    out.h2d_transfer_samples = telemetry.h2d_transfer_samples;
    out.d2h_transfer_samples = telemetry.d2h_transfer_samples;
    out.d2d_transfer_samples = telemetry.d2d_transfer_samples;
    return out;
}

#if GRAPHX_ENABLE_GPU_TEST_HOOKS
/**
 * @brief Reset for testing.
 */
void NativeMetalTelemetryCapability::ResetForTesting() {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    telemetry.transfer_samples = 0;
    telemetry.kernel_samples = 0;
    telemetry.error_count = 0;
    telemetry.transfer_total_duration_ns = 0;
    telemetry.kernel_total_duration_ns = 0;
    telemetry.last_transfer_duration_ns = 0;
    telemetry.last_kernel_duration_ns = 0;
    telemetry.h2d_transfer_samples = 0;
    telemetry.d2h_transfer_samples = 0;
    telemetry.d2d_transfer_samples = 0;
    telemetry.error_code_counts.clear();
}
#endif

} // namespace graph::gpu::metal::capabilities
