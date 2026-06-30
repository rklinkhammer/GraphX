#include "gpu/metal/native/NativeMetalRuntimeInternal.hpp"

namespace graph::gpu::metal::capabilities {

bool NativeMetalCollectiveCapability::AllReduce(accel::DeviceBufferView& in_out,
                                                const accel::CollectiveTicket& ticket) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    if (!accel::IsValidView(in_out) || !accel::IsValidCollectiveTicket(ticket)) {
        return false;
    }

    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    ++telemetry.error_count;
    ++telemetry.error_code_counts["unsupported-metal-collective-allreduce"];
    return false;
}

bool NativeMetalCollectiveCapability::AllGather(const accel::DeviceBufferView& input,
                                                accel::DeviceBufferView& output,
                                                const accel::CollectiveTicket& ticket) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    if (!accel::IsValidView(input) || !accel::IsValidView(output) ||
        !accel::IsValidCollectiveTicket(ticket)) {
        return false;
    }

    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    ++telemetry.error_count;
    ++telemetry.error_code_counts["unsupported-metal-collective-allgather"];
    return false;
}

bool NativeMetalCollectiveCapability::ReduceScatter(const accel::DeviceBufferView& input,
                                                    accel::DeviceBufferView& output,
                                                    const accel::CollectiveTicket& ticket) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    if (!accel::IsValidView(input) || !accel::IsValidView(output) ||
        !accel::IsValidCollectiveTicket(ticket)) {
        return false;
    }

    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    ++telemetry.error_count;
    ++telemetry.error_code_counts["unsupported-metal-collective-reducescatter"];
    return false;
}

} // namespace graph::gpu::metal::capabilities
