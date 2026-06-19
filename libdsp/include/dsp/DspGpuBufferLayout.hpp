/**
 * @file DspGpuBufferLayout.hpp
 * @brief DSP GPU buffer layout contracts.
 *
 * @details Defines the narrow host/device tensor layouts used by DSP GPU
 * transfer nodes. These helpers describe byte shape only; they do not allocate
 * buffers, launch kernels, or imply that a token is resident on a device.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "dsp/IqPacket.hpp"
#include "dsp/MagnitudePacket.hpp"
#include "gpu/accel/types/AccelTypes.hpp"

#include <cstddef>
#include <cstdint>

namespace dsp {

/**
 * @brief DSP GPU packet layout constants and helpers.
 *
 * @details IQ samples are represented as contiguous Float32 I/Q pairs with
 * layout `[sample, component]`, where component 0 is I and component 1 is Q.
 * Magnitude spectra are represented as contiguous Float32 bins with layout
 * `[bin]`. These contracts are intentionally limited to `float` packets until
 * a later PR adds broader type support.
 */
template <std::size_t N>
struct DspGpuBufferLayout {
    static constexpr std::size_t kIqComponentCount = 2;
    static constexpr std::size_t kMagnitudeBinCount = N / 2;
    static constexpr std::size_t kIqFloatCount = N * kIqComponentCount;
    static constexpr std::size_t kMagnitudeFloatCount = kMagnitudeBinCount;
    static constexpr std::uint64_t kIqBytes =
        static_cast<std::uint64_t>(kIqFloatCount * sizeof(float));
    static constexpr std::uint64_t kMagnitudeBytes =
        static_cast<std::uint64_t>(kMagnitudeFloatCount * sizeof(float));

    [[nodiscard]] static constexpr graph::gpu::accel::TensorLayout IqTensorLayout() noexcept {
        graph::gpu::accel::TensorLayout layout{};
        layout.rank = 2;
        layout.shape[0] = N;
        layout.shape[1] = kIqComponentCount;
        layout.stride[0] = kIqComponentCount;
        layout.stride[1] = 1;
        return layout;
    }

    [[nodiscard]] static constexpr graph::gpu::accel::TensorLayout MagnitudeTensorLayout() noexcept {
        graph::gpu::accel::TensorLayout layout{};
        layout.rank = 1;
        layout.shape[0] = kMagnitudeBinCount;
        layout.stride[0] = 1;
        return layout;
    }

    [[nodiscard]] static constexpr graph::gpu::accel::DataType ScalarDataType() noexcept {
        return graph::gpu::accel::DataType::Float32;
    }
};

template <std::size_t N>
[[nodiscard]] constexpr std::uint64_t IqPacketDeviceBytes() noexcept {
    return DspGpuBufferLayout<N>::kIqBytes;
}

template <std::size_t N>
[[nodiscard]] constexpr std::uint64_t MagnitudePacketDeviceBytes() noexcept {
    return DspGpuBufferLayout<N>::kMagnitudeBytes;
}

template <std::size_t N>
[[nodiscard]] constexpr graph::gpu::accel::TensorLayout IqPacketDeviceLayout() noexcept {
    return DspGpuBufferLayout<N>::IqTensorLayout();
}

template <std::size_t N>
[[nodiscard]] constexpr graph::gpu::accel::TensorLayout MagnitudePacketDeviceLayout() noexcept {
    return DspGpuBufferLayout<N>::MagnitudeTensorLayout();
}

}  // namespace dsp
