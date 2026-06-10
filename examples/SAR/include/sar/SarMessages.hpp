#pragma once

#include "gpu/accel/types/AccelTypes.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace sar {

enum class SarBackendKind : std::uint8_t {
    Host,
    SimulatedDevice,
    NativeDevice,
};

enum class SarFrameMarker : std::uint8_t {
    Data,
    Watermark,
    EndOfStream,
};

enum class SarTransferDirection : std::uint8_t {
    HostToDevice,
    DeviceToHost,
};

inline graph::gpu::accel::BackendKind ToAccelBackendKind(SarBackendKind backend) noexcept {
    switch (backend) {
        case SarBackendKind::SimulatedDevice:
        case SarBackendKind::NativeDevice:
            return graph::gpu::accel::BackendKind::Metal;
        case SarBackendKind::Host:
            return graph::gpu::accel::BackendKind::Unknown;
    }
    return graph::gpu::accel::BackendKind::Unknown;
}

inline graph::gpu::accel::TensorLayout MakeAccelVectorLayout(std::uint64_t element_count) noexcept {
    graph::gpu::accel::TensorLayout layout{};
    layout.rank = 1;
    layout.shape[0] = element_count;
    layout.stride[0] = 1;
    return layout;
}

struct SarMessageEnvelope {
    std::uint64_t sequence_id{};
    std::uint64_t batch_id{};
    std::uint64_t aperture_id{};
    std::uint64_t pulse_range_start{};
    std::uint32_t pulse_range_count{};
    std::uint32_t stream_id{};
    std::uint32_t tile_id{};
    std::uint32_t tile_count{};
    std::uint32_t backend_id{};
    SarBackendKind backend{SarBackendKind::Host};
    SarFrameMarker marker{SarFrameMarker::Data};
    bool synthetic{true};
};

struct SarBufferDescriptor {
    std::uint64_t buffer_id{};
    std::size_t byte_count{};
    std::uint32_t device_index{};
    SarBackendKind backend{SarBackendKind::Host};
    SarTransferDirection direction{SarTransferDirection::HostToDevice};
};

struct SarGpuMetadata {
    graph::gpu::accel::BufferLease lease{};
    graph::gpu::accel::DeviceBufferView device_view{};
    graph::gpu::accel::HostPinnedBufferView host_view{};
    graph::gpu::accel::TransferTicket transfer_ticket{};
    graph::gpu::accel::KernelTicket kernel_ticket{};
    bool has_lease{false};
    bool has_device_view{false};
    bool has_host_view{false};
    bool has_transfer_ticket{false};
    bool has_kernel_ticket{false};
};

struct SarStageTimingMetrics {
    std::uint64_t range_window_time_us{};
    std::uint64_t range_compression_time_us{};
    std::uint64_t split_time_us{};
    std::uint64_t h2d_stage_time_us{};
    std::uint64_t backprojection_stage_time_us{};
    std::uint64_t d2h_stage_time_us{};
    std::uint64_t merge_stage_time_us{};
    std::uint64_t diagnostics_sink_time_us{};
};

struct SarSidecar {
    std::uint64_t sequence_id{};
    std::uint64_t batch_id{};
    std::uint64_t aperture_id{};
    std::uint64_t pulse_range_start{};
    std::uint32_t pulse_range_count{};
    std::uint32_t stream_id{};
    std::uint32_t tile_id{};
    std::uint32_t tile_count{};
    std::uint32_t backend_id{};
    SarBackendKind backend{SarBackendKind::Host};
    SarFrameMarker marker{SarFrameMarker::Data};
    bool synthetic{true};
    std::size_t payload_byte_count{};
    std::uint64_t h2d_queue_id{};
    std::uint64_t kernel_queue_id{};
    std::uint64_t d2h_queue_id{};
    SarStageTimingMetrics stage_timings{};
};

template <typename SidecarT>
struct AccelControlToken {
    std::uint64_t token_id{};
    SidecarT sidecar{};
    graph::gpu::accel::BufferLease lease{};
    graph::gpu::accel::DeviceBufferView device_view{};
    graph::gpu::accel::HostPinnedBufferView host_view{};
    graph::gpu::accel::TransferTicket transfer_ticket{};
    graph::gpu::accel::KernelTicket kernel_ticket{};
    bool has_lease{false};
    bool has_device_view{false};
    bool has_host_view{false};
    bool has_transfer_ticket{false};
    bool has_kernel_ticket{false};
};

using SarAccelControlToken = AccelControlToken<SarSidecar>;

static_assert(std::is_standard_layout_v<SarSidecar>);
static_assert(std::is_standard_layout_v<SarAccelControlToken>);

using SarIqSample = std::complex<float>;

struct SarPulseBlockMessage {
    SarMessageEnvelope envelope{};
    SarBufferDescriptor buffer{};
    SarStageTimingMetrics stage_timings{};
    std::vector<SarIqSample> iq_samples{};
};

struct SarDispatchMetadata {
    std::uint32_t queue_id{};
    std::uint32_t kernel_id{};
    std::uint32_t dispatch_width{};
    std::uint32_t dispatch_height{};
    std::uint32_t dispatch_depth{1};
};

struct SarMergeStatusMessage {
    SarMessageEnvelope envelope{};
    SarGpuMetadata gpu{};
    SarStageTimingMetrics stage_timings{};
    std::uint32_t expected_tiles{};
    std::uint32_t received_tiles{};
    std::uint32_t duplicate_tiles{};
    std::uint32_t missing_tiles{};
    std::uint32_t out_of_order_tiles{};
    std::uint64_t bytes_h2d{};
    std::uint64_t bytes_d2h{};
    std::uint64_t kernel_dispatches{};
    std::uint64_t transfer_h2d_time_us{};
    std::uint64_t kernel_exec_time_us{};
    std::uint64_t transfer_d2h_time_us{};
    bool watermark_seen{false};
    std::uint64_t fanin_wait_ms{};
    bool complete{false};
};

struct SarDiagnosticsMessage {
    SarMessageEnvelope envelope{};
    SarStageTimingMetrics stage_timings{};
    std::uint64_t pulses_processed{};
    std::uint64_t tiles_processed{};
    std::uint64_t bytes_h2d{};
    std::uint64_t bytes_d2h{};
    std::uint64_t kernel_dispatches{};
    std::uint64_t transfer_h2d_time_us{};
    std::uint64_t kernel_exec_time_us{};
    std::uint64_t transfer_d2h_time_us{};
    std::uint64_t fanin_wait_ms{};
    std::uint64_t e2e_latency_ms{};
    std::uint64_t duplicate_tile_count{};
    std::uint64_t missing_tile_count{};
    std::uint64_t out_of_order_completion_count{};
    std::uint64_t queue_backpressure_events{};
    std::uint64_t peak_queue_depth{};
};

} // namespace sar
