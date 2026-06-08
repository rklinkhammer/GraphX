#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace sar::reference {

struct PointTarget {
    double x_m{};
    double y_m{};
    double reflectivity{1.0};
};

struct Geometry {
    std::uint32_t pulse_count{9};
    std::uint32_t range_bin_count{128};
    std::uint32_t image_width{9};
    std::uint32_t image_height{9};
    double platform_x_start_m{-4.0};
    double platform_x_end_m{4.0};
    double platform_y_m{-20.0};
    double range_origin_m{15.0};
    double range_spacing_m{0.25};
    double scene_center_x_m{0.0};
    double scene_center_y_m{0.0};
    double pixel_spacing_m{0.5};
    double wavelength_m{0.03};
};

struct Image {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<float> pixels{};
};

struct PeakMetric {
    std::uint32_t x{};
    std::uint32_t y{};
    float value{};
};

struct ErrorMetrics {
    double l_inf{};
    double rms{};
    double relative_l2{};
};

struct ChirpReferenceConfig {
    std::uint32_t sample_count{16};
    double sample_rate_hz{16.0e6};
    double bandwidth_hz{4.0e6};
    double chirp_duration_s{1.0e-6};
    double carrier_hz{9.6e9};
    double range_origin_m{0.0};
    double range_spacing_m{0.25};
    double wavelength_m{0.03};
};

struct ImageQualityMetrics {
    PeakMetric peak{};
    double peak_location_error_pixels{};
    double impulse_response_width_pixels{};
    double peak_sidelobe_ratio_db{};
    double integrated_sidelobe_ratio_db{};
    double dynamic_range_db{};
    std::uint64_t image_hash{};
};

struct BackprojectionAdapterConfig {
    std::uint32_t tap_count{8};
    double delay_step{0.5};
    double phase_tap_scale{0.35};
    double phase_aperture_scale{0.2};
};

inline void ValidateGeometry(const Geometry& geometry) {
    if (geometry.pulse_count == 0 || geometry.range_bin_count == 0 ||
        geometry.image_width == 0 || geometry.image_height == 0) {
        throw std::invalid_argument("SAR reference geometry dimensions must be non-zero");
    }
    if (geometry.range_spacing_m <= 0.0 || geometry.pixel_spacing_m <= 0.0 ||
        geometry.wavelength_m <= 0.0) {
        throw std::invalid_argument("SAR reference geometry spacings and wavelength must be positive");
    }
}

inline double PlatformX(const Geometry& geometry, std::uint32_t pulse) {
    if (geometry.pulse_count == 1) {
        return geometry.platform_x_start_m;
    }
    const double t = static_cast<double>(pulse) / static_cast<double>(geometry.pulse_count - 1u);
    return geometry.platform_x_start_m +
           ((geometry.platform_x_end_m - geometry.platform_x_start_m) * t);
}

inline double PixelX(const Geometry& geometry, std::uint32_t x) {
    const double center = (static_cast<double>(geometry.image_width) - 1.0) * 0.5;
    return geometry.scene_center_x_m + ((static_cast<double>(x) - center) * geometry.pixel_spacing_m);
}

inline double PixelY(const Geometry& geometry, std::uint32_t y) {
    const double center = (static_cast<double>(geometry.image_height) - 1.0) * 0.5;
    return geometry.scene_center_y_m + ((static_cast<double>(y) - center) * geometry.pixel_spacing_m);
}

inline double SlantRange(double platform_x_m,
                         double platform_y_m,
                         double target_x_m,
                         double target_y_m) {
    const double dx = target_x_m - platform_x_m;
    const double dy = target_y_m - platform_y_m;
    return std::sqrt((dx * dx) + (dy * dy));
}

inline std::vector<std::complex<double>> GeneratePointTargetPhaseHistory(
    const Geometry& geometry,
    const PointTarget& target) {
    ValidateGeometry(geometry);

    std::vector<std::complex<double>> phase_history(
        static_cast<std::size_t>(geometry.pulse_count) * geometry.range_bin_count,
        std::complex<double>{0.0, 0.0});

    constexpr double kPi = 3.141592653589793238462643383279502884;
    for (std::uint32_t pulse = 0; pulse < geometry.pulse_count; ++pulse) {
        const double platform_x = PlatformX(geometry, pulse);
        const double range = SlantRange(platform_x, geometry.platform_y_m, target.x_m, target.y_m);
        const auto range_bin = static_cast<long long>(
            std::llround((range - geometry.range_origin_m) / geometry.range_spacing_m));
        if (range_bin < 0 ||
            range_bin >= static_cast<long long>(geometry.range_bin_count)) {
            continue;
        }

        const double phase_rad = -4.0 * kPi * range / geometry.wavelength_m;
        phase_history[(static_cast<std::size_t>(pulse) * geometry.range_bin_count) +
                      static_cast<std::size_t>(range_bin)] +=
            target.reflectivity * std::complex<double>{std::cos(phase_rad), std::sin(phase_rad)};
    }

    return phase_history;
}

inline Image BackprojectNearestRange(const Geometry& geometry,
                                     const std::vector<std::complex<double>>& phase_history) {
    ValidateGeometry(geometry);
    const auto expected_samples =
        static_cast<std::size_t>(geometry.pulse_count) * geometry.range_bin_count;
    if (phase_history.size() != expected_samples) {
        throw std::invalid_argument("SAR phase-history size does not match geometry");
    }

    Image image{};
    image.width = geometry.image_width;
    image.height = geometry.image_height;
    image.pixels.assign(
        static_cast<std::size_t>(geometry.image_width) * geometry.image_height,
        0.0f);

    constexpr double kPi = 3.141592653589793238462643383279502884;
    for (std::uint32_t y = 0; y < geometry.image_height; ++y) {
        const double pixel_y = PixelY(geometry, y);
        for (std::uint32_t x = 0; x < geometry.image_width; ++x) {
            const double pixel_x = PixelX(geometry, x);
            std::complex<double> accum{0.0, 0.0};

            for (std::uint32_t pulse = 0; pulse < geometry.pulse_count; ++pulse) {
                const double platform_x = PlatformX(geometry, pulse);
                const double range =
                    SlantRange(platform_x, geometry.platform_y_m, pixel_x, pixel_y);
                const auto range_bin = static_cast<long long>(
                    std::llround((range - geometry.range_origin_m) / geometry.range_spacing_m));
                if (range_bin < 0 ||
                    range_bin >= static_cast<long long>(geometry.range_bin_count)) {
                    continue;
                }

                const auto index =
                    (static_cast<std::size_t>(pulse) * geometry.range_bin_count) +
                    static_cast<std::size_t>(range_bin);
                const double phase_rad = 4.0 * kPi * range / geometry.wavelength_m;
                const std::complex<double> phase_correction{
                    std::cos(phase_rad),
                    std::sin(phase_rad)};
                accum += phase_history[index] * phase_correction;
            }

            const auto image_index =
                (static_cast<std::size_t>(y) * geometry.image_width) + x;
            image.pixels[image_index] =
                static_cast<float>(std::abs(accum) / static_cast<double>(geometry.pulse_count));
        }
    }

    return image;
}

inline PeakMetric FindPeak(const Image& image) {
    if (image.width == 0 || image.height == 0 || image.pixels.empty()) {
        throw std::invalid_argument("SAR image must be non-empty");
    }

    std::size_t peak_index = 0;
    float peak_value = -std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        if (image.pixels[i] > peak_value) {
            peak_value = image.pixels[i];
            peak_index = i;
        }
    }

    return PeakMetric{
        .x = static_cast<std::uint32_t>(peak_index % image.width),
        .y = static_cast<std::uint32_t>(peak_index / image.width),
        .value = peak_value,
    };
}

inline ErrorMetrics CompareImages(const Image& actual, const Image& expected) {
    if (actual.width != expected.width || actual.height != expected.height ||
        actual.pixels.size() != expected.pixels.size()) {
        throw std::invalid_argument("SAR image dimensions must match for comparison");
    }

    double l_inf = 0.0;
    double sum_sq = 0.0;
    double expected_sum_sq = 0.0;
    for (std::size_t i = 0; i < actual.pixels.size(); ++i) {
        const double diff =
            static_cast<double>(actual.pixels[i]) - static_cast<double>(expected.pixels[i]);
        l_inf = std::max(l_inf, std::abs(diff));
        sum_sq += diff * diff;
        expected_sum_sq += static_cast<double>(expected.pixels[i]) *
                           static_cast<double>(expected.pixels[i]);
    }

    const double count = static_cast<double>(actual.pixels.size());
    return ErrorMetrics{
        .l_inf = l_inf,
        .rms = std::sqrt(sum_sq / count),
        .relative_l2 = expected_sum_sq == 0.0 ? 0.0 : std::sqrt(sum_sq / expected_sum_sq),
    };
}

inline void ValidateChirpReferenceConfig(const ChirpReferenceConfig& config) {
    if (config.sample_count == 0) {
        throw std::invalid_argument("SAR chirp reference sample count must be non-zero");
    }
    if (config.sample_rate_hz <= 0.0 || config.bandwidth_hz <= 0.0 ||
        config.chirp_duration_s <= 0.0 || config.range_spacing_m <= 0.0 ||
        config.wavelength_m <= 0.0) {
        throw std::invalid_argument("SAR chirp reference frequency, duration, spacing, and wavelength must be positive");
    }
}

inline std::vector<std::complex<double>> GenerateLinearFmChirp(
    const ChirpReferenceConfig& config) {
    ValidateChirpReferenceConfig(config);

    constexpr double kPi = 3.141592653589793238462643383279502884;
    const double chirp_rate = config.bandwidth_hz / config.chirp_duration_s;
    const double center_time = 0.5 * config.chirp_duration_s;

    std::vector<std::complex<double>> chirp(config.sample_count);
    for (std::uint32_t i = 0; i < config.sample_count; ++i) {
        const double t = static_cast<double>(i) / config.sample_rate_hz;
        const double centered_t = t - center_time;
        const double phase_rad = kPi * chirp_rate * centered_t * centered_t;
        chirp[i] = {std::cos(phase_rad), std::sin(phase_rad)};
    }
    return chirp;
}

inline std::vector<std::complex<double>> GenerateDelayedEcho(
    const std::vector<std::complex<double>>& reference_chirp,
    std::uint32_t delay_samples,
    double reflectivity = 1.0) {
    if (reference_chirp.empty()) {
        throw std::invalid_argument("SAR delayed echo reference chirp must be non-empty");
    }

    std::vector<std::complex<double>> echo(reference_chirp.size(), {0.0, 0.0});
    for (std::size_t i = 0; i < reference_chirp.size(); ++i) {
        const auto out_index = i + delay_samples;
        if (out_index >= echo.size()) {
            break;
        }
        echo[out_index] += reference_chirp[i] * reflectivity;
    }
    return echo;
}

inline std::vector<std::complex<double>> MatchedFilterRangeCompress(
    const std::vector<std::complex<double>>& received,
    const std::vector<std::complex<double>>& reference_chirp) {
    if (received.empty() || reference_chirp.empty()) {
        throw std::invalid_argument("SAR matched filter inputs must be non-empty");
    }
    if (received.size() != reference_chirp.size()) {
        throw std::invalid_argument("SAR matched filter inputs must have matching sample counts");
    }

    std::vector<std::complex<double>> compressed(received.size(), {0.0, 0.0});
    for (std::size_t delay = 0; delay < received.size(); ++delay) {
        std::complex<double> accum{0.0, 0.0};
        for (std::size_t i = 0; i + delay < received.size(); ++i) {
            accum += received[i + delay] * std::conj(reference_chirp[i]);
        }
        compressed[delay] = accum;
    }
    return compressed;
}

inline Image MagnitudeImage(std::uint32_t width,
                            std::uint32_t height,
                            const std::vector<std::complex<double>>& samples) {
    if (width == 0 || height == 0) {
        throw std::invalid_argument("SAR magnitude image dimensions must be non-zero");
    }
    if (samples.size() != static_cast<std::size_t>(width) * height) {
        throw std::invalid_argument("SAR magnitude image sample count must match dimensions");
    }

    Image image{};
    image.width = width;
    image.height = height;
    image.pixels.reserve(samples.size());
    for (const auto& sample : samples) {
        image.pixels.push_back(static_cast<float>(std::abs(sample)));
    }
    return image;
}

inline std::vector<float> RunBackprojectionAdapterReference(
    const std::vector<float>& range_tile,
    const BackprojectionAdapterConfig& config) {
    if (range_tile.empty()) {
        throw std::invalid_argument("SAR adapter reference input must be non-empty");
    }
    if (config.tap_count == 0) {
        throw std::invalid_argument("SAR adapter reference tap count must be non-zero");
    }

    constexpr double kPi = 3.141592653589793238462643383279502884;
    const auto sample_count = range_tile.size();
    const double inv_tap_count = 1.0 / static_cast<double>(config.tap_count);

    std::vector<float> output(sample_count, 0.0f);
    for (std::size_t gid = 0; gid < sample_count; ++gid) {
        const double aperture_norm =
            static_cast<double>(gid) / static_cast<double>(sample_count);
        double accum = 0.0;

        for (std::uint32_t tap = 0; tap < config.tap_count; ++tap) {
            const double delay =
                static_cast<double>(gid) + (config.delay_step * static_cast<double>(tap));
            const double sample_pos = std::clamp(
                delay,
                0.0,
                static_cast<double>(sample_count - 1u));
            const auto idx0 = static_cast<std::size_t>(std::floor(sample_pos));
            const auto idx1 = std::min(sample_count - 1u, idx0 + 1u);
            const double frac = sample_pos - static_cast<double>(idx0);
            const double sample =
                (static_cast<double>(range_tile[idx0]) * (1.0 - frac)) +
                (static_cast<double>(range_tile[idx1]) * frac);
            const double aperture_weight =
                0.5 - (0.5 * std::cos(2.0 * kPi *
                                      ((static_cast<double>(tap) + 0.5) * inv_tap_count)));
            const double phase =
                ((config.phase_tap_scale * static_cast<double>(tap)) +
                 (config.phase_aperture_scale * aperture_norm)) *
                kPi;
            const double phasor = std::cos(phase) + (0.5 * std::sin(phase));
            accum += sample * aperture_weight * phasor;
        }

        output[gid] = static_cast<float>(accum * inv_tap_count);
    }

    return output;
}

inline ErrorMetrics CompareVectors(const std::vector<float>& actual,
                                   const std::vector<float>& expected) {
    if (actual.size() != expected.size()) {
        throw std::invalid_argument("SAR vector sizes must match for comparison");
    }
    Image actual_image{};
    actual_image.width = static_cast<std::uint32_t>(actual.size());
    actual_image.height = 1;
    actual_image.pixels = actual;

    Image expected_image{};
    expected_image.width = static_cast<std::uint32_t>(expected.size());
    expected_image.height = 1;
    expected_image.pixels = expected;
    return CompareImages(actual_image, expected_image);
}

inline std::uint64_t QuantizedImageHash(const Image& image, double scale = 1.0e6) {
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };

    mix(image.width);
    mix(image.height);
    for (float pixel : image.pixels) {
        const auto quantized = static_cast<std::int64_t>(std::llround(
            static_cast<double>(pixel) * scale));
        mix(static_cast<std::uint64_t>(quantized));
    }
    return hash;
}

inline ImageQualityMetrics MeasureImageQuality(const Image& image,
                                               std::uint32_t expected_peak_x,
                                               std::uint32_t expected_peak_y) {
    if (image.width == 0 || image.height == 0 || image.pixels.empty()) {
        throw std::invalid_argument("SAR image quality metrics require a non-empty image");
    }
    if (expected_peak_x >= image.width || expected_peak_y >= image.height) {
        throw std::invalid_argument("SAR expected peak location must be inside the image");
    }

    constexpr double kEpsilon = 1.0e-12;
    const auto peak = FindPeak(image);
    const double dx = static_cast<double>(peak.x) - static_cast<double>(expected_peak_x);
    const double dy = static_cast<double>(peak.y) - static_cast<double>(expected_peak_y);

    double max_sidelobe = 0.0;
    double sidelobe_energy = 0.0;
    double mainlobe_energy = 0.0;
    float min_positive = std::numeric_limits<float>::max();
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const auto index = static_cast<std::size_t>(y) * image.width + x;
            const double value = static_cast<double>(image.pixels[index]);
            const double energy = value * value;
            const bool in_mainlobe =
                std::abs(static_cast<int>(x) - static_cast<int>(peak.x)) <= 1 &&
                std::abs(static_cast<int>(y) - static_cast<int>(peak.y)) <= 1;
            if (in_mainlobe) {
                mainlobe_energy += energy;
            } else {
                max_sidelobe = std::max(max_sidelobe, std::abs(value));
                sidelobe_energy += energy;
            }
            if (image.pixels[index] > 0.0f) {
                min_positive = std::min(min_positive, image.pixels[index]);
            }
        }
    }

    std::uint32_t response_width = 0;
    const double half_power = static_cast<double>(peak.value) * 0.5;
    const auto peak_row_offset = static_cast<std::size_t>(peak.y) * image.width;
    for (std::uint32_t x = 0; x < image.width; ++x) {
        if (static_cast<double>(image.pixels[peak_row_offset + x]) >= half_power) {
            ++response_width;
        }
    }

    const double peak_value = std::max(static_cast<double>(peak.value), kEpsilon);
    const double pslr = 20.0 * std::log10(std::max(max_sidelobe, kEpsilon) / peak_value);
    const double islr = 10.0 * std::log10(
        std::max(sidelobe_energy, kEpsilon) / std::max(mainlobe_energy, kEpsilon));
    const double min_value = min_positive == std::numeric_limits<float>::max()
                                 ? kEpsilon
                                 : static_cast<double>(min_positive);
    const double dynamic_range = 20.0 * std::log10(peak_value / std::max(min_value, kEpsilon));

    return ImageQualityMetrics{
        .peak = peak,
        .peak_location_error_pixels = std::sqrt((dx * dx) + (dy * dy)),
        .impulse_response_width_pixels = static_cast<double>(response_width),
        .peak_sidelobe_ratio_db = pslr,
        .integrated_sidelobe_ratio_db = islr,
        .dynamic_range_db = dynamic_range,
        .image_hash = QuantizedImageHash(image),
    };
}

} // namespace sar::reference
