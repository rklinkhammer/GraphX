#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace graphx::sar {

struct ComplexSample {
    float real{0.0f};
    float imag{0.0f};
};

struct PlatformState {
    std::array<double, 3> position_m{};
    std::array<double, 3> velocity_mps{};
};

struct PerVectorParameters {
    std::uint64_t vector_index{0};
    double time_seconds{0.0};
    std::uint64_t range_sample_start{0};
    std::optional<std::uint64_t> source_file_index{};
    std::optional<std::uint64_t> source_pulse_index{};
    PlatformState platform{};
};

struct WaveformMetadata {
    std::string waveform_id{};
    double carrier_hz{0.0};
    double bandwidth_hz{0.0};
    double sample_rate_hz{0.0};
    std::string sample_type{"complex_f32"};
    std::string polarization{};
    std::vector<double> frequency_axis_hz{};
};

struct PulseVector {
    PerVectorParameters parameters{};
    std::vector<ComplexSample> samples{};
};

struct ChannelSignal {
    std::string channel_id{};
    WaveformMetadata waveform{};
    std::vector<PulseVector> pulses{};
};

struct CollectionMetadata {
    std::string product_id{};
    std::string collector_name{};
    std::string collection_id{};
    std::string coordinate_frame{};
    std::string time_basis{};
    std::vector<std::string> source_files{};
    std::optional<std::uint64_t> expected_pulse_count{};
    std::string provenance_label{};
    std::string source_ordering{};
};

struct ReferenceGeometry {
    std::array<double, 3> scene_center_m{};
    PlatformState reference_platform{};
};

struct ProductShape {
    std::size_t pulse_count{0};
    std::size_t channel_count{0};
    std::size_t max_sample_count{0};
};

struct NormalizedSarProduct {
    CollectionMetadata collection{};
    ReferenceGeometry reference_geometry{};
    std::vector<ChannelSignal> channels{};

    [[nodiscard]] ProductShape Shape() const noexcept {
        ProductShape shape{};
        shape.channel_count = channels.size();
        for (const auto& channel : channels) {
            if (channel.pulses.size() > shape.pulse_count) {
                shape.pulse_count = channel.pulses.size();
            }
            for (const auto& pulse : channel.pulses) {
                if (pulse.samples.size() > shape.max_sample_count) {
                    shape.max_sample_count = pulse.samples.size();
                }
            }
        }
        return shape;
    }

    [[nodiscard]] bool Empty() const noexcept {
        return channels.empty();
    }

    [[nodiscard]] const ChannelSignal& Channel(std::size_t channel_index) const {
        return channels.at(channel_index);
    }

    [[nodiscard]] ChannelSignal& Channel(std::size_t channel_index) {
        return channels.at(channel_index);
    }

    [[nodiscard]] const PulseVector& Pulse(std::size_t pulse_index, std::size_t channel_index) const {
        return channels.at(channel_index).pulses.at(pulse_index);
    }

    [[nodiscard]] PulseVector& Pulse(std::size_t pulse_index, std::size_t channel_index) {
        return channels.at(channel_index).pulses.at(pulse_index);
    }

    [[nodiscard]] const ComplexSample& Sample(
        std::size_t pulse_index,
        std::size_t channel_index,
        std::size_t sample_index) const {
        return Pulse(pulse_index, channel_index).samples.at(sample_index);
    }

    [[nodiscard]] ComplexSample& Sample(
        std::size_t pulse_index,
        std::size_t channel_index,
        std::size_t sample_index) {
        return Pulse(pulse_index, channel_index).samples.at(sample_index);
    }

    [[nodiscard]] std::vector<std::string> MissingRequiredFields() const {
        std::vector<std::string> missing{};

        if (collection.product_id.empty()) {
            missing.emplace_back("collection.product_id");
        }
        if (collection.coordinate_frame.empty()) {
            missing.emplace_back("collection.coordinate_frame");
        }
        if (collection.time_basis.empty()) {
            missing.emplace_back("collection.time_basis");
        }
        if (channels.empty()) {
            missing.emplace_back("channels");
            return missing;
        }

        for (std::size_t channel_index = 0; channel_index < channels.size(); ++channel_index) {
            const auto& channel = channels[channel_index];
            const auto prefix = std::string{"channels["} + std::to_string(channel_index) + "]";
            if (channel.channel_id.empty()) {
                missing.emplace_back(prefix + ".channel_id");
            }
            if (channel.waveform.waveform_id.empty()) {
                missing.emplace_back(prefix + ".waveform.waveform_id");
            }
            if (channel.waveform.sample_rate_hz <= 0.0) {
                missing.emplace_back(prefix + ".waveform.sample_rate_hz");
            }
            if (channel.pulses.empty()) {
                missing.emplace_back(prefix + ".pulses");
                continue;
            }
            for (std::size_t pulse_index = 0; pulse_index < channel.pulses.size(); ++pulse_index) {
                if (channel.pulses[pulse_index].samples.empty()) {
                    missing.emplace_back(prefix + ".pulses[" + std::to_string(pulse_index) + "].samples");
                }
            }
        }

        return missing;
    }

    [[nodiscard]] bool HasRequiredFields() const {
        return MissingRequiredFields().empty();
    }
};

struct SarReadResult {
    bool success{false};
    std::string message{};
    NormalizedSarProduct product{};
};

struct SarWriteResult {
    bool success{false};
    std::string message{};
};

class ISarReader {
public:
    virtual ~ISarReader() = default;

    [[nodiscard]] virtual SarReadResult Read(const std::filesystem::path& path) const = 0;
};

class ISarWriter {
public:
    virtual ~ISarWriter() = default;

    [[nodiscard]] virtual SarWriteResult Write(
        const std::filesystem::path& path,
        const NormalizedSarProduct& product) const = 0;
};

} // namespace graphx::sar
