#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
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

struct SarMessageEnvelope {
    std::uint64_t sequence_id{};
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

using SarIqSample = std::complex<float>;

struct SarPulseBlockMessage {
    SarMessageEnvelope envelope{};
    SarBufferDescriptor buffer{};
    std::vector<SarIqSample> iq_samples{};
};

struct SarRangeTileMessage {
    SarMessageEnvelope envelope{};
    SarBufferDescriptor buffer{};
    std::vector<float> range_bins{};
};

struct SarDeviceLeaseMessage {
    SarMessageEnvelope envelope{};
    std::uint32_t lease_token{};
    std::uint32_t device_index{};
    bool granted{false};
};

struct SarTransferTicketMessage {
    SarMessageEnvelope envelope{};
    SarBufferDescriptor buffer{};
    std::uint64_t transfer_id{};
    std::uint64_t byte_count{};
};

struct SarDispatchMetadata {
    std::uint32_t queue_id{};
    std::uint32_t kernel_id{};
    std::uint32_t dispatch_width{};
    std::uint32_t dispatch_height{};
    std::uint32_t dispatch_depth{1};
};

struct SarImageTileMessage {
    SarMessageEnvelope envelope{};
    SarBufferDescriptor buffer{};
    SarDispatchMetadata dispatch{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<float> pixels{};
};

struct SarMergeStatusMessage {
    SarMessageEnvelope envelope{};
    std::uint32_t expected_tiles{};
    std::uint32_t received_tiles{};
    std::uint32_t duplicate_tiles{};
    std::uint32_t missing_tiles{};
    std::uint32_t out_of_order_tiles{};
    std::uint64_t bytes_h2d{};
    std::uint64_t bytes_d2h{};
    std::uint64_t kernel_dispatches{};
    bool watermark_seen{false};
    std::uint64_t fanin_wait_ms{};
    bool complete{false};
};

struct SarDiagnosticsMessage {
    SarMessageEnvelope envelope{};
    std::uint64_t pulses_processed{};
    std::uint64_t tiles_processed{};
    std::uint64_t bytes_h2d{};
    std::uint64_t bytes_d2h{};
    std::uint64_t kernel_dispatches{};
    std::uint64_t fanin_wait_ms{};
    std::uint64_t e2e_latency_ms{};
    std::uint64_t duplicate_tile_count{};
    std::uint64_t missing_tile_count{};
    std::uint64_t queue_backpressure_events{};
    std::uint64_t peak_queue_depth{};
};

} // namespace sar
