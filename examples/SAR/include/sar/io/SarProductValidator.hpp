#pragma once

#include "sar/io/NormalizedSarProduct.hpp"

#include <array>
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
    std::vector<SarValidationIssue> warnings{};

    [[nodiscard]] bool ok() const noexcept {
        return errors.empty();
    }

    [[nodiscard]] bool has_warnings() const noexcept {
        return !warnings.empty();
    }
};

class SarProductValidator {
public:
    [[nodiscard]] static SarValidationResult Validate(const NormalizedSarProduct& product) {
        SarValidationResult result{};
        ValidateRequiredFields(product, result);
        ValidateFiniteMetadata(product, result);
        ValidatePulseCountConsistency(product, result);
        ValidateShapeConsistency(product, result);
        ValidateFrequencyMetadataConsistency(product, result);
        ValidatePulseFileMetadata(product, result);
        ValidateGeometryCompleteness(product, result);
        ValidatePulseOrdering(product, result);
        ValidatePlatformVariationInfo(product, result);
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

    static void AddWarning(
        SarValidationResult& result,
        std::string code,
        std::string path,
        std::string message) {
        result.warnings.push_back(SarValidationIssue{
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

    static void ValidatePulseCountConsistency(
        const NormalizedSarProduct& product,
        SarValidationResult& result) {
        if (product.channels.empty()) {
            return;
        }

        const auto pulse_count = product.Shape().pulse_count;
        if (product.collection.expected_pulse_count.has_value() &&
            pulse_count != *product.collection.expected_pulse_count) {
            AddError(
                result,
                "pulse_count_mismatch",
                "collection.expected_pulse_count",
                "shape pulse count does not match collection.expected_pulse_count");
        }

        if (!product.collection.source_files.empty() &&
            pulse_count < product.collection.source_files.size()) {
            AddError(
                result,
                "pulse_count_inconsistent",
                "collection.source_files",
                "pulse count is smaller than source file count");
        }
    }

    static void ValidateFrequencyMetadataConsistency(
        const NormalizedSarProduct& product,
        SarValidationResult& result) {
        if (product.channels.empty()) {
            return;
        }

        const auto& reference_waveform = product.channels.front().waveform;
        for (std::size_t channel_index = 0; channel_index < product.channels.size(); ++channel_index) {
            const auto& waveform = product.channels[channel_index].waveform;
            const auto prefix = ChannelPath(channel_index) + ".waveform";

            if (!waveform.frequency_axis_hz.empty()) {
                for (std::size_t i = 1; i < waveform.frequency_axis_hz.size(); ++i) {
                    if (waveform.frequency_axis_hz[i] <= waveform.frequency_axis_hz[i - 1]) {
                        AddError(
                            result,
                            "frequency_axis_not_strictly_increasing",
                            prefix + ".frequency_axis_hz",
                            "frequency_axis_hz must be strictly increasing");
                        break;
                    }
                }
            }

            if (channel_index == 0) {
                continue;
            }

            if (waveform.carrier_hz != reference_waveform.carrier_hz ||
                waveform.bandwidth_hz != reference_waveform.bandwidth_hz ||
                waveform.sample_rate_hz != reference_waveform.sample_rate_hz) {
                AddError(
                    result,
                    "frequency_metadata_mismatch",
                    prefix,
                    "frequency metadata must match the first channel");
            }

            if (waveform.frequency_axis_hz != reference_waveform.frequency_axis_hz) {
                AddError(
                    result,
                    "frequency_axis_mismatch",
                    prefix + ".frequency_axis_hz",
                    "frequency_axis_hz must match the first channel");
            }
        }
    }

    static void ValidatePulseFileMetadata(const NormalizedSarProduct& product, SarValidationResult& result) {
        for (std::size_t channel_index = 0; channel_index < product.channels.size(); ++channel_index) {
            const auto& pulses = product.channels[channel_index].pulses;
            bool has_any_source_file_index = false;
            bool has_any_source_pulse_index = false;

            for (const auto& pulse : pulses) {
                has_any_source_file_index = has_any_source_file_index || pulse.parameters.source_file_index.has_value();
                has_any_source_pulse_index = has_any_source_pulse_index || pulse.parameters.source_pulse_index.has_value();
            }

            if (!has_any_source_file_index && !has_any_source_pulse_index) {
                continue;
            }

            for (std::size_t pulse_index = 0; pulse_index < pulses.size(); ++pulse_index) {
                const auto& params = pulses[pulse_index].parameters;
                const auto pulse_path = PulsePath(channel_index, pulse_index) + ".parameters";

                if (!params.source_file_index.has_value() || !params.source_pulse_index.has_value()) {
                    AddError(
                        result,
                        "pulse_file_metadata_incomplete",
                        pulse_path,
                        "source_file_index and source_pulse_index must be provided together");
                    continue;
                }

                if (!product.collection.source_files.empty() &&
                    *params.source_file_index >= product.collection.source_files.size()) {
                    AddError(
                        result,
                        "pulse_file_index_out_of_bounds",
                        pulse_path + ".source_file_index",
                        "source_file_index must reference a valid collection.source_files entry");
                }

                if (pulse_index == 0) {
                    continue;
                }

                const auto& previous = pulses[pulse_index - 1].parameters;
                if (!previous.source_file_index.has_value() || !previous.source_pulse_index.has_value()) {
                    continue;
                }

                if (*params.source_file_index < *previous.source_file_index ||
                    (*params.source_file_index == *previous.source_file_index &&
                     *params.source_pulse_index <= *previous.source_pulse_index)) {
                    AddError(
                        result,
                        "pulse_file_sequence_mismatch",
                        pulse_path,
                        "(source_file_index, source_pulse_index) must be strictly increasing in vector order");
                }
            }
        }
    }

    static void ValidateGeometryCompleteness(const NormalizedSarProduct& product, SarValidationResult& result) {
        for (std::size_t channel_index = 0; channel_index < product.channels.size(); ++channel_index) {
            const auto& pulses = product.channels[channel_index].pulses;
            for (std::size_t pulse_index = 0; pulse_index < pulses.size(); ++pulse_index) {
                const auto& position = pulses[pulse_index].parameters.platform.position_m;
                if (position[0] == 0.0 && position[1] == 0.0 && position[2] == 0.0) {
                    AddError(
                        result,
                        "geometry_incomplete",
                        PulsePath(channel_index, pulse_index) + ".parameters.platform.position_m",
                        "platform.position_m must be populated for every pulse");
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

    static void ValidatePlatformVariationInfo(const NormalizedSarProduct& product, SarValidationResult& result) {
        for (std::size_t channel_index = 0; channel_index < product.channels.size(); ++channel_index) {
            const auto& pulses = product.channels[channel_index].pulses;
            if (pulses.size() < 2) {
                continue;
            }

            const auto& reference_position = pulses.front().parameters.platform.position_m;
            const auto& reference_velocity = pulses.front().parameters.platform.velocity_mps;
            bool has_variation = false;
            for (std::size_t pulse_index = 1; pulse_index < pulses.size(); ++pulse_index) {
                if (pulses[pulse_index].parameters.platform.position_m != reference_position ||
                    pulses[pulse_index].parameters.platform.velocity_mps != reference_velocity) {
                    has_variation = true;
                    break;
                }
            }

            if (has_variation) {
                AddWarning(
                    result,
                    "platform_state_varies_per_pulse",
                    ChannelPath(channel_index),
                    "platform state varies across pulses; this is expected for full-aperture products");
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
