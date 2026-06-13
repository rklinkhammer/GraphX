#pragma once

#include "sar/io/NormalizedSarProduct.hpp"

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace graphx::sar {

struct SarValidationIssue {
    std::string code{};
    std::string path{};
    std::string message{};
};

struct SarValidationResult {
    std::vector<SarValidationIssue> errors{};

    [[nodiscard]] bool ok() const noexcept {
        return errors.empty();
    }
};

class SarProductValidator {
public:
    [[nodiscard]] static SarValidationResult Validate(const NormalizedSarProduct& product) {
        SarValidationResult result{};
        ValidateRequiredFields(product, result);
        ValidateFiniteMetadata(product, result);
        ValidateShapeConsistency(product, result);
        ValidatePulseOrdering(product, result);
        ValidateSampleTypes(product, result);
        ValidateFiniteSamples(product, result);
        return result;
    }

private:
    static void AddError(
        SarValidationResult& result,
        std::string code,
        std::string path,
        std::string message) {
        result.errors.push_back(SarValidationIssue{
            .code = std::move(code),
            .path = std::move(path),
            .message = std::move(message),
        });
    }

    [[nodiscard]] static std::string ChannelPath(std::size_t channel_index) {
        return "channels[" + std::to_string(channel_index) + "]";
    }

    [[nodiscard]] static std::string PulsePath(std::size_t channel_index, std::size_t pulse_index) {
        return ChannelPath(channel_index) + ".pulses[" + std::to_string(pulse_index) + "]";
    }

    [[nodiscard]] static std::string SamplePath(
        std::size_t channel_index,
        std::size_t pulse_index,
        std::size_t sample_index) {
        return PulsePath(channel_index, pulse_index) + ".samples[" + std::to_string(sample_index) + "]";
    }

    static void ValidateRequiredFields(const NormalizedSarProduct& product, SarValidationResult& result) {
        for (const auto& path : product.MissingRequiredFields()) {
            AddError(
                result,
                "missing_required_field",
                path,
                "required normalized SAR product field is missing");
        }
    }

    static void ValidateFiniteMetadata(const NormalizedSarProduct& product, SarValidationResult& result) {
        ValidateFiniteArray(product.reference_geometry.scene_center_m, "reference_geometry.scene_center_m", result);
        ValidateFiniteArray(
            product.reference_geometry.reference_platform.position_m,
            "reference_geometry.reference_platform.position_m",
            result);
        ValidateFiniteArray(
            product.reference_geometry.reference_platform.velocity_mps,
            "reference_geometry.reference_platform.velocity_mps",
            result);

        for (std::size_t channel_index = 0; channel_index < product.channels.size(); ++channel_index) {
            const auto& channel = product.channels[channel_index];
            const auto prefix = ChannelPath(channel_index);
            ValidateFinite(channel.waveform.carrier_hz, prefix + ".waveform.carrier_hz", result);
            ValidateFinite(channel.waveform.bandwidth_hz, prefix + ".waveform.bandwidth_hz", result);
            ValidateFinite(channel.waveform.sample_rate_hz, prefix + ".waveform.sample_rate_hz", result);

            for (std::size_t pulse_index = 0; pulse_index < channel.pulses.size(); ++pulse_index) {
                const auto& pulse = channel.pulses[pulse_index];
                const auto pulse_prefix = PulsePath(channel_index, pulse_index);
                ValidateFinite(pulse.parameters.time_seconds, pulse_prefix + ".parameters.time_seconds", result);
                ValidateFiniteArray(
                    pulse.parameters.platform.position_m,
                    pulse_prefix + ".parameters.platform.position_m",
                    result);
                ValidateFiniteArray(
                    pulse.parameters.platform.velocity_mps,
                    pulse_prefix + ".parameters.platform.velocity_mps",
                    result);
            }
        }
    }

    static void ValidateShapeConsistency(const NormalizedSarProduct& product, SarValidationResult& result) {
        if (product.channels.empty()) {
            return;
        }

        const auto expected_pulses = product.channels.front().pulses.size();
        std::size_t expected_samples = 0;
        if (!product.channels.front().pulses.empty()) {
            expected_samples = product.channels.front().pulses.front().samples.size();
        }

        for (std::size_t channel_index = 0; channel_index < product.channels.size(); ++channel_index) {
            const auto& channel = product.channels[channel_index];
            if (channel.pulses.size() != expected_pulses) {
                AddError(
                    result,
                    "shape_mismatch",
                    ChannelPath(channel_index) + ".pulses",
                    "channel pulse count does not match the first channel");
            }

            for (std::size_t pulse_index = 0; pulse_index < channel.pulses.size(); ++pulse_index) {
                if (channel.pulses[pulse_index].samples.size() != expected_samples) {
                    AddError(
                        result,
                        "shape_mismatch",
                        PulsePath(channel_index, pulse_index) + ".samples",
                        "pulse sample count does not match the first pulse");
                }
            }
        }
    }

    static void ValidatePulseOrdering(const NormalizedSarProduct& product, SarValidationResult& result) {
        for (std::size_t channel_index = 0; channel_index < product.channels.size(); ++channel_index) {
            const auto& pulses = product.channels[channel_index].pulses;
            for (std::size_t pulse_index = 0; pulse_index < pulses.size(); ++pulse_index) {
                if (pulses[pulse_index].parameters.vector_index != pulse_index) {
                    AddError(
                        result,
                        "pulse_order_mismatch",
                        PulsePath(channel_index, pulse_index) + ".parameters.vector_index",
                        "pulse vector_index must match its zero-based position in the channel");
                }
                if (pulse_index > 0 &&
                    pulses[pulse_index].parameters.time_seconds < pulses[pulse_index - 1].parameters.time_seconds) {
                    AddError(
                        result,
                        "pulse_time_order_mismatch",
                        PulsePath(channel_index, pulse_index) + ".parameters.time_seconds",
                        "pulse time_seconds must be monotonically nondecreasing within a channel");
                }
            }
        }
    }

    static void ValidateSampleTypes(const NormalizedSarProduct& product, SarValidationResult& result) {
        for (std::size_t channel_index = 0; channel_index < product.channels.size(); ++channel_index) {
            const auto& channel = product.channels[channel_index];
            if (channel.waveform.sample_type != "complex_f32") {
                AddError(
                    result,
                    "unsupported_sample_type",
                    ChannelPath(channel_index) + ".waveform.sample_type",
                    "normalized SAR product samples must be complex_f32");
            }
        }
    }

    static void ValidateFiniteSamples(const NormalizedSarProduct& product, SarValidationResult& result) {
        for (std::size_t channel_index = 0; channel_index < product.channels.size(); ++channel_index) {
            const auto& channel = product.channels[channel_index];
            for (std::size_t pulse_index = 0; pulse_index < channel.pulses.size(); ++pulse_index) {
                const auto& pulse = channel.pulses[pulse_index];
                for (std::size_t sample_index = 0; sample_index < pulse.samples.size(); ++sample_index) {
                    const auto& sample = pulse.samples[sample_index];
                    const auto path = SamplePath(channel_index, pulse_index, sample_index);
                    ValidateFinite(sample.real, path + ".real", result);
                    ValidateFinite(sample.imag, path + ".imag", result);
                }
            }
        }
    }

    template <typename T, std::size_t N>
    static void ValidateFiniteArray(
        const std::array<T, N>& values,
        const std::string& path,
        SarValidationResult& result) {
        for (std::size_t index = 0; index < values.size(); ++index) {
            ValidateFinite(values[index], path + "[" + std::to_string(index) + "]", result);
        }
    }

    template <typename T>
    static void ValidateFinite(T value, const std::string& path, SarValidationResult& result) {
        if (!std::isfinite(value)) {
            AddError(
                result,
                "non_finite_value",
                path,
                "value must be finite");
        }
    }
};

} // namespace graphx::sar
