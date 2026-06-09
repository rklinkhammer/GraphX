#pragma once

#include "sar/SarMessages.hpp"

#include <cstdint>
#include <optional>

namespace sar::detail {

struct AccelTokenSidecar {
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
};

void StoreAccelTokenSidecar(std::uint64_t token, const AccelTokenSidecar& sidecar);
std::optional<AccelTokenSidecar> FindAccelTokenSidecar(std::uint64_t token);
void UpdateAccelTokenSidecarH2D(std::uint64_t token,
                                std::uint32_t backend_id,
                                SarBackendKind backend,
                                std::uint64_t queue_id);
void UpdateAccelTokenSidecarKernel(std::uint64_t token,
                                   std::uint32_t backend_id,
                                   SarBackendKind backend,
                                   std::uint64_t queue_id);
void UpdateAccelTokenSidecarD2H(std::uint64_t token,
                                std::uint32_t backend_id,
                                SarBackendKind backend,
                                std::uint64_t queue_id);

} // namespace sar::detail
